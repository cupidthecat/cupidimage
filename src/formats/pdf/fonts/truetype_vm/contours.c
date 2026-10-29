#include "internal.h"
#include <limits.h>

static int vm_shift_zone(pdf_tt_vm *vm, unsigned op) {
    if (!cupidimage_pdf_tt_vm_need(vm, 1))
        return 0;
    int32_t target = vm->stack[vm->stack_size - 1];
    unsigned reference_zone = vm->graphics.zone[op & 1 ? 0 : 1];
    uint32_t reference_index = vm->graphics.reference[op & 1 ? 1 : 2];
    if (target < 0 || target > 1 || reference_zone > 1 || reference_index > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
    if (vm->stage == PDF_TT_VM_PREP && (target == 1 || reference_zone == 1)) {
        vm->stack_size--;
        return 1;
    }
    pdf_tt_vm_zone *zone = &vm->zones[target];
    unsigned count = target ? zone->outline_count : zone->count;
    if (count > vm->work_left)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_BUDGET);
    vm->work_left -= count;
    pdf_tt_vm_point *reference =
        cupidimage_pdf_tt_vm_get_point(vm, reference_zone, (int32_t)reference_index);
    int32_t distance;
    if (!reference || !cupidimage_pdf_tt_vm_distance(vm, reference->current, reference->original,
                                                     vm->graphics.projection, &distance))
        return 0;
    for (unsigned i = 0; i < count; i++) {
        if ((unsigned)target == reference_zone && i == reference_index)
            continue;
        unsigned char touched = zone->points[i].touched;
        if (!cupidimage_pdf_tt_vm_move(vm, &zone->points[i], distance))
            return 0;
        zone->points[i].touched = touched;
    }
    vm->stack_size--;
    return 1;
}

int cupidimage_pdf_tt_vm_contour_op(pdf_tt_vm *vm, unsigned op) {
    if (op == 0x36 || op == 0x37)
        return vm_shift_zone(vm, op);
    if (op != 0x34 && op != 0x35)
        return -1;
    if (!cupidimage_pdf_tt_vm_need(vm, 1))
        return 0;
    int32_t contour = vm->stack[vm->stack_size - 1];
    unsigned target_zone = vm->graphics.zone[2], reference_zone = vm->graphics.zone[op & 1 ? 0 : 1];
    uint32_t reference_index = vm->graphics.reference[op & 1 ? 1 : 2];
    if (contour < 0 || target_zone > 1 || reference_zone > 1 || reference_index > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
    if (vm->stage == PDF_TT_VM_PREP && (target_zone == 1 || reference_zone == 1)) {
        vm->stack_size--;
        return 1;
    }
    pdf_tt_vm_zone *zone = &vm->zones[target_zone];
    unsigned start = 0, limit = zone->count;
    if (target_zone == 0) {
        /* Twilight's virtual contour zero consists of all allocated points. */
        if (contour != 0)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
    } else {
        unsigned current = 0;
        int found = 0;
        for (unsigned i = 0; i < zone->outline_count; i++) {
            if (!vm->work_left)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_BUDGET);
            vm->work_left--;
            if (zone->points[i].end) {
                if (current == (unsigned)contour) {
                    limit = i + 1;
                    found = 1;
                    break;
                }
                current++;
                start = i + 1;
            }
        }
        if (!found)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
    }
    if (limit - start > vm->work_left)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_BUDGET);
    vm->work_left -= limit - start;
    pdf_tt_vm_point *reference =
        cupidimage_pdf_tt_vm_get_point(vm, reference_zone, (int32_t)reference_index);
    int32_t distance;
    if (!reference || !cupidimage_pdf_tt_vm_distance(vm, reference->current, reference->original,
                                                     vm->graphics.projection, &distance))
        return 0;
    for (unsigned i = start; i < limit; i++)
        if ((reference_zone != target_zone || reference_index != i) &&
            !cupidimage_pdf_tt_vm_move(vm, &zone->points[i], distance))
            return 0;
    vm->stack_size--;
    return 1;
}
