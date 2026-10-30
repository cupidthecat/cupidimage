#ifndef CUPIDIMAGE_PDF_TRUETYPE_H
#define CUPIDIMAGE_PDF_TRUETYPE_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const unsigned char *data;
    size_t length;
} pdf_tt_table;

typedef struct {
    pdf_tt_table head, maxp, loca, glyf, hhea, hmtx, cmap, post, cvt, fpgm, prep, os2, vhea, vmtx;
    unsigned units, glyphs, location_size, hmetrics;
} pdf_tt_font;

typedef struct {
    double x, y;
    unsigned char on, end;
} pdf_tt_point;

typedef struct {
    pdf_tt_point *points;
    unsigned count, capacity;
    unsigned advance; /* Resolved, unhinted advance in font units. */
} pdf_tt_outline;

int cupidimage_pdf_tt_init(pdf_tt_font *font, const unsigned char *data, size_t length);
int cupidimage_pdf_tt_has_cmap(const pdf_tt_font *font, unsigned platform, unsigned encoding);
unsigned cupidimage_pdf_tt_map(const pdf_tt_font *font, uint32_t code, int platform, int encoding);
unsigned cupidimage_pdf_tt_name(const pdf_tt_font *font, const char *name);
/* Raw hmtx entry; composite inheritance is resolved with the outline. */
unsigned cupidimage_pdf_tt_advance(const pdf_tt_font *font, unsigned glyph);
int cupidimage_pdf_tt_outline(const pdf_tt_font *font, unsigned glyph, pdf_tt_outline *outline);
void cupidimage_pdf_tt_outline_free(pdf_tt_outline *outline);
#endif
