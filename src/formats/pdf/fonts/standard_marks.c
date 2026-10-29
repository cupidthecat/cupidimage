#include "standard.h"
#include <string.h>

#include "standard_marks.inc"

const pdf_standard_outline *cupidimage_pdf_standard_mark(const char *name) {
    for (size_t i = 0; i < sizeof(standard_marks) / sizeof(*standard_marks); i++)
        if (!strcmp(name, standard_marks[i].name))
            return &standard_marks[i];
    return NULL;
}
