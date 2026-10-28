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
        ok = cupidimage_pdf_resolved_number(data, size, &array, &pos, &numbers[i]) &&
             isfinite(numbers[i]) && fabs(numbers[i]) <= 1e30;
    }
    cupidimage_pdf_skip_ws_and_comments(array.data, array.len, &pos);
    ok = ok && pos < array.len && array.data[pos] == ']';
    cupidimage_pdf_object_view_release(&array);
    return ok;
}

/* A base encoding visits 256 names and Differences permits 1024 operands.
 * Encoding slots hold small indices, never truncated indirect-object identities. */
typedef struct {
    const pdf_object_view *procs;
    pdf_object_id objects[1280];
    unsigned count;
} type3_encoding_objects;

static unsigned type3_named_glyph(const void *context, const char *name) {
    type3_encoding_objects *encoding = (type3_encoding_objects *)context;
    const pdf_object_view *procs = encoding->procs;
    size_t pos;
    pdf_object_id object;
    if (!cupidimage_pdf_page_value(procs, name, &pos))
        return 0;
    if (procs->len - pos >= 4 && !memcmp(procs->data + pos, "null", 4) &&
        (procs->len - pos == 4 || cupidimage_pdf_is_ws(procs->data[pos + 4]) ||
         cupidimage_pdf_is_delim(procs->data[pos + 4])))
        return 0;
    if (!cupidimage_pdf_page_reference(procs, pos, &object))
        return UINT_MAX;
    if (!PDF_OBJECT_NUMBER(object) ||
        (procs->xref && !cupidimage_pdf_xref_find(procs->xref, object)))
        return 0;
    if (encoding->count == sizeof(encoding->objects) / sizeof(encoding->objects[0]))
        return UINT_MAX;
    encoding->objects[encoding->count++] = object;
    return encoding->count;
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
    type3_encoding_objects *references = NULL;
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
    references = calloc(1, sizeof(*references));
    if (!references)
        goto done;
    references->procs = &procs;
    if (!cupidimage_pdf_simple_encoding(data, size, view, glyphs, references, type3_named_glyph))
        goto done;
    for (unsigned i = 0; i < 256; i++) {
        if (glyphs[i] > references->count)
            goto done;
        type->glyphs[i] = glyphs[i] ? references->objects[glyphs[i] - 1] : 0;
    }
    type->dictionary.owned = malloc(view->len ? view->len : 1);
    if (!type->dictionary.owned)
        goto done;
    memcpy(type->dictionary.owned, view->data, view->len);
    type->dictionary.data = type->dictionary.owned;
    type->dictionary.len = view->len;
    type->dictionary.xref = view->xref;
    font->type3 = type;
    type = NULL;
    ok = 1;
done:
    free(references);
    free(type);
    cupidimage_pdf_object_view_release(&encoding);
    cupidimage_pdf_object_view_release(&procs);
    return ok;
}
