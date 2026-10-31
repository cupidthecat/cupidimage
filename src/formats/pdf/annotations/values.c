#include "internal.h"
#include <string.h>

int cupidimage_pdf_annotation_null(const pdf_object_view *view) {
    size_t at = 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &at);
    return view->len - at >= 4 && !memcmp(view->data + at, "null", 4) &&
           (view->len - at == 4 || cupidimage_pdf_is_ws(view->data[at + 4]) ||
            cupidimage_pdf_is_delim(view->data[at + 4]));
}

int cupidimage_pdf_annotation_dictionary(const pdf_object_view *value, size_t *end) {
    pdf_object_view dictionary = {0};
    return cupidimage_pdf_dictionary_view(value, 0, &dictionary, end);
}

/* Direct dictionaries and nulls are allowed; streams retain their canonical ID. */
int cupidimage_pdf_annotation_item(pdf_render_state *st, const pdf_object_view *source,
                                   size_t *position, pdf_object_view *out, pdf_object_id *identity,
                                   size_t *work) {
    memset(out, 0, sizeof(*out));
    *identity = 0;
    if (!*work)
        return 0;
    --*work;
    size_t start = *position, end;
    cupidimage_pdf_skip_ws_and_comments(source->data, source->len, &start);
    end = start;
    pdf_object_id object;
    if (cupidimage_pdf_parse_reference(source, &end, &object)) {
        int status = cupidimage_pdf_resolve_object(st->xref, st->document, st->document_size,
                                                   object, out, identity, work);
        if (status < 0)
            return 0;
        if (!status)
            *out = (pdf_object_view){(const unsigned char *)"null", 4, NULL, st->xref};
    } else {
        pdf_object_view tail = {source->data + start, source->len - start, NULL, source->xref};
        if (cupidimage_pdf_annotation_null(&tail))
            end = start + 4;
        else if (!cupidimage_pdf_dictionary_view(source, start, out, &end))
            return 0;
        *out = (pdf_object_view){source->data + start, end - start, NULL, source->xref};
    }
    size_t cost = 4;
    if (!cupidimage_pdf_annotation_null(out) && !cupidimage_pdf_annotation_dictionary(out, &cost)) {
        cupidimage_pdf_object_view_release(out);
        return 0;
    }
    if (cost > *work) {
        cupidimage_pdf_object_view_release(out);
        return 0;
    }
    *work -= cost;
    *position = end;
    return 1;
}
