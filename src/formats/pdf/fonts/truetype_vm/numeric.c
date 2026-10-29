#include "internal.h"

/* Distances are F26Dot6. This VM uses zero engine-color compensation. */
static int64_t vm_round(const pdf_tt_vm_round *rounding, int32_t value) {
    int64_t magnitude = value < 0 ? -(int64_t)value : value;
    if (rounding->off)
        return value;
    int64_t shifted = magnitude - rounding->phase + rounding->threshold;
    int64_t steps = shifted >= 0 ? shifted / rounding->period
                                 : -((-shifted + rounding->period - 1) / rounding->period);
    int64_t result = steps * rounding->period + rounding->phase;
    if (result < 0)
        result = rounding->phase;
    return value < 0 ? -result : result;
}

int cupidimage_pdf_tt_vm_numeric_op(pdf_tt_vm *vm, unsigned op) {
    pdf_tt_vm_round *r = &vm->graphics.rounding;
    switch (op) {
        case 0x18: /* RTG */
            *r = (pdf_tt_vm_round){64, 0, 32, 0};
            return 1;
        case 0x19: /* RTHG */
            *r = (pdf_tt_vm_round){64, 32, 32, 0};
            return 1;
        case 0x3d: /* RTDG */
            *r = (pdf_tt_vm_round){32, 0, 16, 0};
            return 1;
        case 0x7a: /* ROFF */
            r->off = 1;
            return 1;
        case 0x7c: /* RUTG */
            *r = (pdf_tt_vm_round){64, 0, 63, 0};
            return 1;
        case 0x7d: /* RDTG */
            *r = (pdf_tt_vm_round){64, 0, 0, 0};
            return 1;
        case 0x76: { /* SROUND */
            if (!cupidimage_pdf_tt_vm_need(vm, 1))
                return 0;
            unsigned selector = (unsigned)vm->stack[--vm->stack_size] & 255;
            unsigned period_bits = selector >> 6;
            if (period_bits == 3)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
            int period = 32 << period_bits;
            int threshold = (int)(selector & 15);
            *r = (pdf_tt_vm_round){period, (int)((selector >> 4) & 3) * period / 4,
                                   threshold ? (threshold - 4) * period / 8 : period - 1, 0};
            return 1;
        }
        default:
            break;
    }
    unsigned operands;
    switch (op) {
        case 0x62:
        case 0x63:
            operands = 2;
            break;
        case 0x56:
        case 0x57:
        case 0x68:
        case 0x69:
        case 0x6a:
        case 0x6c:
        case 0x6d:
        case 0x6e:
            operands = 1;
            break;
        default:
            return -1;
    }
    if (!cupidimage_pdf_tt_vm_need(vm, operands))
        return 0;
    int64_t a = vm->stack[vm->stack_size - 1], value;
    if (op == 0x62) {
        if (!a)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
        value = (int64_t)vm->stack[vm->stack_size - 2] * 64 / a;
    } else if (op == 0x63) {
        int64_t product = (int64_t)vm->stack[vm->stack_size - 2] * a;
        /* Quantize the product to nearest F26Dot6, with ties away from zero. */
        value = product < 0 ? -((-product + 32) / 64) : (product + 32) / 64;
    } else if (op >= 0x6c) {
        value = a;
    } else {
        value = vm_round(r, (int32_t)a);
        if (op == 0x56 || op == 0x57) {
            unsigned fraction = (unsigned)((uint64_t)value & 127);
            value = fraction == (op == 0x56 ? 64u : 0u);
        }
    }
    vm->stack_size -= operands;
    return cupidimage_pdf_tt_vm_push(vm, value);
}
