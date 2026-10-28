#include "standard.h"
#include "standard_metrics.inc"
#include <string.h>

const pdf_standard_font *cupidimage_pdf_standard_font(const char *name) {
    for (size_t i = 0; i < sizeof(standard_fonts) / sizeof(*standard_fonts); i++)
        if (!strcmp(name, standard_fonts[i].name))
            return &standard_fonts[i];
    return NULL;
}

unsigned cupidimage_pdf_standard_glyph(const void *context, const char *name) {
    const pdf_standard_font *font = context;
    size_t lo = 1, hi = font->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(font->metrics[mid].name, name);
        if (cmp < 0)
            lo = mid + 1;
        else if (cmp > 0)
            hi = mid;
        else
            return (unsigned)mid;
    }
    return 0;
}
