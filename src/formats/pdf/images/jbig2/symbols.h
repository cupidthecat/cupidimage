#ifndef CUPIDIMAGE_PDF_JBIG2_SYMBOLS_H
#define CUPIDIMAGE_PDF_JBIG2_SYMBOLS_H
#include "internal.h"

enum {
    JB2_DH,
    JB2_DW,
    JB2_EX,
    JB2_AI,
    JB2_DT,
    JB2_FS,
    JB2_DS,
    JB2_IT,
    JB2_RI,
    JB2_RDW,
    JB2_RDH,
    JB2_RDX,
    JB2_RDY,
    JB2_INTEGER_CONTEXTS
};

typedef struct {
    pdf_jb2_context *context;
    unsigned huffman, id_bits, fixed_ids;
    pdf_jb2_bits bits;
    pdf_jb2_arithmetic arithmetic;
    unsigned char integers[JB2_INTEGER_CONTEXTS][512];
    unsigned char *ids;
    size_t id_count;
    const pdf_jb2_huffman *symbol_codes;
} pdf_jb2_coding;

int cupidimage_pdf_jb2_coding_init(pdf_jb2_coding *, pdf_jb2_context *, const unsigned char *,
                                   size_t, unsigned, unsigned);
int cupidimage_pdf_jb2_integer(pdf_jb2_coding *, unsigned, const pdf_jb2_huffman *, int64_t *,
                               int *);
int cupidimage_pdf_jb2_integer_required(pdf_jb2_coding *, unsigned, const pdf_jb2_huffman *,
                                        int64_t *);
int cupidimage_pdf_jb2_symbol_codes(pdf_jb2_bits *, uint32_t, pdf_jb2_huffman *);
int cupidimage_pdf_jb2_symbol_id(pdf_jb2_coding *, uint32_t *);
int cupidimage_pdf_jb2_refine_symbol(pdf_jb2_coding *, const pdf_jb2_refinement *, unsigned char *,
                                     size_t, const pdf_jb2_huffman *, pdf_jb2_bitmap **);

typedef struct {
    uint32_t width, height, instances, symbol_count;
    pdf_jb2_bitmap **symbols;
    unsigned refine, strips_log, corner, transposed, combination, default_pixel;
    int ds_offset;
    const pdf_jb2_huffman *fs, *ds, *dt, *rdw, *rdh, *rdx, *rdy, *rsize;
    pdf_jb2_refinement refinement;
    unsigned char *refinement_states;
    size_t refinement_count;
} pdf_jb2_text;

int cupidimage_pdf_jb2_text_decode(pdf_jb2_coding *, const pdf_jb2_text *, pdf_jb2_bitmap **);

typedef struct {
    uint32_t imported_count, new_count, exported_count;
    pdf_jb2_bitmap **imported;
    unsigned aggregate;
    pdf_jb2_generic generic;
    unsigned char *generic_states;
    size_t generic_count;
    pdf_jb2_refinement refinement;
    unsigned char *refinement_states;
    size_t refinement_count;
    const pdf_jb2_huffman *dh, *dw, *bitmap_size, *aggregate_count;
    const pdf_jb2_huffman *standard1, *standard6, *standard8, *standard11, *standard15;
} pdf_jb2_dictionary;

int cupidimage_pdf_jb2_dictionary_decode(pdf_jb2_coding *, const pdf_jb2_dictionary *,
                                         pdf_jb2_bitmap ***);
#endif
