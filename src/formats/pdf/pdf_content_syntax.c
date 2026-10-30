#include "pdf_content_syntax.h"
#include "pdf_internal.h"
#include <stdlib.h>
#include <string.h>

#define CONTENT_TOKEN_LIMIT (1024u * 1024u)

typedef struct {
    char *data;
    size_t length, capacity;
    int keep;
} content_bytes;

static int content_put(content_bytes *bytes, unsigned char c) {
    if (bytes->length == CONTENT_TOKEN_LIMIT)
        return 0;
    if (bytes->keep) {
        if (bytes->length + 1 >= bytes->capacity) {
            size_t capacity = bytes->capacity ? bytes->capacity * 2 : 64;
            if (capacity > CONTENT_TOKEN_LIMIT + 1u)
                capacity = CONTENT_TOKEN_LIMIT + 1u;
            char *data = realloc(bytes->data, capacity);
            if (!data)
                return 0;
            bytes->data = data;
            bytes->capacity = capacity;
        }
        bytes->data[bytes->length] = (char)c;
    }
    bytes->length++;
    return 1;
}

static int content_finish(content_bytes *bytes, char **out, size_t *length) {
    if (!bytes->keep)
        return 1;
    if (!bytes->data) {
        bytes->data = malloc(1);
        if (!bytes->data)
            return 0;
    }
    bytes->data[bytes->length] = 0;
    *out = bytes->data;
    *length = bytes->length;
    return 1;
}

static int content_hex_digit(unsigned char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}

int cupidimage_pdf_content_number(const unsigned char *data, size_t size, size_t *pos,
                                  double *out) {
    size_t p = *pos;
    if (!cupidimage_pdf_parse_pdf_number(data, size, &p, out) ||
        (p < size && !cupidimage_pdf_is_ws(data[p]) && !cupidimage_pdf_is_delim(data[p])))
        return 0;
    *pos = p;
    return 1;
}

int cupidimage_pdf_content_name(const unsigned char *data, size_t size, size_t *pos, char **out,
                                size_t *length) {
    size_t p = *pos;
    content_bytes bytes = {NULL, 0, 0, out != NULL};
    if (p >= size || data[p++] != '/')
        return 0;
    while (p < size && !cupidimage_pdf_is_ws(data[p]) && !cupidimage_pdf_is_delim(data[p])) {
        unsigned char c = data[p++];
        if (c == '#') {
            if (size - p < 2)
                goto fail;
            int high = content_hex_digit(data[p]), low = content_hex_digit(data[p + 1]);
            if (high < 0 || low < 0)
                goto fail;
            c = (unsigned char)(high * 16 + low);
            if (!c)
                goto fail;
            p += 2;
        }
        if (!content_put(&bytes, c))
            goto fail;
    }
    if (!content_finish(&bytes, out, length))
        goto fail;
    *pos = p;
    return 1;
fail:
    free(bytes.data);
    return 0;
}

int cupidimage_pdf_content_literal(const unsigned char *data, size_t size, size_t *pos, char **out,
                                   size_t *length) {
    size_t p = *pos;
    unsigned depth = 1;
    content_bytes bytes = {NULL, 0, 0, out != NULL};
    if (p >= size || data[p++] != '(')
        return 0;
    while (p < size) {
        unsigned char c = data[p++];
        if (c == '\\') {
            if (p == size)
                goto fail;
            c = data[p++];
            if (c == '\r' || c == '\n') {
                if (c == '\r' && p < size && data[p] == '\n')
                    p++;
                continue;
            }
            if (c >= '0' && c <= '7') {
                unsigned value = c - '0';
                for (unsigned i = 0; i < 2 && p < size && data[p] >= '0' && data[p] <= '7'; i++)
                    value = value * 8 + data[p++] - '0';
                c = (unsigned char)value;
            } else if (c == 'n')
                c = '\n';
            else if (c == 'r')
                c = '\r';
            else if (c == 't')
                c = '\t';
            else if (c == 'b')
                c = '\b';
            else if (c == 'f')
                c = '\f';
        } else if (c == '(') {
            if (++depth > 64)
                goto fail;
        } else if (c == ')') {
            if (--depth == 0) {
                if (!content_finish(&bytes, out, length))
                    goto fail;
                *pos = p;
                return 1;
            }
        } else if (c == '\r') {
            if (p < size && data[p] == '\n')
                p++;
            c = '\n';
        }
        if (!content_put(&bytes, c))
            goto fail;
    }
fail:
    free(bytes.data);
    return 0;
}

int cupidimage_pdf_content_hex(const unsigned char *data, size_t size, size_t *pos, char **out,
                               size_t *length) {
    size_t p = *pos;
    int high = -1;
    content_bytes bytes = {NULL, 0, 0, out != NULL};
    if (p >= size || data[p++] != '<' || (p < size && data[p] == '<'))
        return 0;
    while (p < size) {
        unsigned char c = data[p++];
        if (c == '>') {
            if (high >= 0 && !content_put(&bytes, (unsigned char)(high * 16)))
                goto fail;
            if (!content_finish(&bytes, out, length))
                goto fail;
            *pos = p;
            return 1;
        }
        if (cupidimage_pdf_is_ws(c))
            continue;
        int digit = content_hex_digit(c);
        if (digit < 0)
            goto fail;
        if (high < 0)
            high = digit;
        else {
            if (!content_put(&bytes, (unsigned char)(high * 16 + digit)))
                goto fail;
            high = -1;
        }
    }
fail:
    free(bytes.data);
    return 0;
}

/* Preserve aggregate operands without interpreting their contained operators. */
static int content_aggregate(const unsigned char *data, size_t size, size_t *pos, char **out,
                             size_t *length, int dictionary) {
    size_t start = *pos, p = start;
    unsigned char stack[64];
    unsigned depth = 1;
    if (p >= size)
        return 0;
    if (dictionary) {
        if (size - p < 2 || data[p] != '<' || data[p + 1] != '<')
            return 0;
        p += 2;
        stack[0] = '>';
    } else {
        if (data[p++] != '[')
            return 0;
        stack[0] = ']';
    }
    while (p < size && depth) {
        if (p - start > CONTENT_TOKEN_LIMIT)
            return 0;
        unsigned char c = data[p];
        if (c == '%') {
            while (p < size && data[p] != '\r' && data[p] != '\n')
                p++;
        } else if (c == '(') {
            if (!cupidimage_pdf_content_literal(data, size, &p, NULL, NULL))
                return 0;
        } else if (c == '/') {
            if (!cupidimage_pdf_content_name(data, size, &p, NULL, NULL))
                return 0;
        } else if (c == '<') {
            if (p + 1 < size && data[p + 1] == '<') {
                if (depth == sizeof(stack))
                    return 0;
                stack[depth++] = '>';
                p += 2;
            } else if (!cupidimage_pdf_content_hex(data, size, &p, NULL, NULL))
                return 0;
        } else if (c == '[') {
            if (depth == sizeof(stack))
                return 0;
            stack[depth++] = ']';
            p++;
        } else if (c == ']') {
            if (stack[depth - 1] != ']')
                return 0;
            depth--;
            p++;
        } else if (c == '>') {
            if (stack[depth - 1] != '>' || size - p < 2 || data[p + 1] != '>')
                return 0;
            depth--;
            p += 2;
        } else if (c == ')')
            return 0;
        else
            p++;
    }
    if (depth || p - start > CONTENT_TOKEN_LIMIT)
        return 0;
    size_t from = start + (dictionary ? 0 : 1), bytes = p - from - (dictionary ? 0 : 1);
    if (out) {
        char *copy = malloc(bytes + 1);
        if (!copy)
            return 0;
        memcpy(copy, data + from, bytes);
        copy[bytes] = 0;
        *out = copy;
    }
    *pos = p;
    if (length)
        *length = bytes;
    return 1;
}

int cupidimage_pdf_content_array(const unsigned char *data, size_t size, size_t *pos, char **out,
                                 size_t *length) {
    return content_aggregate(data, size, pos, out, length, 0);
}

int cupidimage_pdf_content_dictionary(const unsigned char *data, size_t size, size_t *pos,
                                      char **out, size_t *length) {
    return content_aggregate(data, size, pos, out, length, 1);
}
