#include "fonts/encoding.h"
#include "pdf_internal.h"
#include <math.h>
#include <string.h>

int cupidimage_pdf_encoding_differences(const unsigned char *data, size_t size,
                                        const pdf_object_view *encoding, unsigned glyphs[256],
                                        const void *context,
                                        unsigned (*lookup)(const void *, const char *)) {
    pdf_object_view differences = {0};
    int present = cupidimage_pdf_optional_value(data, size, encoding, "Differences", &differences);
    if (present <= 0)
        return present == 0;
    size_t pos = 0;
    cupidimage_pdf_skip_ws_and_comments(differences.data, differences.len, &pos);
    int ok = pos < differences.len && differences.data[pos++] == '[';
    int code = -1;
    unsigned entries = 0;
    while (ok) {
        cupidimage_pdf_skip_ws_and_comments(differences.data, differences.len, &pos);
        if (pos < differences.len && differences.data[pos] == ']')
            break;
        if (++entries > 1024) {
            ok = 0;
            break;
        }
        size_t end = pos, at = 0;
        pdf_object_id object;
        int indirect = cupidimage_pdf_parse_reference(&differences, &end, &object);
        pdf_object_view value = {0};
        ok = cupidimage_pdf_resolve_value(data, size, &differences, pos, &value, 0);
        cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &at);
        if (ok && at < value.len && value.data[at] == '/') {
            char name[128];
            ok = code >= 0 && code < 256 && cupidimage_pdf_read_name(&value, &at, name);
            if (ok)
                glyphs[code++] = lookup(context, name);
        } else if (ok) {
            double number;
            size_t start = at;
            ok = cupidimage_pdf_parse_pdf_number(value.data, value.len, &at, &number) &&
                 number >= 0 && number <= 255 && floor(number) == number &&
                 (at == value.len || cupidimage_pdf_is_ws(value.data[at]) ||
                  cupidimage_pdf_is_delim(value.data[at]));
            for (size_t i = start; ok && i < at; i++)
                if (value.data[i] == '.')
                    ok = 0;
            if (ok)
                code = (int)number;
        }
        if (ok && indirect) {
            cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &at);
            ok = at == value.len;
        }
        if (ok)
            pos = indirect ? end : pos + at;
        cupidimage_pdf_object_view_release(&value);
    }
    cupidimage_pdf_object_view_release(&differences);
    return ok;
}

int cupidimage_pdf_simple_encoding(const unsigned char *data, size_t size,
                                   const pdf_object_view *view, unsigned glyphs[256],
                                   const void *context,
                                   unsigned (*lookup)(const void *, const char *)) {
    pdf_object_view encoding = {0};
    size_t pos;
    if (!cupidimage_pdf_page_value(view, "Encoding", &pos))
        return 1;
    if (!cupidimage_pdf_dictionary_value(data, size, view, "Encoding", &encoding))
        return 0;
    char base[128] = {0};
    int ok = 1, base_present = 0;
    pos = 0;
    cupidimage_pdf_skip_ws_and_comments(encoding.data, encoding.len, &pos);
    int dictionary =
        encoding.len - pos >= 2 && encoding.data[pos] == '<' && encoding.data[pos + 1] == '<';
    if (pos < encoding.len && encoding.data[pos] == '/') {
        base_present = 1;
        ok = cupidimage_pdf_read_name(&encoding, &pos, base);
    } else if (!dictionary) {
        ok = encoding.len - pos >= 4 && !memcmp(encoding.data + pos, "null", 4) &&
             (encoding.len - pos == 4 || cupidimage_pdf_is_ws(encoding.data[pos + 4]) ||
              cupidimage_pdf_is_delim(encoding.data[pos + 4]));
    } else if (cupidimage_pdf_page_value(&encoding, "BaseEncoding", &pos)) {
        pdf_object_view value = {0};
        size_t at = 0;
        int present = cupidimage_pdf_optional_value(data, size, &encoding, "BaseEncoding", &value);
        base_present = present > 0;
        ok = present == 0 || (present > 0 && cupidimage_pdf_read_name(&value, &at, base));
        cupidimage_pdf_object_view_release(&value);
    }
    if (base_present) {
        if (strcmp(base, "StandardEncoding") && strcmp(base, "WinAnsiEncoding") &&
            strcmp(base, "MacRomanEncoding") && strcmp(base, "MacExpertEncoding"))
            ok = 0;
        for (unsigned i = 0; ok && i < 256; i++)
            glyphs[i] = lookup(context, cupidimage_pdf_encoding_name(base, i));
    }
    if (ok && dictionary)
        ok = cupidimage_pdf_encoding_differences(data, size, &encoding, glyphs, context, lookup);
    cupidimage_pdf_object_view_release(&encoding);
    return ok;
}

static int pdf_font_reference_end(const pdf_object_view *view, size_t *pos) {
    double ignored;
    if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &ignored))
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &ignored))
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    return *pos < view->len && view->data[(*pos)++] == 'R';
}

int cupidimage_pdf_graphics_font(const unsigned char *data, size_t size,
                                 const pdf_object_view *state, pdf_object_id *object,
                                 double *font_size) {
    pdf_object_view array = {0}, scalar = {0};
    size_t pos = 0, at = 0;
    int ok = cupidimage_pdf_dictionary_value(data, size, state, "Font", &array);
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
    if (ok)
        ok = cupidimage_pdf_resolve_value(data, size, &array, pos, &scalar, 0);
    cupidimage_pdf_skip_ws_and_comments(scalar.data, scalar.len, &at);
    ok = ok && cupidimage_pdf_parse_pdf_number(scalar.data, scalar.len, &at, font_size) &&
         isfinite(*font_size) && fabs(*font_size) <= 1e30 &&
         (at == scalar.len || cupidimage_pdf_is_ws(scalar.data[at]) ||
          cupidimage_pdf_is_delim(scalar.data[at]));
    if (ok) {
        pdf_object_id reference;
        double ignored;
        if (cupidimage_pdf_page_reference(&array, pos, &reference))
            ok = pdf_font_reference_end(&array, &pos);
        else
            ok = cupidimage_pdf_parse_pdf_number(array.data, array.len, &pos, &ignored);
        cupidimage_pdf_skip_ws_and_comments(array.data, array.len, &pos);
        ok = ok && pos < array.len && array.data[pos] == ']';
    }
    cupidimage_pdf_object_view_release(&scalar);
    cupidimage_pdf_object_view_release(&array);
    return ok;
}
