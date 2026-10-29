/* Adapted from FreeType 2.13.3, src/base/ftcalc.c, FT_Vector_NormLen.
 * Copyright (C) 1996-2024 by
 * David Turner, Robert Wilhelm, and Werner Lemberg.
 * Used under the FreeType Project License in third_party/freetype-math/FTL.TXT.
 * Changes and attribution: third_party/freetype-math/README.md. */
#include "internal.h"
#include <limits.h>

static int64_t vm_floor_65536(int64_t value) {
    return value >= 0 ? value / 65536 : -((-value + 65535) / 65536);
}

int cupidimage_pdf_tt_vm_normalize(pdf_tt_vm *vm, int32_t dx, int32_t dy, int32_t result[2]) {
    uint32_t x = (uint32_t)(dx < 0 ? -(int64_t)dx : dx);
    uint32_t y = (uint32_t)(dy < 0 ? -(int64_t)dy : dy);
    if (!x && !y)
        return 1;
    if (!x || !y) {
        result[0] = dx < 0 ? -16384 : dx > 0 ? 16384 : 0;
        result[1] = dy < 0 ? -16384 : dy > 0 ? 16384 : 0;
        return 1;
    }
    uint32_t length = x > y ? x + (y >> 1) : y + (x >> 1);
    unsigned high_bit = 0;
    for (uint32_t bits = length; bits > 1; bits >>= 1)
        high_bit++;
    int shift = 31 - (int)high_bit;
    shift -= 15 + (length >= (UINT32_C(0xaaaaaaaa) >> shift));
    if (shift > 0) {
        x <<= shift;
        y <<= shift;
        length = x > y ? x + (y >> 1) : y + (x >> 1);
    } else {
        x >>= -shift;
        y >>= -shift;
        length >>= -shift;
    }
    int64_t correction = 65536 - (int64_t)length;
    uint32_t u = 0, v = 0;
    for (unsigned iteration = 0; iteration < 32; iteration++) {
        if (correction <= -65536 || correction > 65536)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
        int64_t next_x = x + vm_floor_65536((int64_t)x * correction);
        int64_t next_y = y + vm_floor_65536((int64_t)y * correction);
        if (next_x < 0 || next_x > UINT32_MAX || next_y < 0 || next_y > UINT32_MAX)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
        u = (uint32_t)next_x;
        v = (uint32_t)next_y;
        uint32_t squared = u * u + v * v;
        int64_t signed_squared =
            squared <= INT32_MAX ? (int64_t)squared : (int64_t)squared - INT64_C(4294967296);
        int64_t adjustment = -signed_squared / 512;
        adjustment = adjustment * ((65536 + correction) / 256) / 65536;
        correction += adjustment;
        if (adjustment <= 0) {
            if (u > 65539 || v > 65539)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
            int32_t nx = (int32_t)(u / 4), ny = (int32_t)(v / 4);
            result[0] = dx < 0 ? -nx : nx;
            result[1] = dy < 0 ? -ny : ny;
            return 1;
        }
    }
    return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
}

int cupidimage_pdf_tt_vm_vector_op(pdf_tt_vm *vm, unsigned op) {
    if (op != 0x0a && op != 0x0b)
        return -1;
    if (!cupidimage_pdf_tt_vm_need(vm, 2))
        return 0;
    int32_t values[2];
    for (unsigned i = 0; i < 2; i++) {
        unsigned raw = (unsigned)vm->stack[vm->stack_size - 2 + i] & 65535;
        values[i] = raw >= 32768 ? (int32_t)raw - 65536 : (int32_t)raw;
    }
    int32_t *vector = op == 0x0a ? vm->graphics.projection : vm->graphics.freedom;
    if (!cupidimage_pdf_tt_vm_normalize(vm, values[0], values[1], vector))
        return 0;
    if (op == 0x0a) {
        vm->graphics.dual_projection[0] = vector[0];
        vm->graphics.dual_projection[1] = vector[1];
    }
    vm->stack_size -= 2;
    return 1;
}
