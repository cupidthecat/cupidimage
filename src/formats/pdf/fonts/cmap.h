#ifndef CUPIDIMAGE_PDF_ENCODING_CMAP_H
#define CUPIDIMAGE_PDF_ENCODING_CMAP_H
#include "../pdf_internal.h"
int cupidimage_pdf_cmap_load(const unsigned char *document, size_t size,
                             const pdf_object_view *view, pdf_cmap **result);
void cupidimage_pdf_cmap_free(pdf_cmap *map);
size_t cupidimage_pdf_cmap_bytes(const pdf_cmap *map);
int cupidimage_pdf_cmap_vertical(const pdf_cmap *map);
int cupidimage_pdf_cmap_next(const pdf_cmap *map, const unsigned char *text, size_t length,
                             size_t *used, uint32_t *source, unsigned *cid, unsigned *notdef,
                             int *valid, size_t *work);
#endif
