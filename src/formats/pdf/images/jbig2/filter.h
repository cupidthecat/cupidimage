#ifndef CUPIDIMAGE_PDF_JBIG2_FILTER_H
#define CUPIDIMAGE_PDF_JBIG2_FILTER_H
#include "formats/pdf/pdf_internal.h"

int cupidimage_pdf_jbig2_filter(const pdf_object_view *, const pdf_image_stream_profile *,
                                pdf_object_id, const unsigned char *, size_t, unsigned char **,
                                size_t *);
#endif
