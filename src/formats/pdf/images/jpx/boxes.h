#ifndef CUPIDIMAGE_PDF_JPX_BOXES_H
#define CUPIDIMAGE_PDF_JPX_BOXES_H

#include "file.h"

#define JPX_BOX(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (d))

typedef struct {
    const unsigned char *data;
    size_t length, boxes, metadata_end;
    size_t *work;
    pdf_jpx_file *file;
} pdf_jpx_reader;

typedef struct {
    uint32_t type;
    pdf_jpx_slice payload;
    int to_end;
} pdf_jpx_box;

uint16_t cupidimage_jpx_u16(const unsigned char *);
uint32_t cupidimage_jpx_u32(const unsigned char *);
uint64_t cupidimage_jpx_u64(const unsigned char *);
int cupidimage_jpx_charge(pdf_jpx_reader *, size_t);
int cupidimage_jpx_box_next(pdf_jpx_reader *, pdf_jpx_slice, size_t *, int, pdf_jpx_box *);
int cupidimage_jpx_fragments(pdf_jpx_reader *, pdf_jpx_slice, size_t, unsigned char **, size_t *,
                             size_t *, size_t *);
int cupidimage_jpx_header_read(pdf_jpx_reader *, pdf_jpx_slice, int, int);
int cupidimage_jpx_channels(pdf_jpx_file *);

#endif
