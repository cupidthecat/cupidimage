#ifndef CUPIDIMAGE_PDF_POSTSCRIPT_TYPE1_GRIDFIT_H
#define CUPIDIMAGE_PDF_POSTSCRIPT_TYPE1_GRIDFIT_H
#include "../type1/trace.h"
#include "gridfit.h"

int cupidimage_pdf_ps_type1_inputs(const pdf_ps_private_hints *private_hints,
                                   const pdf_type1_hint_trace *trace, const pdf_cff_outline *source,
                                   const pdf_ps_gridfit_size *size, pdf_ps_private_hints *selected,
                                   unsigned *stem_count);
#endif
