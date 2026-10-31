#include "internal.h"
#include <stdlib.h>

int cupidimage_pdf_render_annotations(pdf_render_state *parent, const pdf_page_geometry *geometry) {
    pdf_object_view page = {0}, annotations = {0}, value = {0};
    pdf_render_state *st = NULL;
    int ok = 0;
    if (!cupidimage_pdf_state_object(parent, parent->page_object, &page))
        goto done;
    int status = cupidimage_pdf_optional_value(parent->document, parent->document_size, &page,
                                               "Annots", &annotations);
    if (status <= 0) {
        ok = status == 0;
        goto done;
    }
    size_t pos = 0, work = 16u * 1024u * 1024u;
    cupidimage_pdf_skip_ws_and_comments(annotations.data, annotations.len, &pos);
    if (pos == annotations.len || annotations.data[pos++] != '[')
        goto done;
    st = malloc(sizeof(*st));
    if (!st)
        goto done;
    *st = *parent;
    st->gs = st->initial_gs;
    cupidimage_pdf_path_init(&st->path);
    st->text_group = NULL;
    st->text_clip = NULL;
    st->text_winding = NULL;
    st->text_winding_bytes = 0;
    st->gs_stack_len = st->pending_clip = st->in_text = 0;
    st->form_depth = st->glyph_depth = st->glyph_active = 0;
    st->optional_hidden = st->paint_coverage_clipped = 0;
    st->image_failed = 0;
    for (unsigned count = 0; count <= 65536; count++) {
        cupidimage_pdf_skip_ws_and_comments(annotations.data, annotations.len, &pos);
        if (pos < annotations.len && annotations.data[pos] == ']') {
            ok = 1;
            break;
        }
        pdf_object_id identity;
        if (count == 65536 ||
            !cupidimage_pdf_annotation_item(st, &annotations, &pos, &value, &identity, &work))
            break;
        if (!cupidimage_pdf_annotation_null(&value) &&
            !cupidimage_pdf_annotation_paint(st, &value, geometry, &work))
            break;
        cupidimage_pdf_object_view_release(&value);
    }
done:
    cupidimage_pdf_object_view_release(&value);
    cupidimage_pdf_object_view_release(&annotations);
    cupidimage_pdf_object_view_release(&page);
    if (st)
        cupidimage_pdf_render_state_release(st);
    free(st);
    if (!ok)
        cupidimage_set_err(parent->error, parent->error_capacity,
                           "pdf: invalid annotation appearance or resource limit exceeded");
    return ok;
}
