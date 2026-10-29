#include "internal.h"

int cupidimage_pdf_tt_hint_take(pdf_tt_hint *hint, size_t *work, size_t amount) {
    if (amount > *work)
        return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_BUDGET);
    *work -= amount;
    return 1;
}

int cupidimage_pdf_tt_hint_load(pdf_tt_hint *hint, unsigned glyph, unsigned chain[16],
                                unsigned depth, unsigned *calls, size_t *work) {
    if (depth >= 16 || !*calls)
        return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_BUDGET);
    (*calls)--;
    for (unsigned i = 0; i < depth; i++)
        if (chain[i] == glyph)
            return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_PROGRAM);
    chain[depth] = glyph;
    pdf_tt_table view;
    if (!cupidimage_pdf_tt_glyph_data(&hint->font, glyph, &view) ||
        (view.length && view.length < 10))
        return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_PROGRAM);
    if (!cupidimage_pdf_tt_hint_take(hint, work, 1) ||
        !cupidimage_pdf_tt_hint_take(hint, work, view.length))
        return 0;
    if (!view.length || tt_hint_i16(view.data) >= 0)
        return cupidimage_pdf_tt_hint_simple(hint, glyph, &view, work);
    return cupidimage_pdf_tt_hint_composite(hint, glyph, &view, chain, depth, calls, work);
}

int cupidimage_pdf_tt_hint_glyph(pdf_tt_hint *hint, unsigned glyph) {
    if (hint->vm.error != PDF_TT_VM_OK)
        return 0;
    if (!hint->vm.prepared)
        return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_PROGRAM);
    if (!cupidimage_pdf_tt_vm_begin_glyph(&hint->vm, NULL, 0, 0))
        return 0;
    unsigned chain[16], calls = 4096;
    size_t work = hint->glyph_budget;
    return cupidimage_pdf_tt_hint_load(hint, glyph, chain, 0, &calls, &work);
}
