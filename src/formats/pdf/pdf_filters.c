#include "compression/ccitt.h"
#include "pdf_internal.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PDF_FILTER_LIMIT (64u * 1024u * 1024u)

typedef struct {
    unsigned char *data;
    size_t used, capacity;
} pdf_filter_buffer;

static int pdf_filter_append(pdf_filter_buffer *out, const unsigned char *data, size_t count) {
    if (count > PDF_FILTER_LIMIT - out->used)
        return 0;
    size_t needed = out->used + count;
    if (needed > out->capacity) {
        size_t capacity = out->capacity ? out->capacity : 256;
        while (capacity < needed)
            capacity *= 2;
        unsigned char *next = (unsigned char *)realloc(out->data, capacity);
        if (!next)
            return 0;
        out->data = next;
        out->capacity = capacity;
    }
    if (count)
        memcpy(out->data + out->used, data, count);
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
        if (cupidimage_pdf_is_ws(c))
            continue;
        int low = c >= '0' && c <= '9'   ? c - '0'
                  : c >= 'a' && c <= 'f' ? c - 'a' + 10
                  : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                         : -1;
        if (low < 0)
            return 0;
        if (high < 0)
            high = low;
        else {
            unsigned char value = (unsigned char)(high * 16 + low);
            if (!pdf_filter_append(out, &value, 1))
                return 0;
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
        if (cupidimage_pdf_is_ws(c))
            continue;
        if (c == '~') {
            if (++i >= length || data[i] != '>' || digits == 1)
                return 0;
            if (!digits)
                return 1;
            int count = digits - 1;
            while (digits++ < 5)
                tuple = tuple * 85 + 84;
            if (tuple > UINT32_MAX)
                return 0;
            for (int k = 0; k < 4; k++)
                bytes[k] = (unsigned char)(tuple >> (24 - 8 * k));
            return pdf_filter_append(out, bytes, (size_t)count);
        }
        if (c == 'z') {
            if (digits)
                return 0;
            memset(bytes, 0, sizeof(bytes));
            if (!pdf_filter_append(out, bytes, 4))
                return 0;
            continue;
        }
        if (c < '!' || c > 'u')
            return 0;
        tuple = tuple * 85 + (c - '!');
        if (++digits == 5) {
            if (tuple > UINT32_MAX)
                return 0;
            for (int k = 0; k < 4; k++)
                bytes[k] = (unsigned char)(tuple >> (24 - 8 * k));
            if (!pdf_filter_append(out, bytes, 4))
                return 0;
            digits = 0;
            tuple = 0;
        }
    }
    return 0;
}

static int pdf_filter_runlength(const unsigned char *data, size_t length, pdf_filter_buffer *out) {
    size_t pos = 0;
    while (pos < length) {
        unsigned code = data[pos++];
        if (code == 128)
            return 1;
        if (code < 128) {
            size_t count = code + 1;
            if (count > length - pos || !pdf_filter_append(out, data + pos, count))
                return 0;
            pos += count;
        } else {
            unsigned char run[128];
            if (pos == length)
                return 0;
            memset(run, data[pos++], 257 - code);
            if (!pdf_filter_append(out, run, 257 - code))
                return 0;
        }
    }
    return 0;
}

typedef struct {
    int predictor, colors, columns, bits, early, color_transform;
    cupidimage_ccitt_options fax;
} pdf_filter_params;

static int pdf_filter_lzw(const unsigned char *data, size_t length, int early,
                          pdf_filter_buffer *out) {
    uint16_t prefix[4096];
    unsigned char suffix[4096], reverse[4096], phrase[4096];
    unsigned width = 9, next = 258, buffered = 0;
    uint32_t bits = 0;
    size_t pos = 0;
    int previous = -1;
    unsigned char first = 0;
    for (;;) {
        while (buffered < width) {
            if (pos == length)
                return 0;
            bits = (bits << 8) | data[pos++];
            buffered += 8;
        }
        buffered -= width;
        unsigned code = (bits >> buffered) & ((1u << width) - 1u);
        if (code == 256) {
            width = 9;
            next = 258;
            previous = -1;
            continue;
        }
        if (code == 257)
            return 1;
        unsigned current = code;
        size_t used = 0;
        if (code >= next) {
            if (code != next || previous < 0)
                return 0;
            reverse[used++] = first;
            current = (unsigned)previous;
        }
        while (current >= 256) {
            if (current < 258 || current >= next || used == sizeof(reverse))
                return 0;
            reverse[used++] = suffix[current];
            current = prefix[current];
        }
        if (used == sizeof(reverse))
            return 0;
        first = (unsigned char)current;
        reverse[used++] = first;
        for (size_t i = 0; i < used; i++)
            phrase[i] = reverse[used - 1 - i];
        if (!pdf_filter_append(out, phrase, used))
            return 0;
        if (previous >= 0 && next < 4096) {
            prefix[next] = (uint16_t)previous;
            suffix[next++] = first;
            if (width < 12 && next == (1u << width) - (unsigned)early)
                width++;
        }
        previous = (int)code;
    }
}

static uint32_t pdf_filter_sample(const unsigned char *row, size_t sample, unsigned bits) {
    if (bits == 16)
        return (uint32_t)row[sample * 2] * 256 + row[sample * 2 + 1];
    size_t bit = sample * bits;
    return (row[bit / 8] >> (8 - bits - (bit % 8))) & ((1u << bits) - 1);
}

static void pdf_filter_store(unsigned char *row, size_t sample, unsigned bits, uint32_t value) {
    if (bits == 16) {
        row[sample * 2] = (unsigned char)(value >> 8);
        row[sample * 2 + 1] = (unsigned char)value;
        return;
    }
    size_t bit = sample * bits;
    unsigned shift = 8 - bits - (unsigned)(bit % 8), mask = ((1u << bits) - 1) << shift;
    row[bit / 8] = (unsigned char)((row[bit / 8] & ~mask) | ((value << shift) & mask));
}

static int pdf_filter_predict(pdf_filter_buffer *out, const pdf_filter_params *params) {
    if (params->predictor == 1)
        return 1;
    if (params->predictor != 2 && (params->predictor < 10 || params->predictor > 15))
        return 0;
    if (params->colors < 1 || params->columns < 1 ||
        (params->bits != 1 && params->bits != 2 && params->bits != 4 && params->bits != 8 &&
         params->bits != 16))
        return 0;
    uint64_t samples64 = (uint64_t)params->colors * (unsigned)params->columns;
    if (samples64 > (uint64_t)PDF_FILTER_LIMIT * 8 / (unsigned)params->bits)
        return 0;
    uint64_t row64 = (samples64 * (unsigned)params->bits + 7) / 8;
    if (row64 > PDF_FILTER_LIMIT)
        return 0;
    size_t row = (size_t)row64, samples = (size_t)samples64;
    size_t stride = row + (params->predictor >= 10);
    if (out->used % stride)
        return 0;
    size_t rows = out->used / stride;
    size_t bpp = ((size_t)params->colors * params->bits + 7) / 8;
    for (size_t y = 0; y < rows; y++) {
        unsigned char *scan = out->data + y * stride;
        if (params->predictor == 2) {
            for (size_t i = (size_t)params->colors; i < samples; i++) {
                uint32_t value =
                    pdf_filter_sample(scan, i, (unsigned)params->bits) +
                    pdf_filter_sample(scan, i - params->colors, (unsigned)params->bits);
                pdf_filter_store(scan, i, (unsigned)params->bits, value);
            }
        } else {
            unsigned filter = *scan++;
            if (filter > 4)
                return 0;
            for (size_t x = 0; x < row; x++) {
                int a = x >= bpp ? scan[x - bpp] : 0;
                int b = y ? out->data[(y - 1) * row + x] : 0;
                int c = y && x >= bpp ? out->data[(y - 1) * row + x - bpp] : 0;
                int prediction = 0;
                if (filter == 1)
                    prediction = a;
                else if (filter == 2)
                    prediction = b;
                else if (filter == 3)
                    prediction = (a + b) / 2;
                else if (filter == 4) {
                    int p = a + b - c, da = abs(p - a), db = abs(p - b), dc = abs(p - c);
                    prediction = da <= db && da <= dc ? a : db <= dc ? b : c;
                }
                scan[x] = (unsigned char)(scan[x] + prediction);
            }
        }
        memmove(out->data + y * row, scan, row);
    }
    out->used = rows * row;
    return 1;
}

static int pdf_filter_null(const pdf_object_view *view, size_t *pos) {
    if (view->len - *pos < 4 || memcmp(view->data + *pos, "null", 4))
        return 0;
    *pos += 4;
    return *pos == view->len || cupidimage_pdf_is_ws(view->data[*pos]) ||
           cupidimage_pdf_is_delim(view->data[*pos]);
}

int cupidimage_pdf_filter_parameter_applies(const char *filter, const char *key) {
    int lzw = !strcmp(filter, "LZWDecode") || !strcmp(filter, "LZW");
    int predict = lzw || !strcmp(filter, "FlateDecode") || !strcmp(filter, "Fl");
    int fax = !strcmp(filter, "CCITTFaxDecode") || !strcmp(filter, "CCF");
    if (!strcmp(key, "ColorTransform"))
        return !strcmp(filter, "DCTDecode") || !strcmp(filter, "DCT");
    if (!strcmp(key, "EarlyChange"))
        return lzw;
    if (!strcmp(key, "Columns"))
        return predict || fax;
    if (!strcmp(key, "Predictor") || !strcmp(key, "Colors") || !strcmp(key, "BitsPerComponent"))
        return predict;
    return fax;
}

/* Indexed streams resolve indirect parameters before entering this decoder. */
static int pdf_filter_parameters(const pdf_object_view *view, size_t *pos,
                                 pdf_filter_params *params, const char *filter) {
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    if (pdf_filter_null(view, pos))
        return 1;
    pdf_object_view dictionary = {0};
    size_t end;
    if (!cupidimage_pdf_dictionary_view(view, *pos, &dictionary, &end))
        return 0;
    static const char *keys[] = {
        "Predictor",        "Colors",     "Columns", "BitsPerComponent",       "EarlyChange",
        "ColorTransform",   "K",          "Rows",    "DamagedRowsBeforeError", "EndOfLine",
        "EncodedByteAlign", "EndOfBlock", "BlackIs1"};
    *pos = end;
    view = &dictionary;
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        size_t at;
        if (!cupidimage_pdf_filter_parameter_applies(filter, keys[i]) ||
            !cupidimage_pdf_page_value(view, keys[i], &at))
            continue;
        size_t *pos = &at;
        const char *name = keys[i];
        size_t n = strlen(name);
        int *value = NULL;
        int boolean = 0;
        if (n == 9 && !memcmp(name, "Predictor", n))
            value = &params->predictor;
        else if (n == 6 && !memcmp(name, "Colors", n))
            value = &params->colors;
        else if (n == 7 && !memcmp(name, "Columns", n))
            value = &params->columns;
        else if (n == 16 && !memcmp(name, "BitsPerComponent", n))
            value = &params->bits;
        else if (n == 11 && !memcmp(name, "EarlyChange", n))
            value = &params->early;
        else if (n == 14 && !memcmp(name, "ColorTransform", n))
            value = &params->color_transform;
        else if (n == 1 && name[0] == 'K')
            value = &params->fax.k;
        else if (n == 4 && !memcmp(name, "Rows", n))
            value = &params->fax.rows;
        else if (n == 22 && !memcmp(name, "DamagedRowsBeforeError", n))
            value = &params->fax.damaged_rows;
        else if (n == 9 && !memcmp(name, "EndOfLine", n)) {
            value = &params->fax.end_of_line;
            boolean = 1;
        } else if (n == 16 && !memcmp(name, "EncodedByteAlign", n)) {
            value = &params->fax.byte_align;
            boolean = 1;
        } else if (n == 10 && !memcmp(name, "EndOfBlock", n)) {
            value = &params->fax.end_of_block;
            boolean = 1;
        } else if (n == 8 && !memcmp(name, "BlackIs1", n)) {
            value = &params->fax.black_is_one;
            boolean = 1;
        }
        if (!value)
            return 0;
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
        if (pdf_filter_null(view, pos))
            continue;
        pdf_object_id reference;
        if (cupidimage_pdf_page_reference(view, *pos, &reference))
            return 0;
        if (boolean) {
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
            const char *word =
                view->len - *pos >= 4 && !memcmp(view->data + *pos, "true", 4) ? "true" : "false";
            size_t length = strlen(word);
            if (view->len - *pos < length || memcmp(view->data + *pos, word, length))
                return 0;
            *pos += length;
            if (*pos < view->len && !cupidimage_pdf_is_ws(view->data[*pos]) &&
                !cupidimage_pdf_is_delim(view->data[*pos]))
                return 0;
            *value = length == 4;
            continue;
        }
        double number;
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
        if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &number) ||
            number < (value == &params->fax.k ? INT_MIN : 0) || number > INT_MAX ||
            floor(number) != number ||
            (*pos < view->len && !cupidimage_pdf_is_ws(view->data[*pos]) &&
             !cupidimage_pdf_is_delim(view->data[*pos])))
            return 0;
        *value = (int)number;
        if (value == &params->columns)
            params->fax.columns = (int)number;
    }
    return 1;
}

int cupidimage_pdf_stream_filter_names(const pdf_object_view *view, char filters[8][32],
                                       int *total) {
    int count = 0;
    size_t pos;
    if (cupidimage_pdf_page_value(view, "Filter", &pos) && !pdf_filter_null(view, &pos)) {
        int array = pos < view->len && view->data[pos] == '[';
        if (array)
            pos++;
        for (;;) {
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
            if (array && pos < view->len && view->data[pos] == ']') {
                pos++;
                break;
            }
            char name[128];
            if (count == 8 || !cupidimage_pdf_read_name(view, &pos, name) ||
                strlen(name) >= sizeof(filters[0]))
                return 0;
            strcpy(filters[count++], name);
            if (!array)
                break;
        }
    }
    *total = count;
    return 1;
}

static int pdf_decode_filters(const pdf_object_view *view, const unsigned char *data, size_t length,
                              unsigned char **decoded, size_t *decoded_length,
                              const pdf_image_stream_profile *profile) {
    char filters[8][32];
    pdf_filter_params params[8];
    for (int i = 0; i < 8; i++)
        params[i] = (pdf_filter_params){1, 1, 1, 8, 1, -1, {0, 1728, 0, 0, 0, 1, 0, 0}};
    int count = 0;
    size_t pos;
    *decoded = NULL;
    *decoded_length = 0;
    if (!cupidimage_pdf_stream_filter_names(view, filters, &count))
        return 0;
    if (cupidimage_pdf_page_value(view, "DecodeParms", &pos) && !pdf_filter_null(view, &pos)) {
        int array = pos < view->len && view->data[pos] == '[';
        if (array)
            pos++;
        else if (count != 1)
            return 0;
        for (int i = 0; i < count; i++) {
            if (!pdf_filter_parameters(view, &pos, &params[i], filters[i]))
                return 0;
        }
        if (array) {
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
            if (pos == view->len || view->data[pos] != ']')
                return 0;
        }
    }
    unsigned char *owned = NULL;
    for (int i = 0; i < count; i++) {
        pdf_filter_buffer out = {0};
        const char *name = filters[i];
        int ok = 0;
        if (!strcmp(name, "FlateDecode") || !strcmp(name, "Fl"))
            ok = cupidimage_pdf_inflate_auto(data, length, &out.data, &out.used);
        else if (!strcmp(name, "LZWDecode") || !strcmp(name, "LZW"))
            ok = (params[i].early == 0 || params[i].early == 1) &&
                 pdf_filter_lzw(data, length, params[i].early, &out);
        else if (!strcmp(name, "ASCIIHexDecode") || !strcmp(name, "AHx"))
            ok = pdf_filter_hex(data, length, &out);
        else if (!strcmp(name, "ASCII85Decode") || !strcmp(name, "A85"))
            ok = pdf_filter_ascii85(data, length, &out);
        else if (!strcmp(name, "CCITTFaxDecode") || !strcmp(name, "CCF"))
            ok = cupidimage_ccitt_decode(data, length, &params[i].fax, &out.data, &out.used);
        else if (!strcmp(name, "RunLengthDecode") || !strcmp(name, "RL"))
            ok = pdf_filter_runlength(data, length, &out);
        else if (profile && (!strcmp(name, "DCTDecode") || !strcmp(name, "DCT"))) {
            ok = profile->bits == 8 &&
                 cupidimage_jpeg_decode_samples(data, length, profile->width, profile->height,
                                                profile->components, params[i].color_transform,
                                                &out.data);
            if (ok)
                out.used = (size_t)profile->width * profile->height * profile->components;
        }
        if (ok && (!strcmp(name, "FlateDecode") || !strcmp(name, "Fl") ||
                   !strcmp(name, "LZWDecode") || !strcmp(name, "LZW")))
            ok = pdf_filter_predict(&out, &params[i]);
        free(owned);
        if (!ok) {
            free(out.data);
            return 0;
        }
        owned = out.data;
        data = owned;
        length = out.used;
    }
    if (!count) {
        if (length > PDF_FILTER_LIMIT)
            return 0;
        owned = (unsigned char *)malloc(length ? length : 1);
        if (!owned)
            return 0;
        if (length)
            memcpy(owned, data, length);
    }
    *decoded = owned;
    *decoded_length = length;
    return 1;
}

int cupidimage_pdf_decode_direct_stream(const pdf_object_view *view, const unsigned char *data,
                                        size_t length, unsigned char **decoded,
                                        size_t *decoded_length) {
    return pdf_decode_filters(view, data, length, decoded, decoded_length, NULL);
}

int cupidimage_pdf_decode_direct_image_stream(const pdf_object_view *view,
                                              const unsigned char *data, size_t length,
                                              unsigned char **decoded, size_t *decoded_length,
                                              const pdf_image_stream_profile *profile) {
    return pdf_decode_filters(view, data, length, decoded, decoded_length, profile);
}
