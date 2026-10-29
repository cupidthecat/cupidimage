#include "internal.h"

int cupidimage_pdf_tt_hint_phantoms(pdf_tt_hint *hint, unsigned glyph, const pdf_tt_table *view,
                                    int32_t result[4][2]) {
    const pdf_tt_font *font = &hint->font;
    int32_t xmin = view->length ? tt_hint_i16(view->data + 2) : 0;
    int32_t ymax = view->length ? tt_hint_i16(view->data + 8) : 0;
    result[0][0] = xmin - cupidimage_pdf_tt_left_bearing(font, glyph);
    result[1][0] = result[0][0] + (int32_t)cupidimage_pdf_tt_advance(font, glyph);
    result[0][1] = result[1][1] = result[2][0] = result[3][0] = 0;
    if (font->vhea.data || font->vmtx.data) {
        if (font->vhea.length < 36 || !font->vmtx.data || tt_hint_u16(font->vhea.data) != 1 ||
            (tt_hint_u16(font->vhea.data + 2) != 0 && tt_hint_u16(font->vhea.data + 2) != 0x1000))
            return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_PROGRAM);
        unsigned count = tt_hint_u16(font->vhea.data + 34);
        if (!count || count > font->glyphs ||
            font->vmtx.length < (size_t)count * 4 + (font->glyphs - count) * 2)
            return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_PROGRAM);
        unsigned metric = glyph < count ? glyph : count - 1;
        size_t bearing =
            glyph < count ? (size_t)glyph * 4 + 2 : (size_t)count * 4 + (glyph - count) * 2;
        result[2][1] = ymax + tt_hint_i16(font->vmtx.data + bearing);
        result[3][1] = result[2][1] - (int32_t)tt_hint_u16(font->vmtx.data + metric * 4);
    } else if (font->os2.data && font->os2.length >= 72) {
        result[2][1] = tt_hint_i16(font->os2.data + 68);
        result[3][1] = tt_hint_i16(font->os2.data + 70);
    } else {
        /* Legacy version 0 can end at usLastCharIndex, before typographic metrics. */
        if (font->os2.data && (font->os2.length != 68 || tt_hint_u16(font->os2.data) != 0))
            return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_PROGRAM);
        result[2][1] = tt_hint_i16(font->hhea.data + 4);
        result[3][1] = tt_hint_i16(font->hhea.data + 6);
    }
    return 1;
}
