#include "internal.h"
#include <string.h>

void cupidimage_pdf_tt_vm_graphics_defaults(pdf_tt_vm_graphics *state) {
    memset(state, 0, sizeof(*state));
    state->rounding = (pdf_tt_vm_round){64, 0, 32, 0};
    state->projection[0] = state->freedom[0] = state->dual_projection[0] = 16384;
    state->zone[0] = state->zone[1] = state->zone[2] = 1;
    state->loop = 1;
    state->minimum_distance = 64;
    state->cvt_cut_in = 68;
    state->delta_base = 9;
    state->delta_shift = 3;
    state->auto_flip = 1;
}

int cupidimage_pdf_tt_vm_graphics_op(pdf_tt_vm *vm, unsigned op) {
    pdf_tt_vm_graphics *g = &vm->graphics;
    if (op <= 5) {
        int32_t axis[2] = {op & 1 ? 16384 : 0, op & 1 ? 0 : 16384};
        if (op <= 3) {
            memcpy(g->projection, axis, sizeof(axis));
            memcpy(g->dual_projection, axis, sizeof(axis));
        }
        if (op <= 1 || op >= 4)
            memcpy(g->freedom, axis, sizeof(axis));
        return 1;
    }
    if (op == 0x0c || op == 0x0d) {
        const int32_t *vector = op == 0x0c ? g->projection : g->freedom;
        return cupidimage_pdf_tt_vm_push(vm, vector[0]) && cupidimage_pdf_tt_vm_push(vm, vector[1]);
    }
    if (op == 0x0e) {
        memcpy(g->freedom, g->projection, sizeof(g->freedom));
        return 1;
    }
    if (op == 0x4d || op == 0x4e) {
        g->auto_flip = op == 0x4d;
        return 1;
    }
    switch (op) {
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x14:
        case 0x15:
        case 0x16:
        case 0x17:
        case 0x1a:
        case 0x1d:
        case 0x1e:
        case 0x5e:
        case 0x5f:
            break;
        default:
            return -1;
    }
    if (!cupidimage_pdf_tt_vm_need(vm, 1))
        return 0;
    int32_t value = vm->stack[--vm->stack_size];
    if (op == 0x1a) {
        g->minimum_distance = value;
    } else if (op == 0x1d) {
        g->cvt_cut_in = value;
    } else if (op == 0x1e) {
        g->single_width_cut_in = value;
    } else {
        if (value < 0)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
        if (op <= 0x12) {
            g->reference[op - 0x10] = (uint32_t)value;
        } else if (op <= 0x16) {
            if (value > 1)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
            if (op == 0x16)
                g->zone[0] = g->zone[1] = g->zone[2] = (uint32_t)value;
            else
                g->zone[op - 0x13] = (uint32_t)value;
        } else if (op == 0x17) {
            g->loop = (uint32_t)value;
        } else if (op == 0x5e) {
            g->delta_base = (uint32_t)value;
        } else {
            if (value > 6)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
            g->delta_shift = (uint32_t)value;
        }
    }
    return 1;
}
