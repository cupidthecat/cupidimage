#include "../pdf_content_syntax.h"
#include "security.h"
#include <stdlib.h>
#include <string.h>

#define SECURITY_OBJECT_LIMIT (64u * 1024u * 1024u)

typedef struct {
    const pdf_object_view *view;
    unsigned char *output;
    size_t length, capacity, copied;
    size_t *work;
} security_string_buffer;

static int security_append(security_string_buffer *buffer, const void *data, size_t length) {
    if (length > SECURITY_OBJECT_LIMIT - buffer->length || length > *buffer->work)
        return 0;
    size_t needed = buffer->length + length;
    if (needed > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity : 256;
        while (capacity < needed)
            capacity *= 2;
        unsigned char *next = realloc(buffer->output, capacity);
        if (!next)
            return 0;
        buffer->output = next;
        buffer->capacity = capacity;
    }
    if (length)
        memcpy(buffer->output + buffer->length, data, length);
    buffer->length += length;
    *buffer->work -= length;
    return 1;
}

static int security_direct_type(const pdf_object_view *view, const char *name) {
    size_t at;
    char type[128];
    return cupidimage_pdf_page_value(view, "Type", &at) &&
           cupidimage_pdf_read_name(view, &at, type) && !strcmp(type, name);
}

static int security_signature(const pdf_object_view *dictionary) {
    size_t at;
    if (cupidimage_pdf_page_value(dictionary, "Type", &at))
        return security_direct_type(dictionary, "Sig") ||
               security_direct_type(dictionary, "DocTimeStamp");
    /* Byte-range signatures may omit Type, whose default is Sig. Their values
     * must be direct objects, so this identification cannot recurse into itself. */
    return cupidimage_pdf_page_value(dictionary, "ByteRange", &at) && at < dictionary->len &&
           dictionary->data[at] == '[';
}

static int security_string(security_string_buffer *buffer, size_t *at, int exempt) {
    const pdf_object_view *view = buffer->view;
    size_t start = *at, length = 0;
    char *raw = NULL;
    int hex = view->data[start] == '<';
    int ok = hex ? cupidimage_pdf_content_hex(view->data, view->len, at, &raw, &length)
                 : cupidimage_pdf_content_literal(view->data, view->len, at, &raw, &length);
    unsigned char *decoded = NULL;
    size_t decoded_length = 0;
    if (!ok || (exempt && hex)) {
        free(raw);
        return ok;
    }
    const pdf_security *security = view->xref->security;
    ok = length <= *buffer->work &&
         cupidimage_pdf_security_decrypt(security, view->identity, security->strings,
                                         (unsigned char *)raw, length, &decoded, &decoded_length);
    if (length <= *buffer->work)
        *buffer->work -= length;
    free(raw);
    static const char digits[] = "0123456789abcdef";
    if (ok)
        ok = security_append(buffer, view->data + buffer->copied, start - buffer->copied) &&
             security_append(buffer, "<", 1);
    for (size_t i = 0; ok && i < decoded_length; i++) {
        char pair[] = {digits[decoded[i] >> 4], digits[decoded[i] & 15]};
        ok = security_append(buffer, pair, 2);
    }
    if (ok)
        ok = security_append(buffer, ">", 1);
    if (decoded)
        cupidimage_pdf_crypto_erase(decoded, decoded_length);
    free(decoded);
    buffer->copied = *at;
    return ok;
}

static int security_value(security_string_buffer *buffer, size_t *at, unsigned depth, int exempt) {
    const pdf_object_view *view = buffer->view;
    if (depth >= 64 || !*buffer->work)
        return 0;
    --*buffer->work;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, at);
    if (*at == view->len)
        return 0;
    unsigned char c = view->data[*at];
    if (c == '(' || (c == '<' && (view->len - *at < 2 || view->data[*at + 1] != '<')))
        return security_string(buffer, at, exempt);
    if (c == '[') {
        (*at)++;
        for (;;) {
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, at);
            if (*at < view->len && view->data[*at] == ']') {
                (*at)++;
                return 1;
            }
            if (!security_value(buffer, at, depth + 1, 0))
                return 0;
        }
    }
    if (c == '<') {
        pdf_object_view dictionary = {0};
        size_t end;
        if (!cupidimage_pdf_dictionary_view(view, *at, &dictionary, &end))
            return 0;
        int signature = security_signature(&dictionary);
        *at += 2;
        for (;;) {
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, at);
            if (view->len - *at >= 2 && !memcmp(view->data + *at, ">>", 2)) {
                *at += 2;
                return *at == end;
            }
            char name[128];
            if (!cupidimage_pdf_read_name(view, at, name) ||
                !security_value(buffer, at, depth + 1, signature && !strcmp(name, "Contents")))
                return 0;
        }
    }
    if (c == '/') {
        char name[128];
        return cupidimage_pdf_read_name(view, at, name);
    }
    pdf_object_id reference;
    if (cupidimage_pdf_parse_reference(view, at, &reference))
        return 1;
    size_t start = *at;
    while (*at < view->len && !cupidimage_pdf_is_ws(view->data[*at]) &&
           !cupidimage_pdf_is_delim(view->data[*at]))
        (*at)++;
    return *at != start;
}

int cupidimage_pdf_security_strings(pdf_object_view *view, size_t *work) {
    const pdf_security *security = view->xref ? view->xref->security : NULL;
    if (!security || security->strings == PDF_CRYPT_IDENTITY ||
        view->identity == security->encryption_object || security_direct_type(view, "XRef"))
        return 1;
    if (view->len > SECURITY_OBJECT_LIMIT)
        return 0;
    security_string_buffer buffer = {view, NULL, 0, 0, 0, work};
    size_t at = 0;
    int ok = security_value(&buffer, &at, 0, 0);
    if (ok && buffer.output)
        ok = security_append(&buffer, view->data + buffer.copied, view->len - buffer.copied);
    if (!ok) {
        if (buffer.output)
            cupidimage_pdf_crypto_erase(buffer.output, buffer.length);
        free(buffer.output);
        return 0;
    }
    if (buffer.output) {
        free(view->owned);
        view->data = view->owned = buffer.output;
        view->len = buffer.length;
    }
    return 1;
}
