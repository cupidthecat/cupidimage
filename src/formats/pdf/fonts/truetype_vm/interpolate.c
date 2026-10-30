#include "internal.h"
#include <limits.h>

static int vm_ip(pdf_tt_vm *vm) {
    pdf_tt_vm_graphics *g = &vm->graphics;
    unsigned count = g->loop;
    if (!cupidimage_pdf_tt_vm_need(vm, count))
        return 0;
    if (count > vm->work_left)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_BUDGET);
    vm->work_left -= count;
    if (!count ||
        (vm->stage == PDF_TT_VM_PREP && (g->zone[0] == 1 || g->zone[1] == 1 || g->zone[2] == 1))) {
        vm->stack_size -= count;
        g->loop = 1;
        return 1;
    }
    if (g->reference[1] > INT32_MAX || g->reference[2] > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
    pdf_tt_vm_point *a = cupidimage_pdf_tt_vm_get_point(vm, g->zone[0], (int32_t)g->reference[1]);
    pdf_tt_vm_point *b = cupidimage_pdf_tt_vm_get_point(vm, g->zone[1], (int32_t)g->reference[2]);
    if (!a || !b)
        return 0;
    int design = g->zone[0] == 1 && g->zone[1] == 1 && g->zone[2] == 1 && vm->zones[1].design;
    const int32_t *original_a = design ? vm->zones[1].design[g->reference[1]] : a->original;
    const int32_t *original_b = design ? vm->zones[1].design[g->reference[2]] : b->original;
    int scaled_design = design && vm->zones[1].design_scale != vm->zones[1].design_scale_y;
    int32_t old_range, new_range;
    int measured = scaled_design
                       ? cupidimage_pdf_tt_vm_design_distance(vm, 1, (int32_t)g->reference[2], 1,
                                                              (int32_t)g->reference[1], &old_range)
                       : cupidimage_pdf_tt_vm_distance(vm, original_b, original_a,
                                                       g->dual_projection, &old_range);
    if (!measured ||
        !cupidimage_pdf_tt_vm_distance(vm, b->current, a->current, g->projection, &new_range))
        return 0;
    if (!old_range)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    for (unsigned i = 0; i < count; i++) {
        int32_t index = vm->stack[--vm->stack_size];
        pdf_tt_vm_point *point = cupidimage_pdf_tt_vm_get_point(vm, g->zone[2], index);
        if (!point)
            return 0;
        const int32_t *original = design ? vm->zones[1].design[index] : point->original;
        int32_t old_distance, current;
        measured = scaled_design ? cupidimage_pdf_tt_vm_design_distance(
                                       vm, 1, index, 1, (int32_t)g->reference[1], &old_distance)
                                 : cupidimage_pdf_tt_vm_distance(vm, original, original_a,
                                                                 g->dual_projection, &old_distance);
        if (!measured ||
            !cupidimage_pdf_tt_vm_distance(vm, point->current, a->current, g->projection, &current))
            return 0;
        int64_t distance =
            cupidimage_pdf_tt_vm_divide((int64_t)old_distance * new_range, old_range);
        if (distance < INT32_MIN || distance > INT32_MAX)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
        if (!cupidimage_pdf_tt_vm_move(vm, point, distance - current))
            return 0;
    }
    g->loop = 1;
    return 1;
}

static int32_t vm_iup_original(const pdf_tt_vm_zone *zone, unsigned point, unsigned axis) {
    return zone->design ? zone->design[point][axis] : zone->points[point].original[axis];
}

static int vm_iup_point(pdf_tt_vm *vm, unsigned point, unsigned a, unsigned b, unsigned axis) {
    pdf_tt_vm_zone *zone = &vm->zones[1];
    if (vm_iup_original(zone, a, axis) > vm_iup_original(zone, b, axis)) {
        unsigned swap = a;
        a = b;
        b = swap;
    }
    const pdf_tt_vm_point *left = &zone->points[a], *right = &zone->points[b];
    int64_t original = zone->points[point].original[axis], value;
    if (original <= left->original[axis])
        value = original + (int64_t)left->current[axis] - left->original[axis];
    else if (original >= right->original[axis])
        value = original + (int64_t)right->current[axis] - right->original[axis];
    else {
        int64_t range = (int64_t)vm_iup_original(zone, b, axis) - vm_iup_original(zone, a, axis);
        if (!range || left->current[axis] == right->current[axis])
            value = left->current[axis];
        else {
            int64_t span = (int64_t)right->current[axis] - left->current[axis];
            int64_t scale = cupidimage_pdf_tt_vm_divide(span * 65536, range);
            int64_t offset =
                (int64_t)vm_iup_original(zone, point, axis) - vm_iup_original(zone, a, axis);
            if (scale < INT32_MIN || scale > INT32_MAX || offset < INT32_MIN || offset > INT32_MAX)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
            value = left->current[axis] + cupidimage_pdf_tt_vm_divide(offset * scale, 65536);
        }
    }
    if (value < INT32_MIN || value > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    zone->points[point].current[axis] = (int32_t)value;
    return 1;
}

static int vm_iup(pdf_tt_vm *vm, unsigned axis) {
    if (vm->graphics.zone[2] != 1)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
    if (vm->stage == PDF_TT_VM_PREP)
        return 1;
    pdf_tt_vm_zone *zone = &vm->zones[1];
    unsigned count = zone->outline_count;
    /* One bounded scan discovers each contour and its touched anchors; one
     * circular pass interpolates every interval without quadratic searches. */
    if ((size_t)count * 3 > vm->work_left)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_BUDGET);
    vm->work_left -= (size_t)count * 3;
    unsigned start = 0, mask = 1u << axis;
    while (start < count) {
        unsigned end = start, first = count;
        for (;;) {
            if ((zone->points[end].touched & mask) && first == count)
                first = end;
            if (zone->points[end].end)
                break;
            if (++end == count)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
        }
        if (first != count) {
            unsigned anchor = first, cursor = first == end ? start : first + 1;
            do {
                unsigned next = cursor;
                while (next != first && !(zone->points[next].touched & mask))
                    next = next == end ? start : next + 1;
                while (cursor != next) {
                    if (!vm_iup_point(vm, cursor, anchor, next, axis))
                        return 0;
                    cursor = cursor == end ? start : cursor + 1;
                }
                anchor = next;
                cursor = next == end ? start : next + 1;
            } while (anchor != first);
        }
        start = end + 1;
    }
    return 1;
}

int cupidimage_pdf_tt_vm_interpolate_op(pdf_tt_vm *vm, unsigned op) {
    if (op == 0x39)
        return vm_ip(vm);
    if (op == 0x30 || op == 0x31)
        return vm_iup(vm, op == 0x31 ? 0 : 1);
    return -1;
}
