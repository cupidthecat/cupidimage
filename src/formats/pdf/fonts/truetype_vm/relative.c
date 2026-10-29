#include "internal.h"
#include <limits.h>
#include <string.h>

static pdf_tt_vm_point *vm_reference_zero(pdf_tt_vm *vm) {
    if (vm->graphics.reference[0] > INT32_MAX) {
        cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
        return NULL;
    }
    return cupidimage_pdf_tt_vm_get_point(vm, vm->graphics.zone[0],
                                          (int32_t)vm->graphics.reference[0]);
}

static int vm_mirp(pdf_tt_vm *vm, unsigned op) {
    if ((op & 3) == 3)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_UNSUPPORTED);
    if (!cupidimage_pdf_tt_vm_need(vm, 2))
        return 0;
    pdf_tt_vm_graphics *g = &vm->graphics;
    if (vm->stage == PDF_TT_VM_PREP && (g->zone[0] == 1 || g->zone[1] == 1)) {
        vm->stack_size -= 2;
        return 1;
    }
    int32_t index = vm->stack[vm->stack_size - 2], cvt = vm->stack[vm->stack_size - 1];
    pdf_tt_vm_point *point = cupidimage_pdf_tt_vm_get_point(vm, g->zone[1], index);
    pdf_tt_vm_point *reference = vm_reference_zero(vm);
    if (!point || !reference)
        return 0;
    /* MIRP's compatibility index -1 denotes an implicit zero, not an array slot. */
    if (cvt < -1 || (cvt >= 0 && (uint32_t)cvt >= vm->cvt_count))
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_STORAGE);
    int32_t cvt_value = 0;
    if (cvt >= 0 && !cupidimage_pdf_tt_vm_cvt_read(vm, cvt, &cvt_value))
        return 0;
    int64_t value = cvt_value;
    int64_t difference = value - g->single_width;
    if (difference < 0)
        difference = -difference;
    if (difference < g->single_width_cut_in)
        value = value < 0 ? -(int64_t)g->single_width : g->single_width;
    if (value < INT32_MIN || value > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    if (g->zone[1] == 0) {
        int32_t original[2];
        for (unsigned axis = 0; axis < 2; axis++) {
            int64_t coordinate = (int64_t)reference->original[axis] +
                                 cupidimage_pdf_tt_vm_divide(value * g->freedom[axis], 16384);
            if (coordinate < INT32_MIN || coordinate > INT32_MAX)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
            original[axis] = (int32_t)coordinate;
        }
        memcpy(point->original, original, sizeof(original));
        memcpy(point->current, original, sizeof(original));
    }
    int32_t original = 0, current = 0;
    if (!cupidimage_pdf_tt_vm_distance(vm, point->original, reference->original, g->dual_projection,
                                       &original) ||
        !cupidimage_pdf_tt_vm_distance(vm, point->current, reference->current, g->projection,
                                       &current))
        return 0;
    if (g->auto_flip && ((original < 0) != (value < 0))) {
        value = -value;
        if (value < INT32_MIN || value > INT32_MAX)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    }
    if (op & 4) {
        difference = value - original;
        if (difference < 0)
            difference = -difference;
        if (g->zone[0] == g->zone[1] && difference > g->cvt_cut_in)
            value = original;
        value = cupidimage_pdf_tt_vm_round(&g->rounding, (int32_t)value);
    }
    if (op & 8) {
        if (original >= 0 && value < g->minimum_distance)
            value = g->minimum_distance;
        else if (original < 0 && value > -(int64_t)g->minimum_distance)
            value = -(int64_t)g->minimum_distance;
    }
    if (value < INT32_MIN || value > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    if (!cupidimage_pdf_tt_vm_move(vm, point, value - current))
        return 0;
    g->reference[1] = g->reference[0];
    g->reference[2] = (uint32_t)index;
    if (op & 16)
        g->reference[0] = (uint32_t)index;
    vm->stack_size -= 2;
    return 1;
}

static int vm_msirp(pdf_tt_vm *vm, unsigned op) {
    if (!cupidimage_pdf_tt_vm_need(vm, 2))
        return 0;
    pdf_tt_vm_graphics *g = &vm->graphics;
    if (vm->stage == PDF_TT_VM_PREP && (g->zone[0] == 1 || g->zone[1] == 1)) {
        vm->stack_size -= 2;
        return 1;
    }
    int32_t index = vm->stack[vm->stack_size - 2], distance = vm->stack[vm->stack_size - 1];
    pdf_tt_vm_point *point = cupidimage_pdf_tt_vm_get_point(vm, g->zone[1], index);
    pdf_tt_vm_point *reference = vm_reference_zero(vm);
    if (!point || !reference)
        return 0;
    if (g->zone[1] == 0) {
        pdf_tt_vm_point created = *point;
        memcpy(created.current, reference->original, sizeof(created.current));
        if (!cupidimage_pdf_tt_vm_move(vm, &created, distance))
            return 0;
        memcpy(point->original, created.current, sizeof(created.current));
        memcpy(point->current, created.current, sizeof(created.current));
    }
    int32_t current = 0;
    if (!cupidimage_pdf_tt_vm_distance(vm, point->current, reference->current, g->projection,
                                       &current) ||
        !cupidimage_pdf_tt_vm_move(vm, point, (int64_t)distance - current))
        return 0;
    g->reference[1] = g->reference[0];
    g->reference[2] = (uint32_t)index;
    if (op & 1)
        g->reference[0] = (uint32_t)index;
    vm->stack_size -= 2;
    return 1;
}

static int vm_alignrp(pdf_tt_vm *vm) {
    unsigned count = vm->graphics.loop;
    if (!cupidimage_pdf_tt_vm_need(vm, count))
        return 0;
    if (count > vm->work_left)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_BUDGET);
    vm->work_left -= count;
    pdf_tt_vm_graphics *g = &vm->graphics;
    if (!count || (vm->stage == PDF_TT_VM_PREP && (g->zone[0] == 1 || g->zone[1] == 1))) {
        vm->stack_size -= count;
        g->loop = 1;
        return 1;
    }
    pdf_tt_vm_point *reference = vm_reference_zero(vm);
    if (!reference)
        return 0;
    for (unsigned i = 0; i < count; i++) {
        pdf_tt_vm_point *point =
            cupidimage_pdf_tt_vm_get_point(vm, g->zone[1], vm->stack[--vm->stack_size]);
        int32_t distance = 0;
        if (!point ||
            !cupidimage_pdf_tt_vm_distance(vm, point->current, reference->current, g->projection,
                                           &distance) ||
            !cupidimage_pdf_tt_vm_move(vm, point, -(int64_t)distance))
            return 0;
    }
    g->loop = 1;
    return 1;
}

int cupidimage_pdf_tt_vm_relative_op(pdf_tt_vm *vm, unsigned op) {
    if (op >= 0xe0 && op <= 0xff)
        return vm_mirp(vm, op);
    if (op == 0x3a || op == 0x3b)
        return vm_msirp(vm, op);
    if (op == 0x3c)
        return vm_alignrp(vm);
    return -1;
}
