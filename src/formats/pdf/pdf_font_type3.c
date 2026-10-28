#include "pdf_internal.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int type3_array(const unsigned char *data, size_t size, const pdf_object_view *view,
                       const char *name, double *numbers, int count) {
    pdf_object_view array = {0};
    size_t pos = 0;
    int ok = cupidimage_pdf_dictionary_value(data, size, view, name, &array);
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

static unsigned type3_named_glyph(const void *context, const char *name) {
    const pdf_object_view *procs = context;
    size_t pos;
    int object;
    if (!cupidimage_pdf_page_value(procs, name, &pos))
        return 0;
    return cupidimage_pdf_page_reference(procs, pos, &object) && object > 0 ? (unsigned)object
                                                                            : UINT_MAX;
}

int cupidimage_pdf_parse_type3_font(const unsigned char *data, size_t size,
                                    const pdf_object_view *view, pdf_font_map *font) {
    size_t pos;
    if (!cupidimage_pdf_page_value(view, "Subtype", &pos))
        return 1;
    pdf_object_view subtype_value = {0};
    char subtype[128];
    pos = 0;
    int valid = cupidimage_pdf_dictionary_value(data, size, view, "Subtype", &subtype_value) &&
                cupidimage_pdf_read_name(&subtype_value, &pos, subtype);
    cupidimage_pdf_object_view_release(&subtype_value);
    if (!valid)
        return 0;
    if (strcmp(subtype, "Type3"))
        return 1;
    if (view->len > 65536)
        return 0;
    pdf_type3_font *type = calloc(1, sizeof(*type));
    pdf_object_view encoding = {0}, procs = {0};
    int ok = 0;
    if (!type || !type3_array(data, size, view, "FontMatrix", type->matrix, 6) ||
        !type3_array(data, size, view, "FontBBox", type->bounds, 4) ||
        !cupidimage_pdf_dictionary_value(data, size, view, "Encoding", &encoding) ||
        !cupidimage_pdf_dictionary_value(data, size, view, "CharProcs", &procs))
        goto done;
    if (type->bounds[0] > type->bounds[2] || type->bounds[1] > type->bounds[3])
        goto done;
    pos = 0;
    cupidimage_pdf_skip_ws_and_comments(procs.data, procs.len, &pos);
    if (procs.len - pos < 2 || procs.data[pos] != '<' || procs.data[pos + 1] != '<')
        goto done;
    pos = 0;
    cupidimage_pdf_skip_ws_and_comments(encoding.data, encoding.len, &pos);
    if (pos == encoding.len || (encoding.data[pos] != '/' &&
                                (encoding.len - pos < 2 || memcmp(encoding.data + pos, "<<", 2))))
        goto done;
    unsigned glyphs[256] = {0};
    if (!cupidimage_pdf_simple_encoding(data, size, view, glyphs, &procs, type3_named_glyph))
        goto done;
    for (unsigned i = 0; i < 256; i++) {
        if (glyphs[i] > INT_MAX)
            goto done;
        type->glyphs[i] = (int)glyphs[i];
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
    cupidimage_pdf_object_view_release(&procs);
    return ok;
}
