#ifndef CUPIDIMAGE_PDF_JBIG2_INTERNAL_H
#define CUPIDIMAGE_PDF_JBIG2_INTERNAL_H
#include <stddef.h>
#include <stdint.h>

#define PDF_JB2_MEMORY_LIMIT (128u * 1024u * 1024u)
#define PDF_JB2_INPUT_LIMIT (64u * 1024u * 1024u)
#define PDF_JB2_PIXEL_LIMIT (64u * 1024u * 1024u)

typedef struct pdf_jb2_allocation pdf_jb2_allocation;

typedef struct {
    pdf_jb2_allocation *allocations;
    size_t live, peak, limit, *work;
    int failed;
} pdf_jb2_context;

int cupidimage_pdf_jb2_context_init(pdf_jb2_context *, size_t *, size_t);
int cupidimage_pdf_jb2_fail(pdf_jb2_context *);
int cupidimage_pdf_jb2_take(pdf_jb2_context *, uint64_t);
void *cupidimage_pdf_jb2_alloc(pdf_jb2_context *, size_t, size_t);
int cupidimage_pdf_jb2_release(pdf_jb2_context *, void *);
void *cupidimage_pdf_jb2_detach(pdf_jb2_context *, void *);
void cupidimage_pdf_jb2_clear(pdf_jb2_context *);

typedef struct {
    pdf_jb2_context *context;
    const unsigned char *data;
    size_t length, bit;
} pdf_jb2_bits;

int cupidimage_pdf_jb2_read(pdf_jb2_bits *, unsigned, uint32_t *);
int cupidimage_pdf_jb2_align(pdf_jb2_bits *);

typedef struct {
    pdf_jb2_context *context;
    const unsigned char *data;
    size_t length, position;
    uint32_t a, c;
    unsigned ct, marker;
} pdf_jb2_arithmetic;

int cupidimage_pdf_jb2_arithmetic_init(pdf_jb2_arithmetic *, pdf_jb2_context *,
                                       const unsigned char *, size_t);
/* Each byte packs an E.1 state index in bits 1..6 and MPS in bit 0. */
int cupidimage_pdf_jb2_arithmetic_bit(pdf_jb2_arithmetic *, unsigned char *, unsigned *);
int cupidimage_pdf_jb2_arithmetic_integer(pdf_jb2_arithmetic *, unsigned char[512], int64_t *,
                                          int *);
int cupidimage_pdf_jb2_arithmetic_id(pdf_jb2_arithmetic *, unsigned char *, size_t, unsigned,
                                     uint32_t *);

typedef struct {
    uint32_t width, height;
    size_t stride;
    unsigned char *data;
} pdf_jb2_bitmap;

pdf_jb2_bitmap *cupidimage_pdf_jb2_bitmap_new(pdf_jb2_context *, uint32_t, uint32_t, unsigned);
unsigned cupidimage_pdf_jb2_pixel(const pdf_jb2_bitmap *, int64_t, int64_t);
void cupidimage_pdf_jb2_pixel_set(pdf_jb2_bitmap *, uint32_t, uint32_t, unsigned);
int cupidimage_pdf_jb2_blit(pdf_jb2_context *, pdf_jb2_bitmap *, const pdf_jb2_bitmap *, int64_t,
                            int64_t, unsigned);

typedef struct {
    unsigned prefix, range, flags; /* flags: 1 lower range, 2 out-of-band. */
    int64_t low;
} pdf_jb2_huff_row;

typedef struct {
    int32_t child[2], row;
} pdf_jb2_huff_node;

typedef struct {
    const pdf_jb2_huff_row *rows;
    pdf_jb2_huff_node *nodes;
    unsigned count, used, capacity;
} pdf_jb2_huffman;

int cupidimage_pdf_jb2_huffman_build(pdf_jb2_context *, const pdf_jb2_huff_row *, unsigned,
                                     pdf_jb2_huffman *);
int cupidimage_pdf_jb2_huffman_standard(pdf_jb2_context *, unsigned, pdf_jb2_huffman *);
int cupidimage_pdf_jb2_huffman_custom(pdf_jb2_context *, const unsigned char *, size_t,
                                      pdf_jb2_huffman *);
int cupidimage_pdf_jb2_huffman_value(pdf_jb2_bits *, const pdf_jb2_huffman *, int64_t *, int *);

typedef struct {
    uint32_t width, height;
    unsigned template_id, extended, typical, at_count, pattern_width;
    int16_t at_x[12], at_y[12];
    const pdf_jb2_bitmap *skip;
} pdf_jb2_generic;

int cupidimage_pdf_jb2_generic_defaults(pdf_jb2_generic *, unsigned, unsigned);
size_t cupidimage_pdf_jb2_generic_contexts(unsigned);
int cupidimage_pdf_jb2_generic_decode(pdf_jb2_context *, const pdf_jb2_generic *,
                                      pdf_jb2_arithmetic *, unsigned char *, size_t,
                                      pdf_jb2_bitmap **);
int cupidimage_pdf_jb2_mmr(pdf_jb2_context *, const unsigned char *, size_t, uint32_t, uint32_t,
                           int, size_t *, pdf_jb2_bitmap **);

typedef struct {
    uint32_t width, height;
    unsigned template_id, typical;
    int16_t at_x[2], at_y[2];
    int32_t dx, dy;
    const pdf_jb2_bitmap *reference;
} pdf_jb2_refinement;

int cupidimage_pdf_jb2_refinement_decode(pdf_jb2_context *, const pdf_jb2_refinement *,
                                         pdf_jb2_arithmetic *, unsigned char *, size_t,
                                         pdf_jb2_bitmap **);
#endif
