#include "internal.h"
#include <limits.h>
#include <stdlib.h>

int cupidimage_pdf_tt_hint_scale(pdf_tt_hint *hint, unsigned axis, int32_t value, int32_t *scaled) {
    int64_t result =
        cupidimage_pdf_tt_vm_divide((int64_t)value * hint->vm.size.axis_scale[axis], 65536);
    if (result < INT32_MIN || result > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_ARITHMETIC);
    *scaled = (int32_t)result;
    return 1;
}

int cupidimage_pdf_tt_hint_round(pdf_tt_hint *hint, int32_t value, int32_t *result) {
    int64_t rounded = (int64_t)value + 32;
    rounded = (rounded / 64 - (rounded < 0 && rounded % 64 != 0)) * 64;
    if (rounded < INT32_MIN || rounded > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_ARITHMETIC);
    *result = (int32_t)rounded;
    return 1;
}

int cupidimage_pdf_tt_hint_simple(pdf_tt_hint *hint, unsigned glyph, const pdf_tt_table *input,
                                  size_t *work) {
    pdf_tt_vm *vm = &hint->vm;
    pdf_tt_table view = *input, program;
    unsigned contours = view.length ? tt_hint_u16(view.data) : 0, outline = 0;
    if (view.length && contours > (view.length - 10) / 2)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
    if (contours)
        outline = tt_hint_u16(view.data + 10 + (contours - 1) * 2) + 1;
    unsigned count = outline + 4;
    size_t remaining = *work;
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
            ok = cupidimage_pdf_tt_hint_scale(hint, axis, design[i][axis],
                                              &points[i].original[axis]);
            points[i].current[axis] = points[i].original[axis];
            if (ok && !(vm->prepared_graphics.instruction_control & 1) && i >= outline &&
                axis == (i - outline < 2 ? 0u : 1u))
                ok = cupidimage_pdf_tt_hint_round(hint, points[i].current[axis],
                                                  &points[i].current[axis]);
        }
    }
    if (ok)
        ok = cupidimage_pdf_tt_vm_begin_component(vm, points, count, outline) &&
             cupidimage_pdf_tt_vm_set_design_xy(vm, design, count, vm->size.axis_scale[0],
                                                vm->size.axis_scale[1]) &&
             cupidimage_pdf_tt_vm_run(vm, program.data, program.length, PDF_TT_VM_GLYPH, remaining);
    if (ok)
        *work = vm->work_left;
    free(points);
    free(design);
    return ok;
}
