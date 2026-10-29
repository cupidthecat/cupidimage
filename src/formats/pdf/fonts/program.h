#ifndef CUPIDIMAGE_PDF_FONT_PROGRAM_H
#define CUPIDIMAGE_PDF_FONT_PROGRAM_H
#include "truetype.h"
#include "type1.h"
#include "type2.h"

#define PDF_FONT_OUTLINE_CACHE_SLOTS 16
#define PDF_FONT_EXTENDED_GLYPH 65536u

typedef struct {
    pdf_tt_outline truetype;
    pdf_cff_outline postscript;
    unsigned glyph;
    int valid;
} pdf_cached_outline;

struct pdf_font_program {
    unsigned references;
    pdf_tt_font font;
    pdf_cff_font cff;
    int is_cff, is_type1;
    pdf_type1_font type1;
    unsigned char *bytes, *gid_map;
    size_t length, gid_length;
    unsigned glyphs[256];
    int cid;
    pdf_cached_outline outline_cache[PDF_FONT_OUTLINE_CACHE_SLOTS];
    unsigned outline_cache_next;
    struct pdf_font_program *extended;
};

struct pdf_font_program *cupidimage_pdf_font_program_select(struct pdf_font_program *, unsigned *);
struct pdf_font_program *cupidimage_pdf_load_substitute_program(const char *);

int cupidimage_pdf_font_program_retain(struct pdf_font_program *program);
void cupidimage_pdf_font_outline_cache_clear(struct pdf_font_program *program);
int cupidimage_pdf_font_outline(struct pdf_font_program *program, unsigned glyph,
                                pdf_tt_outline *truetype, pdf_cff_outline *postscript,
                                size_t *cache_bytes_left, int *borrowed);
#endif
