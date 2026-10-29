#ifndef CUPIDIMAGE_PDF_POSTSCRIPT_GRIDFIT_H
#define CUPIDIMAGE_PDF_POSTSCRIPT_GRIDFIT_H
#include "../type1/trace.h"
#include "../type2.h"

/* Positive 16.16 font-unit-to-device-pixel scales. Translation is applied later. */
typedef struct {
    int32_t x_scale, y_scale;
    /* Component origin in 16.16 character-space units, before fitting. */
    int32_t origin_x, origin_y;
} pdf_ps_gridfit_size;

/* Returns an owned, device-pixel outline, quantized down to the 1/64-pixel grid.
 * Inputs are borrowed and unchanged. Composite components need separate calls.
 * The budget is shared with the caller and consumed on both success and failure. */
int cupidimage_pdf_ps_gridfit(const pdf_ps_private_hints *private_hints,
                              const pdf_ps_hint_program *hints, const pdf_cff_outline *source,
                              const pdf_ps_gridfit_size *size, pdf_cff_outline *output,
                              size_t *work);
int cupidimage_pdf_ps_gridfit_type1(const pdf_ps_private_hints *private_hints,
                                    const pdf_type1_hint_trace *trace,
                                    const pdf_cff_outline *source, const pdf_ps_gridfit_size *size,
                                    pdf_cff_outline *output, size_t *work);
/* Transform an owned 1/64-pixel outline transactionally. Advances are unchanged. */
int cupidimage_pdf_ps_outline_transform(pdf_cff_outline *outline, const int32_t matrix[4],
                                        const int32_t offset[2], size_t *work_left);
#endif
