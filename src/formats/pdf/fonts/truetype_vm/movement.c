#include "internal.h"
#include <limits.h>
#include <string.h>

static int vm_miap(pdf_tt_vm *vm, unsigned op) {
    if (!cupidimage_pdf_tt_vm_need(vm, 2))
        return 0;
    int32_t index = vm->stack[vm->stack_size - 2], cvt = vm->stack[vm->stack_size - 1];
    pdf_tt_vm_graphics *g = &vm->graphics;
    if (vm->stage == PDF_TT_VM_PREP && g->zone[0] == 1) {
        vm->stack_size -= 2;
        return 1;
    }
    pdf_tt_vm_point *point = cupidimage_pdf_tt_vm_get_point(vm, g->zone[0], index);
    if (!point)
        return 0;
    if (cvt < 0 || (uint32_t)cvt >= vm->cvt_count)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_STORAGE);
    int32_t target = 0;
    if (!cupidimage_pdf_tt_vm_cvt_read(vm, cvt, &target))
        return 0;
    if (g->zone[0] == 0) {
        int32_t original[2];
        for (unsigned axis = 0; axis < 2; axis++) {
            int64_t value = cupidimage_pdf_tt_vm_divide((int64_t)target * g->freedom[axis], 16384);
            if (value < INT32_MIN || value > INT32_MAX)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
            original[axis] = (int32_t)value;
        }
        memcpy(point->original, original, sizeof(original));
        memcpy(point->current, original, sizeof(original));
    }
    int32_t current = 0;
    if (!cupidimage_pdf_tt_vm_project(vm, point->current, g->projection, &current))
        return 0;
    int64_t desired = target;
    if (op & 1) {
        int64_t difference = (int64_t)target - current;
        if (difference < 0)
            difference = -difference;
        if (difference > g->cvt_cut_in)
            desired = current;
        desired = cupidimage_pdf_tt_vm_round(&g->rounding, (int32_t)desired);
        if (desired < INT32_MIN || desired > INT32_MAX)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    }
    if (!cupidimage_pdf_tt_vm_move(vm, point, desired - current))
        return 0;
    g->reference[0] = g->reference[1] = (uint32_t)index;
    vm->stack_size -= 2;
    return 1;
}

static int vm_delta_point(pdf_tt_vm *vm, unsigned band) {
    unsigned ppem;
    if (!cupidimage_pdf_tt_vm_current_ppem(vm, &ppem))
        return 0;
    if (!cupidimage_pdf_tt_vm_need(vm, 1))
        return 0;
    int32_t count = vm->stack[--vm->stack_size];
    if (count < 0 || (uint32_t)count > vm->stack_size / 2)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_STACK);
    if ((uint32_t)count > vm->work_left)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_BUDGET);
    vm->work_left -= (unsigned)count;
    if (vm->graphics.delta_shift > 6)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
    for (int32_t i = 0; i < count; i++) {
        int32_t index = vm->stack[--vm->stack_size];
        unsigned argument = (unsigned)vm->stack[--vm->stack_size] & 255;
        unsigned zone = vm->graphics.zone[0];
        if (vm->stage == PDF_TT_VM_PREP && zone == 1)
            continue;
        pdf_tt_vm_point *point = cupidimage_pdf_tt_vm_get_point(vm, zone, index);
        if (!point)
            return 0;
        uint64_t target = (uint64_t)vm->graphics.delta_base + band * 16 + (argument >> 4);
        if (target == ppem) {
            int step = (int)(argument & 15) - 8;
            if (step >= 0)
                step++;
            int distance = step * (1 << (6 - vm->graphics.delta_shift));
            if (!cupidimage_pdf_tt_vm_move(vm, point, distance))
                return 0;
        }
    }
    return 1;
}

static int vm_shpix(pdf_tt_vm *vm) {
    if (!cupidimage_pdf_tt_vm_need(vm, 1))
        return 0;
    int32_t distance = vm->stack[--vm->stack_size];
    unsigned count = vm->graphics.loop, zone = vm->graphics.zone[2];
    if (!cupidimage_pdf_tt_vm_need(vm, count))
        return 0;
    if (count > vm->work_left)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_BUDGET);
    vm->work_left -= count;
    int64_t x = cupidimage_pdf_tt_vm_divide((int64_t)distance * vm->graphics.freedom[0], 16384);
    int64_t y = cupidimage_pdf_tt_vm_divide((int64_t)distance * vm->graphics.freedom[1], 16384);
    for (unsigned i = 0; i < count; i++) {
        int32_t index = vm->stack[--vm->stack_size];
        if (vm->stage == PDF_TT_VM_PREP && zone == 1)
            continue;
        pdf_tt_vm_point *point = cupidimage_pdf_tt_vm_get_point(vm, zone, index);
        if (!point || !cupidimage_pdf_tt_vm_shift(vm, point, x, y))
            return 0;
    }
    vm->graphics.loop = 1;
    return 1;
}

static int vm_shp(pdf_tt_vm *vm, unsigned op) {
    unsigned count = vm->graphics.loop;
    if (!cupidimage_pdf_tt_vm_need(vm, count))
        return 0;
    if (count > vm->work_left)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_BUDGET);
    vm->work_left -= count;
    unsigned reference_zone = vm->graphics.zone[op & 1 ? 0 : 1];
    unsigned target_zone = vm->graphics.zone[2];
    if (!count || (vm->stage == PDF_TT_VM_PREP && (reference_zone == 1 || target_zone == 1))) {
        vm->stack_size -= count;
        vm->graphics.loop = 1;
        return 1;
    }
    uint32_t index = vm->graphics.reference[op & 1 ? 1 : 2];
    if (index > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
    pdf_tt_vm_point *reference = cupidimage_pdf_tt_vm_get_point(vm, reference_zone, (int32_t)index);
    if (!reference)
        return 0;
    int64_t x = (int64_t)reference->current[0] - reference->original[0];
    int64_t y = (int64_t)reference->current[1] - reference->original[1];
    int64_t distance = cupidimage_pdf_tt_vm_divide(
        x * vm->graphics.projection[0] + y * vm->graphics.projection[1], 16384);
    if (distance < INT32_MIN || distance > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    for (unsigned i = 0; i < count; i++) {
        pdf_tt_vm_point *point =
            cupidimage_pdf_tt_vm_get_point(vm, target_zone, vm->stack[--vm->stack_size]);
        if (!point || !cupidimage_pdf_tt_vm_move(vm, point, distance))
            return 0;
    }
    vm->graphics.loop = 1;
    return 1;
}

static int vm_align_points(pdf_tt_vm *vm) {
    if (!cupidimage_pdf_tt_vm_need(vm, 2))
        return 0;
    unsigned za = vm->graphics.zone[1], zb = vm->graphics.zone[0];
    if (za > 1 || zb > 1)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
    if (vm->stage == PDF_TT_VM_PREP && (za == 1 || zb == 1)) {
        vm->stack_size -= 2;
        return 1;
    }
    pdf_tt_vm_point *a = cupidimage_pdf_tt_vm_get_point(vm, za, vm->stack[vm->stack_size - 2]);
    pdf_tt_vm_point *b = cupidimage_pdf_tt_vm_get_point(vm, zb, vm->stack[vm->stack_size - 1]);
    int32_t distance;
    if (!a || !b ||
        !cupidimage_pdf_tt_vm_distance(vm, b->current, a->current, vm->graphics.projection,
                                       &distance))
        return 0;
    int64_t half = distance / 2;
    if (!cupidimage_pdf_tt_vm_move(vm, a, half) || !cupidimage_pdf_tt_vm_move(vm, b, -half))
        return 0;
    vm->stack_size -= 2;
    return 1;
}

int cupidimage_pdf_tt_vm_movement_op(pdf_tt_vm *vm, unsigned op) {
    if (op == 0x27)
        return vm_align_points(vm);
    if (op == 0x32 || op == 0x33)
        return vm_shp(vm, op);
    if (op == 0x3e || op == 0x3f)
        return vm_miap(vm, op);
    if (op == 0x5d || op == 0x71 || op == 0x72)
        return vm_delta_point(vm, op == 0x5d ? 0 : op - 0x70);
    if (op == 0x38)
        return vm_shpix(vm);
    return -1;
}
