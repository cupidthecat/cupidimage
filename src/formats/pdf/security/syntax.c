#include "syntax.h"
#include "../pdf_content_syntax.h"
#include <stdlib.h>
#include <string.h>

static int security_syntax_value(const pdf_object_view *view, size_t *at, unsigned depth) {
    if (depth >= 32)
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, at);
    if (*at == view->len)
        return 0;
    unsigned char c = view->data[*at];
    if (c == '(' || (c == '<' && (view->len - *at < 2 || view->data[*at + 1] != '<'))) {
        char *value = NULL;
        size_t length = 0;
        int ok = c == '('
                     ? cupidimage_pdf_content_literal(view->data, view->len, at, &value, &length)
                     : cupidimage_pdf_content_hex(view->data, view->len, at, &value, &length);
        free(value);
        return ok;
    }
    if (c == '[') {
        (*at)++;
        for (;;) {
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, at);
            if (*at < view->len && view->data[*at] == ']') {
                (*at)++;
                return 1;
            }
            if (!security_syntax_value(view, at, depth + 1))
                return 0;
        }
    }
    if (c == '<') {
        *at += 2;
        size_t keys[64];
        unsigned count = 0;
        for (;;) {
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, at);
            if (view->len - *at >= 2 && !memcmp(view->data + *at, ">>", 2)) {
                *at += 2;
                return 1;
            }
            if (count == 64)
                return 0;
            size_t key = *at;
            char name[128], previous[128];
            if (!cupidimage_pdf_read_name(view, at, name))
                return 0;
            for (unsigned i = 0; i < count; i++) {
                size_t offset = keys[i];
                if (!cupidimage_pdf_read_name(view, &offset, previous) || !strcmp(name, previous))
                    return 0;
            }
            keys[count++] = key;
            if (!security_syntax_value(view, at, depth + 1))
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
    const char *tokens[] = {"null", "true", "false"};
    for (unsigned i = 0; i < 3; i++) {
        size_t length = strlen(tokens[i]);
        if (length <= view->len - *at && !memcmp(view->data + *at, tokens[i], length) &&
            (*at + length == view->len || cupidimage_pdf_is_ws(view->data[*at + length]) ||
             cupidimage_pdf_is_delim(view->data[*at + length]))) {
            *at += length;
            return 1;
        }
    }
    double number;
    return cupidimage_pdf_parse_pdf_number(view->data, view->len, at, &number) &&
           (*at == view->len || cupidimage_pdf_is_ws(view->data[*at]) ||
            cupidimage_pdf_is_delim(view->data[*at]));
}

int cupidimage_pdf_security_dictionary_valid(const pdf_object_view *view) {
    size_t at = 0;
    if (!view || view->len > 65536 || !security_syntax_value(view, &at, 0))
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &at);
    return at == view->len;
}
