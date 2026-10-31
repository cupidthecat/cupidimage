#ifndef CUPIDIMAGE_PDF_JBIG2_SEGMENTS_H
#define CUPIDIMAGE_PDF_JBIG2_SEGMENTS_H
#include "internal.h"

typedef struct {
    uint32_t number, page, reference_count, rows;
    unsigned type, deferred, unknown_length, global;
    const unsigned char *data, *retention;
    size_t length;
    uint32_t *references;
    unsigned state, retained, intermediate_users;
    void *result;
} pdf_jb2_segment;

typedef struct {
    pdf_jb2_context *context;
    pdf_jb2_segment *segments;
    size_t count, capacity, reference_count;
} pdf_jb2_segments;

int cupidimage_pdf_jb2_segments_read(pdf_jb2_segments *, const unsigned char *, size_t, unsigned);
int cupidimage_pdf_jb2_segments_index(pdf_jb2_segments *);
pdf_jb2_segment *cupidimage_pdf_jb2_segment_find(pdf_jb2_segments *, uint32_t);
int cupidimage_pdf_jb2_segment_retains(const pdf_jb2_segment *, unsigned);
#endif
