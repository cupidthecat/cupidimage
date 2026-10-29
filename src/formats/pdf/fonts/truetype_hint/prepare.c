#include "internal.h"
#include <stdlib.h>
#include <string.h>

void cupidimage_pdf_tt_hint_free(pdf_tt_hint *hint) {
    cupidimage_pdf_tt_vm_free(&hint->vm);
    cupidimage_pdf_tt_outline_free(&hint->raw);
    memset(hint, 0, sizeof(*hint));
}

int cupidimage_pdf_tt_hint_init(pdf_tt_hint *hint, const pdf_tt_font *font, int32_t pixels,
                                int32_t points, int grayscale, size_t budget) {
    memset(hint, 0, sizeof(*hint));
    hint->font = *font;
    hint->glyph_budget = budget;
    if (font->maxp.length < 32 || tt_hint_u16(font->maxp.data) != 1 ||
        tt_hint_u16(font->maxp.data + 2) || tt_hint_u16(font->maxp.data + 14) < 1 ||
        tt_hint_u16(font->maxp.data + 14) > 2 || font->cvt.length % 2 ||
        font->cvt.length / 2 > 65535)
        return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_PROGRAM);
    unsigned count = (unsigned)(font->cvt.length / 2);
    int32_t *cvt = count ? malloc((size_t)count * sizeof(*cvt)) : NULL;
    if (count && !cvt)
        return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_MEMORY);
    for (unsigned i = 0; i < count; i++)
        cvt[i] = tt_hint_i16(font->cvt.data + i * 2);
    int ok = cupidimage_pdf_tt_vm_init(&hint->vm, tt_hint_u16(font->maxp.data + 24),
                                       tt_hint_u16(font->maxp.data + 18), cvt, count,
                                       tt_hint_u16(font->maxp.data + 20));
    if (ok)
        ok = cupidimage_pdf_tt_vm_run(&hint->vm, font->fpgm.data, font->fpgm.length, PDF_TT_VM_FONT,
                                      budget);
    if (ok)
        ok =
            cupidimage_pdf_tt_vm_set_size(&hint->vm, font->units, pixels, points, grayscale, cvt) &&
            cupidimage_pdf_tt_vm_set_zones(&hint->vm, tt_hint_u16(font->maxp.data + 16), NULL, 0,
                                           0);
    if (ok)
        ok = cupidimage_pdf_tt_vm_run(&hint->vm, font->prep.data, font->prep.length, PDF_TT_VM_PREP,
                                      hint->vm.work_left) &&
             cupidimage_pdf_tt_vm_save_preparation(&hint->vm);
    free(cvt);
    return ok;
}
