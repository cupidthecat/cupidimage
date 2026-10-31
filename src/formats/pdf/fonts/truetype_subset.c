#include "truetype_data.h"
#include <stdlib.h>
#include <string.h>

static uint32_t subset_location(const pdf_tt_font *font, unsigned glyph) {
    const unsigned char *p = font->loca.data + (size_t)glyph * font->location_size;
    uint32_t value = 0;
    for (unsigned i = 0; i < font->location_size; i++)
        value = (value << 8) | p[i];
    return font->location_size == 2 ? value * 2 : value;
}

static int subset_compare(const void *left, const void *right) {
    uint32_t a = *(const uint32_t *)left, b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

int cupidimage_pdf_tt_init_subset(pdf_tt_font *font, const unsigned char *data, size_t length,
                                  uint32_t **glyph_ends) {
    if (!glyph_ends)
        return 0;
    *glyph_ends = NULL;
    if (!font)
        return 0;
    if (cupidimage_pdf_tt_init(font, data, length))
        return 1;
    /* Only location ordering is relaxed. Header, metric and table bounds still
     * pass the ordinary checks before any offset becomes an address. */
    if (!cupidimage_pdf_tt_init_tables(font, data, length))
        return 0;
    unsigned count = font->glyphs + 1;
    uint32_t *locations = malloc((size_t)count * sizeof(*locations));
    uint32_t *ends = malloc((size_t)font->glyphs * sizeof(*ends));
    if (!locations || !ends) {
        free(locations);
        free(ends);
        memset(font, 0, sizeof(*font));
        return 0;
    }
    for (unsigned i = 0; i < count; i++)
        locations[i] = subset_location(font, i);
    qsort(locations, count, sizeof(*locations), subset_compare);
    for (unsigned glyph = 0; glyph < font->glyphs; glyph++) {
        uint32_t start = subset_location(font, glyph);
        if (start == subset_location(font, glyph + 1)) {
            ends[glyph] = start;
            continue;
        }
        unsigned low = 0, high = count;
        while (low < high) {
            unsigned middle = low + (high - low) / 2;
            if (locations[middle] <= start)
                low = middle + 1;
            else
                high = middle;
        }
        /* A repaired span never crosses another recorded glyph start. The
         * existing glyph parser must still validate the complete description. */
        ends[glyph] = low < count ? locations[low] : (uint32_t)font->glyf.length;
    }
    free(locations);
    font->glyph_ends = ends;
    *glyph_ends = ends;
    return 1;
}
