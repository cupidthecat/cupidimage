#include "pdf_internal.h"
#include <string.h>

int cupidimage_pdf_resolve_value(const unsigned char *data, size_t size, const pdf_object_view *view,
                          size_t p, pdf_object_view *out, unsigned depth) {
    if (depth == 16)
        return 0;
    int object;
    if (cupidimage_pdf_page_reference(view, p, &object)) {
        pdf_object_view value = {0};
        if (!cupidimage_pdf_get_object_content(data, size, object, &value))
            return 0;
        int next;
        size_t at = 0;
        cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &at);
        if (cupidimage_pdf_page_reference(&value, at, &next)) {
            int ok = cupidimage_pdf_resolve_value(data, size, &value, at, out, depth + 1);
            cupidimage_pdf_object_view_release(&value);
            return ok;
        }
        *out = value;
        return 1;
    }
    out->data = view->data + p;
    out->len = view->len - p;
    return 1;
}
int cupidimage_pdf_dictionary_value(const unsigned char *data, size_t size, const pdf_object_view *view,
                        const char *key, pdf_object_view *out) {
    size_t p;
    return cupidimage_pdf_page_value(view, key, &p) && cupidimage_pdf_resolve_value(data, size, view, p, out, 0);
}
int cupidimage_pdf_read_name(const pdf_object_view *v, size_t *p, char name[128]) {
    cupidimage_pdf_skip_ws_and_comments(v->data, v->len, p);
    if (*p == v->len || v->data[(*p)++] != '/')
        return 0;
    size_t count = 0;
    while (*p < v->len && !cupidimage_pdf_is_ws(v->data[*p]) &&
           !cupidimage_pdf_is_delim(v->data[*p])) {
        unsigned char c = v->data[(*p)++];
        if (c == '#') {
            if (v->len - *p < 2)
                return 0;
            unsigned value = 0;
            for (int i = 0; i < 2; i++) {
                c = v->data[(*p)++];
                int n = c >= '0' && c <= '9'   ? c - '0'
                        : c >= 'a' && c <= 'f' ? c - 'a' + 10
                        : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                               : -1;
                if (n < 0)
                    return 0;
                value = value * 16 + (unsigned)n;
            }
            c = (unsigned char)value;
        }
        if (!c || count == 127)
            return 0;
        name[count++] = (char)c;
    }
    name[count] = 0;
    return count != 0;
}
int cupidimage_pdf_entry_is_name(const unsigned char *data, size_t size,
                                const pdf_object_view *view, const char *key, const char *expected) {
    pdf_object_view value = {0};
    size_t pos = 0;
    char name[128];
    int ok = cupidimage_pdf_dictionary_value(data, size, view, key, &value) &&
             cupidimage_pdf_read_name(&value, &pos, name) && !strcmp(name, expected);
    cupidimage_pdf_object_view_release(&value);
    return ok;
}
