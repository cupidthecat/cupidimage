#include "internal.h"

int cupidimage_pdf_tt_vm_flags_op(pdf_tt_vm *vm, unsigned op) {
    if (op < 0x80 || op > 0x82)
        return -1;
    unsigned count = op == 0x80 ? vm->graphics.loop : 2;
    if (!cupidimage_pdf_tt_vm_need(vm, count))
        return 0;
    unsigned zone = vm->graphics.zone[0];
    if (zone > 1)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
    if (vm->stage == PDF_TT_VM_PREP && zone == 1) {
        vm->stack_size -= count;
        if (op == 0x80)
            vm->graphics.loop = 1;
        return 1;
    }
    if (op == 0x80) {
        if (count > vm->work_left)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_BUDGET);
        vm->work_left -= count;
        for (unsigned i = 0; i < count; i++) {
            pdf_tt_vm_point *point =
                cupidimage_pdf_tt_vm_get_point(vm, zone, vm->stack[--vm->stack_size]);
            if (!point)
                return 0;
            point->on ^= 1;
        }
        vm->graphics.loop = 1;
    } else {
        int32_t low = vm->stack[vm->stack_size - 2], high = vm->stack[vm->stack_size - 1];
        if (!cupidimage_pdf_tt_vm_get_point(vm, zone, low) ||
            !cupidimage_pdf_tt_vm_get_point(vm, zone, high))
            return 0;
        count = low <= high ? (unsigned)(high - low) + 1 : 0;
        if (count > vm->work_left)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_BUDGET);
        vm->work_left -= count;
        for (unsigned i = 0; i < count; i++)
            vm->zones[zone].points[(unsigned)low + i].on = op == 0x81;
        vm->stack_size -= 2;
    }
    return 1;
}
