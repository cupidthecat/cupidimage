#ifndef CUPIDIMAGE_PDF_JBIG2_DOCUMENT_H
#define CUPIDIMAGE_PDF_JBIG2_DOCUMENT_H
#include "halftone.h"
#include "segments.h"
#include "symbols.h"

typedef struct {
    pdf_jb2_bitmap *bitmap;
    uint32_t width, height, x, y;
    unsigned combination;
} pdf_jb2_region;

typedef struct {
    uint32_t count;
    pdf_jb2_bitmap **symbols;
    unsigned flags;
    pdf_jb2_generic generic;
    pdf_jb2_refinement refinement;
    unsigned char *generic_states, *refinement_states;
    size_t generic_count, refinement_count;
} pdf_jb2_dictionary_result;

typedef struct {
    pdf_jb2_context *context;
    pdf_jb2_segments segments;
    pdf_jb2_huffman standard[15];
    pdf_jb2_bitmap *page;
    uint32_t width, height;
    int64_t stripe_end;
    unsigned page_flags, stripe_size, striped, unknown_height, ended;
} pdf_jb2_document;

int cupidimage_pdf_jb2_region_header(pdf_jb2_bits *, pdf_jb2_region *);
int cupidimage_pdf_jb2_signed_byte(pdf_jb2_bits *, int16_t *);
const pdf_jb2_huffman *cupidimage_pdf_jb2_standard(pdf_jb2_document *, unsigned);
int cupidimage_pdf_jb2_imports(pdf_jb2_document *, const pdf_jb2_segment *, pdf_jb2_bitmap ***,
                               uint32_t *, pdf_jb2_huffman *[8], unsigned *,
                               pdf_jb2_dictionary_result **);
const pdf_jb2_huffman *cupidimage_pdf_jb2_select_table(pdf_jb2_document *, unsigned,
                                                       pdf_jb2_huffman *const *, unsigned,
                                                       unsigned *, int);
int cupidimage_pdf_jb2_decode_dictionary(pdf_jb2_document *, pdf_jb2_segment *);
int cupidimage_pdf_jb2_decode_text(pdf_jb2_document *, pdf_jb2_segment *, pdf_jb2_region *);
int cupidimage_pdf_jb2_decode_region(pdf_jb2_document *, pdf_jb2_segment *);
#endif
