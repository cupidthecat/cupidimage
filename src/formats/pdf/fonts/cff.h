#ifndef CUPIDIMAGE_PDF_CFF_H
#define CUPIDIMAGE_PDF_CFF_H
#include "postscript/hints.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const unsigned char *data;
    size_t length;
} pdf_cff_slice;

typedef struct {
    pdf_cff_slice offsets, objects;
    unsigned count, offset_size;
} pdf_cff_index;

typedef struct {
    size_t charset, encoding, charstrings, private_offset, private_size, subrs;
    size_t fdarray, fdselect;
    double matrix[6], default_width, nominal_width;
    pdf_ps_private_hints hints;
    int32_t random_seed;
    unsigned cid, has_matrix;
} pdf_cff_dict;

int cupidimage_pdf_cff_index(pdf_cff_slice source, size_t *position, pdf_cff_index *index);
int cupidimage_pdf_cff_item(const pdf_cff_index *index, unsigned item, pdf_cff_slice *slice);
int cupidimage_pdf_cff_dict(pdf_cff_slice source, pdf_cff_dict *dict, int private_dict);

typedef struct {
    pdf_cff_index subrs;
    pdf_ps_private_hints hints;
    uint32_t random_seed;
    double matrix[6], default_width, nominal_width;
} pdf_cff_private;

typedef struct {
    pdf_cff_slice source;
    pdf_cff_index strings, globals, chars;
    pdf_cff_dict top;
    pdf_cff_private *private_dicts;
    uint16_t *selectors;
    unsigned char *fdselect;
    unsigned encoding[256], private_count;
} pdf_cff_font;

int cupidimage_pdf_cff_init(pdf_cff_font *font, const unsigned char *data, size_t length);
void cupidimage_pdf_cff_free(pdf_cff_font *font);
unsigned cupidimage_pdf_cff_name(const pdf_cff_font *font, const char *name);
unsigned cupidimage_pdf_cff_standard_glyph(const pdf_cff_font *font, unsigned code);
#endif
