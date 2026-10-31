#ifndef CUPIDIMAGE_PDF_ANNOTATIONS_INTERNAL_H
#define CUPIDIMAGE_PDF_ANNOTATIONS_INTERNAL_H

#include "../pdf_render_internal.h"

int cupidimage_pdf_annotation_item(pdf_render_state *, const pdf_object_view *, size_t *,
                                   pdf_object_view *, pdf_object_id *, size_t *);
int cupidimage_pdf_annotation_null(const pdf_object_view *);
int cupidimage_pdf_annotation_dictionary(const pdf_object_view *, size_t *);
int cupidimage_pdf_annotation_paint(pdf_render_state *, const pdf_object_view *,
                                    const pdf_page_geometry *, size_t *);

#endif
