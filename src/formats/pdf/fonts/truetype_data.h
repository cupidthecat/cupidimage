#ifndef CUPIDIMAGE_PDF_TRUETYPE_DATA_H
#define CUPIDIMAGE_PDF_TRUETYPE_DATA_H
#include "truetype.h"

/* Internal, borrowed table views and unnormalized simple-glyph coordinates. */
int cupidimage_pdf_tt_glyph_data(const pdf_tt_font *, unsigned, pdf_tt_table *);
/* Returns 1 on success, 0 for invalid data, -1 for allocation failure. */
int cupidimage_pdf_tt_simple_points(const pdf_tt_table *, pdf_tt_outline *, pdf_tt_table *);
int cupidimage_pdf_tt_left_bearing(const pdf_tt_font *, unsigned);

typedef struct {
    unsigned flags, glyph;
    int32_t argument[2], matrix[4];
} pdf_tt_component;

int cupidimage_pdf_tt_component_read(const pdf_tt_table *, size_t *, pdf_tt_component *);
int cupidimage_pdf_tt_component_program(const pdf_tt_table *, size_t, int, pdf_tt_table *);
#endif
