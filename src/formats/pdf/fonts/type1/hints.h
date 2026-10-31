#ifndef CUPIDIMAGE_PDF_TYPE1_HINTS_H
#define CUPIDIMAGE_PDF_TYPE1_HINTS_H
#include "../postscript/hints.h"
#include "lexer.h"
#include "syntax.h"

unsigned cupidimage_pdf_type1_hint_field(const char *name);
void cupidimage_pdf_type1_hint_read(pdf_ps_private_hints *hints, unsigned *unresolved,
                                    unsigned field, pdf_cff_slice source, size_t *position,
                                    pdf_type1_syntax *syntax);
#endif
