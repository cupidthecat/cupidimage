#include "internal.h"
#include <limits.h>
#include <string.h>

int cupidimage_pdf_tt_vm_fail(pdf_tt_vm *vm, pdf_tt_vm_error error) {
    if (vm->error == PDF_TT_VM_OK)
        vm->error = error;
    return 0;
}

int cupidimage_pdf_tt_vm_need(pdf_tt_vm *vm, unsigned count) {
    return vm->stack_size >= count || cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_STACK);
}

int cupidimage_pdf_tt_vm_push(pdf_tt_vm *vm, int64_t value) {
    if (value < INT32_MIN || value > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    if (vm->stack_size == vm->stack_limit)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_STACK);
    vm->stack[vm->stack_size++] = (int32_t)value;
    return 1;
}

int cupidimage_pdf_tt_vm_stack_op(pdf_tt_vm *vm, unsigned op) {
    if (op == 0x22) {
        vm->stack_size = 0;
        return 1;
    }
    if (op == 0x24)
        return cupidimage_pdf_tt_vm_push(vm, vm->stack_size);
    unsigned need;
    switch (op) {
        case 0x20:
        case 0x21:
        case 0x25:
        case 0x26:
        case 0x43:
        case 0x45:
        case 0x4f:
        case 0x7e:
        case 0x7f:
        case 0x5c:
        case 0x64:
        case 0x65:
        case 0x66:
        case 0x67:
            need = 1;
            break;
        case 0x23:
        case 0x42:
        case 0x44:
        case 0x50:
        case 0x51:
        case 0x52:
        case 0x53:
        case 0x54:
        case 0x55:
        case 0x5a:
        case 0x5b:
        case 0x60:
        case 0x61:
        case 0x8b:
        case 0x8c:
            need = 2;
            break;
        case 0x8a:
            need = 3;
            break;
        default:
            return -1;
    }
    if (!cupidimage_pdf_tt_vm_need(vm, need))
        return 0;
    int32_t *stack = vm->stack;
    unsigned n = vm->stack_size;
    int64_t a = stack[n - 1], b = need >= 2 ? stack[n - 2] : 0, value;
    switch (op) {
        case 0x20:
            return cupidimage_pdf_tt_vm_push(vm, a);
        case 0x21:
        case 0x4f:
        case 0x7e: /* Retired angle-weight/adjust-angle instructions consume their operand. */
        case 0x7f:
            vm->stack_size--;
            return 1;
        case 0x23:
            stack[n - 1] = (int32_t)b;
            stack[n - 2] = (int32_t)a;
            return 1;
        case 0x25:
        case 0x26: {
            vm->stack_size--;
            if (a <= 0 || (uint64_t)a > vm->stack_size)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_STACK);
            unsigned index = vm->stack_size - (unsigned)a;
            int32_t selected = stack[index];
            if (op == 0x26) {
                if ((uint64_t)a > vm->work_left)
                    return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_BUDGET);
                vm->work_left -= (size_t)a;
                memmove(stack + index, stack + index + 1,
                        (vm->stack_size - index - 1) * sizeof(*stack));
                vm->stack_size--;
            }
            return cupidimage_pdf_tt_vm_push(vm, selected);
        }
        case 0x42:
        case 0x44: {
            unsigned count = op == 0x42 ? vm->storage_count : vm->cvt_count;
            int32_t *destination = op == 0x42 ? vm->storage : vm->cvt;
            if (b < 0 || (uint64_t)b >= count)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_STORAGE);
            if (op == 0x44) {
                if (!cupidimage_pdf_tt_vm_cvt_write(vm, (int32_t)b, (int32_t)a))
                    return 0;
            } else
                destination[b] = (int32_t)a;
            vm->stack_size -= 2;
            return 1;
        }
        case 0x43:
        case 0x45: {
            unsigned count = op == 0x43 ? vm->storage_count : vm->cvt_count;
            const int32_t *source = op == 0x43 ? vm->storage : vm->cvt;
            if (a < 0 || (uint64_t)a >= count)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_STORAGE);
            if (op == 0x45) {
                int32_t value = 0;
                if (!cupidimage_pdf_tt_vm_cvt_read(vm, (int32_t)a, &value))
                    return 0;
                stack[n - 1] = value;
            } else
                stack[n - 1] = source[a];
            return 1;
        }
        case 0x50:
            value = b < a;
            break;
        case 0x51:
            value = b <= a;
            break;
        case 0x52:
            value = b > a;
            break;
        case 0x53:
            value = b >= a;
            break;
        case 0x54:
            value = b == a;
            break;
        case 0x55:
            value = b != a;
            break;
        case 0x5a:
            value = b != 0 && a != 0;
            break;
        case 0x5b:
            value = b != 0 || a != 0;
            break;
        case 0x5c:
            value = a == 0;
            break;
        case 0x60:
            value = b + a;
            break;
        case 0x61:
            value = b - a;
            break;
        case 0x64:
            value = a < 0 ? -a : a;
            break;
        case 0x65:
            value = -a;
            break;
        case 0x66:
            value = a >= 0 ? a / 64 * 64 : -((-a + 63) / 64) * 64;
            break;
        case 0x67:
            value = a >= 0 ? (a + 63) / 64 * 64 : -((-a) / 64) * 64;
            break;
        case 0x8a: {
            int32_t third = stack[n - 3];
            stack[n - 3] = (int32_t)b;
            stack[n - 2] = (int32_t)a;
            stack[n - 1] = third;
            return 1;
        }
        case 0x8b:
            value = b > a ? b : a;
            break;
        case 0x8c:
            value = b < a ? b : a;
            break;
        default:
            return -1;
    }
    vm->stack_size -= need;
    return cupidimage_pdf_tt_vm_push(vm, value);
}
