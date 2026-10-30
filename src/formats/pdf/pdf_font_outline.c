#include "fonts/program.h"
#include <string.h>

void cupidimage_pdf_font_outline_cache_clear(struct pdf_font_program *program) {
    for (unsigned i = 0; i < PDF_FONT_OUTLINE_CACHE_SLOTS; i++) {
        cupidimage_pdf_tt_outline_free(&program->outline_cache[i].truetype);
        cupidimage_pdf_type2_free(&program->outline_cache[i].postscript);
    }
    memset(program->outline_cache, 0, sizeof(program->outline_cache));
    program->outline_cache_next = 0;
}

/* Cache font-space geometry only. Text transforms, colors, clipping and opacity
 * are applied after lookup and must never become part of this cached outline.
 * Borrowed storage remains valid until the next request on this program. */
int cupidimage_pdf_font_outline(struct pdf_font_program *program, unsigned glyph,
                                pdf_tt_outline *truetype, pdf_cff_outline *postscript,
                                size_t *cache_bytes_left, int *borrowed) {
    *borrowed = 0;
    for (unsigned i = 0; i < PDF_FONT_OUTLINE_CACHE_SLOTS; i++) {
        const pdf_cached_outline *entry = program->outline_cache + i;
        if (entry->valid && entry->glyph == glyph) {
            *truetype = entry->truetype;
            *postscript = entry->postscript;
            *borrowed = 1;
            return 1;
        }
    }
    int ok;
    if (program->is_type1)
        ok = cupidimage_pdf_type1_outline(&program->type1, glyph, postscript);
    else if (program->is_cff)
        ok = cupidimage_pdf_cff_glyph_outline(&program->cff, glyph, postscript);
    else
        ok = cupidimage_pdf_tt_outline(&program->font, glyph, truetype);
    if (!ok)
        return 0;
    size_t bytes = (size_t)truetype->capacity * sizeof(*truetype->points) +
                   (size_t)postscript->capacity * sizeof(*postscript->segments);
    if (!cache_bytes_left || bytes > *cache_bytes_left)
        return 1;
    pdf_cached_outline *entry = program->outline_cache + program->outline_cache_next;
    cupidimage_pdf_tt_outline_free(&entry->truetype);
    cupidimage_pdf_type2_free(&entry->postscript);
    entry->truetype = *truetype;
    entry->postscript = *postscript;
    entry->glyph = glyph;
    entry->valid = 1;
    program->outline_cache_next = (program->outline_cache_next + 1) % PDF_FONT_OUTLINE_CACHE_SLOTS;
    /* This is a cumulative page budget: eviction never replenishes it. Cache
     * exhaustion falls back to ordinary decoding, not a different glyph. */
    *cache_bytes_left -= bytes;
    *borrowed = 1;
    return 1;
}
