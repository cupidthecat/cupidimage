#include "standard.h"
#include <string.h>

/* These generated definitions require the types from standard.h. */
#include "standard_outlines.inc"

const pdf_standard_outline *cupidimage_pdf_standard_outline(const char *name) {
    for (size_t i = 0; i < sizeof(standard_outlines) / sizeof(*standard_outlines); i++)
        if (!strcmp(name, standard_outlines[i].name))
            return &standard_outlines[i];
    return NULL;
}
