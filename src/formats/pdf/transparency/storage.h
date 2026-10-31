#ifndef CUPIDIMAGE_PDF_GROUP_STORAGE_H
#define CUPIDIMAGE_PDF_GROUP_STORAGE_H
#include <stddef.h>

/* Samples contain alpha, shape, then three or four native color components. */
typedef struct {
    double **blocks;
    size_t pixels, count, bytes, limit;
    size_t *live_bytes;
    unsigned stride;
} pdf_group_storage;

int cupidimage_pdf_group_storage_init(pdf_group_storage *, size_t, unsigned, size_t *, size_t);
const double *cupidimage_pdf_group_storage_read(const pdf_group_storage *, size_t);
double *cupidimage_pdf_group_storage_write(pdf_group_storage *, size_t);
void cupidimage_pdf_group_storage_detach(pdf_group_storage *);
void cupidimage_pdf_group_storage_free(pdf_group_storage *);
#endif
