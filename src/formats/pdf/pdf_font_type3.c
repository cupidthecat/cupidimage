#include "pdf_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int type3_value(const unsigned char *data, size_t size, const pdf_object_view *parent,
                       const char *name, pdf_object_view *value) {
    size_t pos;
    int object;
    if (!cupidimage_pdf_page_value(parent, name, &pos))
        return 0;
    if (cupidimage_pdf_page_reference(parent, pos, &object))
        return cupidimage_pdf_get_object_content(data, size, object, value);
    value->data = parent->data + pos;
    value->len = parent->len - pos;
    return 1;
}
static int type3_array(const unsigned char *data, size_t size, const pdf_object_view *view,
                       const char *name, double *numbers, int count) {
    pdf_object_view array = {0};
    size_t pos = 0;
    int ok = type3_value(data, size, view, name, &array);
    cupidimage_pdf_skip_ws_and_comments(array.data, array.len, &pos);
    ok = ok && pos < array.len && array.data[pos++] == '[';
    for (int i = 0; ok && i < count; i++) {
        cupidimage_pdf_skip_ws_and_comments(array.data, array.len, &pos);
        ok = cupidimage_pdf_parse_pdf_number(array.data, array.len, &pos, &numbers[i]) &&
             isfinite(numbers[i]) && fabs(numbers[i]) <= 1e30;
    }
    cupidimage_pdf_skip_ws_and_comments(array.data, array.len, &pos);
    ok = ok && pos < array.len && array.data[pos] == ']';
    cupidimage_pdf_object_view_release(&array);
    return ok;
}
static int type3_hex(unsigned char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}
static int type3_name(const pdf_object_view *view, size_t *pos, char name[128]) {
    if (*pos == view->len || view->data[(*pos)++] != '/')
        return 0;
    size_t used = 0;
    while (*pos < view->len && !cupidimage_pdf_is_ws(view->data[*pos]) &&
           !cupidimage_pdf_is_delim(view->data[*pos])) {
        unsigned char c = view->data[(*pos)++];
        if (c == '#') {
            if (view->len - *pos < 2)
                return 0;
            int a = type3_hex(view->data[*pos]), b = type3_hex(view->data[*pos + 1]);
            if (a < 0 || b < 0)
                return 0;
            c = (unsigned char)(16 * a + b);
            *pos += 2;
        }
        if (!c || used == 127)
            return 0;
        name[used++] = (char)c;
    }
    name[used] = 0;
    return used != 0;
}
int cupidimage_pdf_parse_type3_font(const unsigned char *data, size_t size,
                                    const pdf_object_view *view, pdf_font_map *font) {
    size_t pos;
    if (!cupidimage_pdf_page_value(view, "Subtype", &pos))
        return 1;
    char subtype[128];
    if (!type3_name(view, &pos, subtype))
        return 0;
    if (strcmp(subtype, "Type3"))
        return 1;
    if (view->len > 65536)
        return 0;
    pdf_type3_font *type = calloc(1, sizeof(*type));
    pdf_object_view encoding = {0}, differences = {0}, procs = {0};
    int ok = 0;
    if (!type || !type3_array(data, size, view, "FontMatrix", type->matrix, 6) ||
        !type3_array(data, size, view, "FontBBox", type->bounds, 4) ||
        !type3_value(data, size, view, "Encoding", &encoding) ||
        !type3_value(data, size, &encoding, "Differences", &differences) ||
        !type3_value(data, size, view, "CharProcs", &procs))
        goto done;
    if (type->bounds[0] > type->bounds[2] || type->bounds[1] > type->bounds[3])
        goto done;
    pos = 0;
    cupidimage_pdf_skip_ws_and_comments(procs.data, procs.len, &pos);
    if (procs.len - pos < 2 || procs.data[pos] != '<' || procs.data[pos + 1] != '<')
        goto done;
    pos = 0;
    cupidimage_pdf_skip_ws_and_comments(differences.data, differences.len, &pos);
    if (pos == differences.len || differences.data[pos++] != '[')
        goto done;
    int code = -1, entries = 0;
    for (;;) {
        cupidimage_pdf_skip_ws_and_comments(differences.data, differences.len, &pos);
        if (pos == differences.len)
            goto done;
        if (differences.data[pos] == ']')
            break;
        if (++entries > 1024)
            goto done;
        if (differences.data[pos] == '/') {
            char name[128];
            size_t found;
            int object;
            if (code < 0 || code > 255 || !type3_name(&differences, &pos, name))
                goto done;
            if (cupidimage_pdf_page_value(&procs, name, &found)) {
                if (!cupidimage_pdf_page_reference(&procs, found, &object))
                    goto done;
                type->glyphs[code] = object;
            }
            code++;
        } else {
            double value;
            if (!cupidimage_pdf_parse_pdf_number(differences.data, differences.len, &pos, &value) ||
                value < 0 || value > 255 || floor(value) != value)
                goto done;
            code = (int)value;
        }
    }
    type->dictionary.owned = malloc(view->len ? view->len : 1);
    if (!type->dictionary.owned)
        goto done;
    memcpy(type->dictionary.owned, view->data, view->len);
    type->dictionary.data = type->dictionary.owned;
    type->dictionary.len = view->len;
    font->type3 = type;
    type = NULL;
    ok = 1;
done:
    free(type);
    cupidimage_pdf_object_view_release(&encoding);
    cupidimage_pdf_object_view_release(&differences);
    cupidimage_pdf_object_view_release(&procs);
    return ok;
}
