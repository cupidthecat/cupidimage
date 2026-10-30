#ifndef CUPIDIMAGE_PDF_OPTIONAL_CONTENT_INTERNAL_H
#define CUPIDIMAGE_PDF_OPTIONAL_CONTENT_INTERNAL_H
#include "state.h"

int cupidimage_pdf_oc_charge(pdf_optional_content *, size_t);
int cupidimage_pdf_oc_null(const pdf_object_view *);
int cupidimage_pdf_oc_item(pdf_optional_content *, const pdf_object_view *, size_t *,
                           pdf_object_view *, pdf_object_id *);
int cupidimage_pdf_oc_property(pdf_optional_content *, const pdf_object_view *, const char *,
                               pdf_object_view *);
int cupidimage_pdf_oc_intent(pdf_optional_content *, const pdf_object_view *, int *);
const pdf_optional_group *cupidimage_pdf_oc_find(const pdf_optional_content *, pdf_object_id);
#endif
