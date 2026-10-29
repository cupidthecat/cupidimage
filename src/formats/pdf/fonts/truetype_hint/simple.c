#include "internal.h"
#include <limits.h>
#include <stdlib.h>

static int tt_hint_scaled(pdf_tt_hint *hint, int32_t value, int32_t *scaled) {
    int64_t result = cupidimage_pdf_tt_vm_divide((int64_t)value * hint->vm.size.scale, 65536);
    if (result < INT32_MIN || result > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_ARITHMETIC);
    *scaled = (int32_t)result;
    return 1;
}

int cupidimage_pdf_tt_hint_glyph(pdf_tt_hint *hint, unsigned glyph) {
    pdf_tt_vm *vm = &hint->vm;
    if (vm->error != PDF_TT_VM_OK)
        return 0;
    if (!vm->prepared)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
    pdf_tt_table view, program;
    if (!cupidimage_pdf_tt_glyph_data(&hint->font, glyph, &view) ||
        (view.length && view.length < 10))
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
    if (view.length && tt_hint_i16(view.data) < 0)
        return cupidimage_pdf_tt_vm_fail(vm, tt_hint_i16(view.data) == -1 ? PDF_TT_VM_UNSUPPORTED
                                                                          : PDF_TT_VM_PROGRAM);
    if (view.length > hint->glyph_budget)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_BUDGET);
    unsigned contours = view.length ? tt_hint_u16(view.data) : 0, outline = 0;
    if (view.length && contours > (view.length - 10) / 2)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
    if (contours)
        outline = tt_hint_u16(view.data + 10 + (contours - 1) * 2) + 1;
    unsigned count = outline + 4;
    size_t remaining = hint->glyph_budget - view.length;
    if (count > remaining)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_BUDGET);
    remaining -= count;
    int decoded = cupidimage_pdf_tt_simple_points(&view, &hint->raw, &program);
    if (decoded <= 0)
        return cupidimage_pdf_tt_vm_fail(vm, decoded < 0 ? PDF_TT_VM_MEMORY : PDF_TT_VM_PROGRAM);
    int32_t phantom[4][2];
    if (!cupidimage_pdf_tt_hint_phantoms(hint, glyph, &view, phantom))
        return 0;
    pdf_tt_vm_point *points = calloc(count, sizeof(*points));
    int32_t (*design)[2] = malloc((size_t)count * sizeof(*design));
    if (!points || !design) {
        free(points);
        free(design);
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_MEMORY);
    }
    int ok = 1;
    for (unsigned i = 0; ok && i < count; i++) {
        if (i < outline) {
            design[i][0] = (int32_t)hint->raw.points[i].x;
            design[i][1] = (int32_t)hint->raw.points[i].y;
            points[i].on = hint->raw.points[i].on;
            points[i].end = hint->raw.points[i].end;
        } else {
            design[i][0] = phantom[i - outline][0];
            design[i][1] = phantom[i - outline][1];
        }
        for (unsigned axis = 0; ok && axis < 2; axis++) {
            ok = tt_hint_scaled(hint, design[i][axis], &points[i].original[axis]);
            int64_t current = points[i].original[axis];
            if (!(vm->prepared_graphics.instruction_control & 1) && i >= outline &&
                axis == (i - outline < 2 ? 0u : 1u)) {
                current += 32;
                current = (current / 64 - (current < 0 && current % 64 != 0)) * 64;
                if (current < INT32_MIN || current > INT32_MAX)
                    ok = cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
            }
            points[i].current[axis] = ok ? (int32_t)current : 0;
        }
    }
    if (ok)
        ok = cupidimage_pdf_tt_vm_begin_glyph(vm, points, count, outline) &&
             cupidimage_pdf_tt_vm_set_design(vm, design, count, vm->size.scale) &&
             cupidimage_pdf_tt_vm_run(vm, program.data, program.length, PDF_TT_VM_GLYPH, remaining);
    free(points);
    free(design);
    return ok;
}
