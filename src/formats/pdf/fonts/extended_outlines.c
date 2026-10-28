#include "standard.h"
#include <string.h>

#include "extended_outlines.inc"

const pdf_extended_font *cupidimage_pdf_extended_font(const char *name) {
    for (size_t i = 0; i < sizeof(extended_fonts) / sizeof(*extended_fonts); i++)
        if (!strcmp(name, extended_fonts[i].name))
            return &extended_fonts[i];
    return NULL;
}
