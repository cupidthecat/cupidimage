#include "internal.h"
#include <limits.h>

static int vm_measure(pdf_tt_vm *vm, unsigned op) {
    if (!cupidimage_pdf_tt_vm_need(vm, 2))
        return 0;
    pdf_tt_vm_graphics *g = &vm->graphics;
    int32_t a = vm->stack[vm->stack_size - 2], b = vm->stack[vm->stack_size - 1], distance = 0;
    if (!(vm->stage == PDF_TT_VM_PREP && (g->zone[0] == 1 || g->zone[1] == 1))) {
        if (op == 0x4a) {
            if (!cupidimage_pdf_tt_vm_design_distance(vm, g->zone[0], a, g->zone[1], b, &distance))
                return 0;
        } else {
            pdf_tt_vm_point *pa = cupidimage_pdf_tt_vm_get_point(vm, g->zone[0], a);
            pdf_tt_vm_point *pb = cupidimage_pdf_tt_vm_get_point(vm, g->zone[1], b);
            if (!pa || !pb ||
                !cupidimage_pdf_tt_vm_distance(vm, pa->current, pb->current, g->projection,
                                               &distance))
                return 0;
        }
    }
    vm->stack[vm->stack_size - 2] = distance;
    vm->stack_size--;
    return 1;
}

int cupidimage_pdf_tt_vm_direct_op(pdf_tt_vm *vm, unsigned op) {
    if (op == 0x49 || op == 0x4a)
        return vm_measure(vm, op);
    if (op < 0xc0 || op > 0xdf)
        return -1;
    if ((op & 3) == 3)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_UNSUPPORTED);
    if (!cupidimage_pdf_tt_vm_need(vm, 1))
        return 0;
    pdf_tt_vm_graphics *g = &vm->graphics;
    if (vm->stage == PDF_TT_VM_PREP && (g->zone[0] == 1 || g->zone[1] == 1)) {
        vm->stack_size--;
        return 1;
    }
    if (g->reference[0] > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
    int32_t index = vm->stack[vm->stack_size - 1], reference = (int32_t)g->reference[0];
    int32_t original, current;
    if (!cupidimage_pdf_tt_vm_design_distance(vm, g->zone[1], index, g->zone[0], reference,
                                              &original))
        return 0;
    pdf_tt_vm_point *point = cupidimage_pdf_tt_vm_get_point(vm, g->zone[1], index);
    pdf_tt_vm_point *base = cupidimage_pdf_tt_vm_get_point(vm, g->zone[0], reference);
    if (!point || !base ||
        !cupidimage_pdf_tt_vm_distance(vm, point->current, base->current, g->projection, &current))
        return 0;
    int64_t distance = original, difference = distance - g->single_width;
    if (difference < 0)
        difference = -difference;
    if (difference < g->single_width_cut_in)
        distance = distance < 0 ? -(int64_t)g->single_width : g->single_width;
    if (distance < INT32_MIN || distance > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    /* Minimum distance follows the sign after single-width substitution. */
    int negative = distance < 0;
    if (op & 4)
        distance = cupidimage_pdf_tt_vm_round(&g->rounding, (int32_t)distance);
    if (op & 8) {
        if (!negative && distance < g->minimum_distance)
            distance = g->minimum_distance;
        else if (negative && distance > -(int64_t)g->minimum_distance)
            distance = -(int64_t)g->minimum_distance;
    }
    if (distance < INT32_MIN || distance > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    if (!cupidimage_pdf_tt_vm_move(vm, point, distance - current))
        return 0;
    g->reference[1] = g->reference[0];
    g->reference[2] = (uint32_t)index;
    if (op & 16)
        g->reference[0] = (uint32_t)index;
    vm->stack_size--;
    return 1;
}
