#include "pdf_internal.h"
#include "fonts/encoding.h"
#include <math.h>
#include <string.h>

static int pdf_font_resolve(const unsigned char *data, size_t size, const pdf_object_view *view,
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
            int ok = pdf_font_resolve(data, size, &value, at, out, depth + 1);
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
int cupidimage_pdf_font_value(const unsigned char *data, size_t size, const pdf_object_view *view,
                        const char *key, pdf_object_view *out) {
    size_t p;
    return cupidimage_pdf_page_value(view, key, &p) && pdf_font_resolve(data, size, view, p, out, 0);
}
int cupidimage_pdf_font_name(const pdf_object_view *v, size_t *p, char name[128]) {
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
int cupidimage_pdf_font_is_name(const unsigned char *data, size_t size,
                                const pdf_object_view *view, const char *key, const char *expected) {
    pdf_object_view value = {0};
    size_t pos = 0;
    char name[128];
    int ok = cupidimage_pdf_font_value(data, size, view, key, &value) &&
             cupidimage_pdf_font_name(&value, &pos, name) && !strcmp(name, expected);
    cupidimage_pdf_object_view_release(&value);
    return ok;
}
int cupidimage_pdf_simple_encoding(const unsigned char *data, size_t size,
                            const pdf_object_view *view, unsigned glyphs[256],
                            const void *context, unsigned (*lookup)(const void *, const char *)) {
    pdf_object_view encoding = {0}, differences = {0};
    size_t pos;
    if (!cupidimage_pdf_page_value(view, "Encoding", &pos))
        return 1;
    if (!cupidimage_pdf_font_value(data, size, view, "Encoding", &encoding))
        return 0;
    char base[128] = {0};
    int ok = 1;
    pos = 0;
    cupidimage_pdf_skip_ws_and_comments(encoding.data, encoding.len, &pos);
    int dictionary = encoding.len - pos >= 2 && encoding.data[pos] == '<' && encoding.data[pos + 1] == '<';
    if (pos < encoding.len && encoding.data[pos] == '/')
        ok = cupidimage_pdf_font_name(&encoding, &pos, base);
    else if (!dictionary) {
        ok = encoding.len - pos >= 4 && !memcmp(encoding.data + pos, "null", 4) &&
             (encoding.len - pos == 4 || cupidimage_pdf_is_ws(encoding.data[pos + 4]) ||
              cupidimage_pdf_is_delim(encoding.data[pos + 4]));
    } else if (cupidimage_pdf_page_value(&encoding, "BaseEncoding", &pos)) {
        pdf_object_view value = {0};
        size_t at = 0;
        ok = cupidimage_pdf_font_value(data, size, &encoding, "BaseEncoding", &value) &&
             cupidimage_pdf_font_name(&value, &at, base);
        cupidimage_pdf_object_view_release(&value);
    }
    if (*base) {
        if (strcmp(base, "StandardEncoding") && strcmp(base, "WinAnsiEncoding") &&
            strcmp(base, "MacRomanEncoding") && strcmp(base, "MacExpertEncoding"))
            ok = 0;
        for (unsigned i = 0; ok && i < 256; i++)
            glyphs[i] = lookup(context, cupidimage_pdf_encoding_name(base, i));
    }
    if (ok && dictionary && cupidimage_pdf_page_value(&encoding, "Differences", &pos)) {
        ok = cupidimage_pdf_font_value(data, size, &encoding, "Differences", &differences);
        pos = 0;
        cupidimage_pdf_skip_ws_and_comments(differences.data, differences.len, &pos);
        ok = ok && pos < differences.len && differences.data[pos++] == '[';
        int code = -1;
        unsigned entries = 0;
        while (ok) {
            cupidimage_pdf_skip_ws_and_comments(differences.data, differences.len, &pos);
            if (pos < differences.len && differences.data[pos] == ']')
                break;
            if (++entries > 1024) { ok = 0; break; }
            if (pos < differences.len && differences.data[pos] == '/') {
                char name[128];
                ok = code >= 0 && code < 256 && cupidimage_pdf_font_name(&differences, &pos, name);
                if (ok) glyphs[code++] = lookup(context, name);
            } else {
                double value;
                ok = cupidimage_pdf_parse_pdf_number(differences.data, differences.len, &pos,
                                                     &value) && value >= 0 && value <= 255 &&
                     floor(value) == value;
                if (ok) code = (int)value;
            }
        }
    }
    cupidimage_pdf_object_view_release(&differences);
    cupidimage_pdf_object_view_release(&encoding);
    return ok;
}

static int pdf_font_reference_end(const pdf_object_view *view, size_t *pos) {
    double ignored;
    if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &ignored)) return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &ignored)) return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    return *pos < view->len && view->data[(*pos)++] == 'R';
}
int cupidimage_pdf_graphics_font(const unsigned char *data, size_t size,
                                const pdf_object_view *state, int *object, double *font_size) {
    pdf_object_view array = {0}, scalar = {0};
    size_t pos = 0, at = 0;
    int ok = cupidimage_pdf_font_value(data, size, state, "Font", &array);
    cupidimage_pdf_skip_ws_and_comments(array.data, array.len, &pos);
    if (ok && array.len - pos >= 4 && !memcmp(array.data + pos, "null", 4) &&
        (array.len - pos == 4 || cupidimage_pdf_is_ws(array.data[pos + 4]) ||
         cupidimage_pdf_is_delim(array.data[pos + 4]))) {
        *object = 0;
        *font_size = 0;
        cupidimage_pdf_object_view_release(&array);
        return 1;
    }
    ok = ok && pos < array.len && array.data[pos++] == '[';
    cupidimage_pdf_skip_ws_and_comments(array.data, array.len, &pos);
    ok = ok && cupidimage_pdf_page_reference(&array, pos, object) && *object > 0 &&
         pdf_font_reference_end(&array, &pos);
    cupidimage_pdf_skip_ws_and_comments(array.data, array.len, &pos);
    if (ok) ok = pdf_font_resolve(data, size, &array, pos, &scalar, 0);
    cupidimage_pdf_skip_ws_and_comments(scalar.data, scalar.len, &at);
    ok = ok && cupidimage_pdf_parse_pdf_number(scalar.data, scalar.len, &at, font_size) &&
         isfinite(*font_size) && fabs(*font_size) <= 1e30 &&
         (at == scalar.len || cupidimage_pdf_is_ws(scalar.data[at]) || cupidimage_pdf_is_delim(scalar.data[at]));
    if (ok) {
        int reference;
        double ignored;
        if (cupidimage_pdf_page_reference(&array, pos, &reference))
            ok = pdf_font_reference_end(&array, &pos);
        else ok = cupidimage_pdf_parse_pdf_number(array.data, array.len, &pos, &ignored);
        cupidimage_pdf_skip_ws_and_comments(array.data, array.len, &pos);
        ok = ok && pos < array.len && array.data[pos] == ']';
    }
    cupidimage_pdf_object_view_release(&scalar);
    cupidimage_pdf_object_view_release(&array);
    return ok;
}
