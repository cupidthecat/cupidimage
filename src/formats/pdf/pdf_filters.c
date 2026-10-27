#include "pdf_internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PDF_FILTER_LIMIT (64u * 1024u * 1024u)

typedef struct {
    unsigned char *data;
    size_t used, capacity;
} pdf_filter_buffer;

static int pdf_filter_append(pdf_filter_buffer *out, const unsigned char *data, size_t count) {
    if (count > PDF_FILTER_LIMIT - out->used) return 0;
    size_t needed = out->used + count;
    if (needed > out->capacity) {
        size_t capacity = out->capacity ? out->capacity : 256;
        while (capacity < needed) capacity *= 2;
        unsigned char *next = (unsigned char *)realloc(out->data, capacity);
        if (!next) return 0;
        out->data = next; out->capacity = capacity;
    }
    if (count) memcpy(out->data + out->used, data, count);
    out->used = needed;
    return 1;
}

/* ISO 32000-1, 7.4.2: ignore whitespace and pad an odd final nibble. */
static int pdf_filter_hex(const unsigned char *data, size_t length, pdf_filter_buffer *out) {
    int high = -1;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = data[i];
        if (c == '>') {
            unsigned char value = (unsigned char)(high * 16);
            return high < 0 || pdf_filter_append(out, &value, 1);
        }
        if (cupidimage_pdf_is_ws(c)) continue;
        int low = c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : -1;
        if (low < 0) return 0;
        if (high < 0) high = low;
        else {
            unsigned char value = (unsigned char)(high * 16 + low);
            if (!pdf_filter_append(out, &value, 1)) return 0;
            high = -1;
        }
    }
    return 0;
}

/* ISO 32000-1, 7.4.3: z is valid only between tuples; ~> terminates. */
static int pdf_filter_ascii85(const unsigned char *data, size_t length, pdf_filter_buffer *out) {
    uint64_t tuple = 0;
    int digits = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = data[i], bytes[4];
        if (cupidimage_pdf_is_ws(c)) continue;
        if (c == '~') {
            if (++i >= length || data[i] != '>' || digits == 1) return 0;
            if (!digits) return 1;
            int count = digits-1;
            while (digits++ < 5) tuple = tuple * 85 + 84;
            if (tuple > UINT32_MAX) return 0;
            for (int k = 0; k < 4; k++) bytes[k] = (unsigned char)(tuple >> (24-8*k));
            return pdf_filter_append(out, bytes, (size_t)count);
        }
        if (c == 'z') {
            if (digits) return 0;
            memset(bytes, 0, sizeof(bytes));
            if (!pdf_filter_append(out, bytes, 4)) return 0;
            continue;
        }
        if (c < '!' || c > 'u') return 0;
        tuple = tuple * 85 + (c-'!');
        if (++digits == 5) {
            if (tuple > UINT32_MAX) return 0;
            for (int k = 0; k < 4; k++) bytes[k] = (unsigned char)(tuple >> (24-8*k));
            if (!pdf_filter_append(out, bytes, 4)) return 0;
            digits = 0; tuple = 0;
        }
    }
    return 0;
}

static int pdf_filter_runlength(const unsigned char *data, size_t length, pdf_filter_buffer *out) {
    size_t pos = 0;
    while (pos < length) {
        unsigned code = data[pos++];
        if (code == 128) return 1;
        if (code < 128) {
            size_t count = code+1;
            if (count > length-pos || !pdf_filter_append(out, data+pos, count)) return 0;
            pos += count;
        } else {
            unsigned char run[128];
            if (pos == length) return 0;
            memset(run, data[pos++], 257-code);
            if (!pdf_filter_append(out, run, 257-code)) return 0;
        }
    }
    return 0;
}

static int pdf_filter_null(const pdf_object_view *view, size_t *pos) {
    if (view->len - *pos < 4 || memcmp(view->data + *pos, "null", 4)) return 0;
    *pos += 4;
    return *pos == view->len || cupidimage_pdf_is_ws(view->data[*pos]) || cupidimage_pdf_is_delim(view->data[*pos]);
}

/* Non-default predictors are handled by the sampled-image decoder. This path
 * accepts only null parameters, rather than silently ignoring a predictor. */
int cupidimage_pdf_decode_stream(const pdf_object_view *view, const unsigned char *data,
                                size_t length, unsigned char **decoded, size_t *decoded_length) {
    char filters[8][32];
    int count = 0;
    size_t pos;
    *decoded = NULL; *decoded_length = 0;
    if (cupidimage_pdf_page_value(view, "Filter", &pos) && !pdf_filter_null(view, &pos)) {
        int array = pos < view->len && view->data[pos] == '[';
        if (array) pos++;
        for (;;) {
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
            if (array && pos < view->len && view->data[pos] == ']') { pos++; break; }
            if (count == 8 || pos == view->len || view->data[pos++] != '/') return 0;
            size_t start = pos;
            while (pos < view->len && !cupidimage_pdf_is_ws(view->data[pos]) && !cupidimage_pdf_is_delim(view->data[pos])) pos++;
            size_t n = pos-start;
            if (!n || n >= sizeof(filters[0])) return 0;
            memcpy(filters[count], view->data+start, n); filters[count++][n] = 0;
            if (!array) break;
        }
    }
    if (cupidimage_pdf_page_value(view, "DecodeParms", &pos) && !pdf_filter_null(view, &pos)) {
        if (pos == view->len || view->data[pos++] != '[') return 0;
        for (int i = 0; i < count; i++) {
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
            if (!pdf_filter_null(view, &pos)) return 0;
        }
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
        if (pos == view->len || view->data[pos] != ']') return 0;
    }
    unsigned char *owned = NULL;
    for (int i = 0; i < count; i++) {
        pdf_filter_buffer out = {0};
        const char *name = filters[i];
        int ok = 0;
        if (!strcmp(name, "FlateDecode") || !strcmp(name, "Fl"))
            ok = cupidimage_pdf_inflate_auto(data, length, &out.data, &out.used);
        else if (!strcmp(name, "ASCIIHexDecode") || !strcmp(name, "AHx")) ok = pdf_filter_hex(data, length, &out);
        else if (!strcmp(name, "ASCII85Decode") || !strcmp(name, "A85")) ok = pdf_filter_ascii85(data, length, &out);
        else if (!strcmp(name, "RunLengthDecode") || !strcmp(name, "RL")) ok = pdf_filter_runlength(data, length, &out);
        free(owned);
        if (!ok) { free(out.data); return 0; }
        owned = out.data; data = owned; length = out.used;
    }
    if (!count) {
        if (length > PDF_FILTER_LIMIT) return 0;
        owned = (unsigned char *)malloc(length ? length : 1);
        if (!owned) return 0;
        if (length) memcpy(owned, data, length);
    }
    *decoded = owned; *decoded_length = length;
    return 1;
}
