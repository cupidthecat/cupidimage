#include "internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int vm_scale(pdf_tt_vm *vm, int32_t value, int32_t *scaled) {
    if (!vm->size.configured)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_SIZE);
    int64_t product = (int64_t)value * vm->size.scale;
    int64_t result = product < 0 ? -((-product + 32768) / 65536) : (product + 32768) / 65536;
    if (result < INT32_MIN || result > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    *scaled = (int32_t)result;
    return 1;
}

int cupidimage_pdf_tt_vm_set_size(pdf_tt_vm *vm, unsigned units, int32_t pixels, int32_t points,
                                  int grayscale, const int32_t *font_cvt) {
    return cupidimage_pdf_tt_vm_set_size_xy(vm, units, pixels, pixels, points, grayscale, 0,
                                            font_cvt);
}

int cupidimage_pdf_tt_vm_set_size_xy(pdf_tt_vm *vm, unsigned units, int32_t x_pixels,
                                     int32_t y_pixels, int32_t points, int grayscale,
                                     unsigned transform_flags, const int32_t *font_cvt) {
    if (vm->error != PDF_TT_VM_OK)
        return 0;
    if (units < 16 || units > 16384 || x_pixels <= 0 || y_pixels <= 0 || x_pixels > 32767 * 64 ||
        y_pixels > 32767 * 64 || points <= 0 ||
        (transform_flags & ~(PDF_TT_VM_ROTATED | PDF_TT_VM_STRETCHED)) ||
        (vm->cvt_count && !font_cvt))
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_SIZE);
    int32_t pixels[2] = {x_pixels, y_pixels};
    unsigned ppem[2] = {(unsigned)(x_pixels + 32) / 64, (unsigned)(y_pixels + 32) / 64};
    unsigned major = ppem[0] >= ppem[1] ? 0 : 1;
    pdf_tt_vm_size_state size = {0};
    for (unsigned axis = 0; axis < 2; axis++) {
        int64_t scale = ((int64_t)pixels[axis] * 65536 + units / 2) / units;
        /* CVT projection uses integer ppem ratios; outline scales retain the
         * requested fractions unless the font's head flags require rounding. */
        int64_t ratio =
            ppem[major] ? ((int64_t)ppem[axis] * 65536 + ppem[major] / 2) / ppem[major] : 65536;
        if (scale <= 0 || scale > INT32_MAX || ratio <= 0)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_SIZE);
        size.axis_scale[axis] = (int32_t)scale;
        size.axis_ratio[axis] = (int32_t)ratio;
    }
    size.scale = size.axis_scale[major];
    size.point_size = points;
    size.ppem = ppem[major];
    size.configured = 1;
    size.grayscale = !!grayscale;
    size.transform_flags = transform_flags;
    vm->size = size;
    for (unsigned i = 0; i < vm->cvt_count; i++)
        if (!vm_scale(vm, font_cvt[i], &vm->cvt[i]))
            return 0;
    cupidimage_pdf_tt_vm_graphics_defaults(&vm->graphics);
    vm->prepared = 0;
    vm->stage = PDF_TT_VM_FONT;
    vm->stack_size = 0;
    if (vm->storage_count)
        memset(vm->storage, 0, (size_t)vm->storage_count * sizeof(*vm->storage));
    if (vm->zones[0].count)
        memset(vm->zones[0].points, 0, (size_t)vm->zones[0].count * sizeof(*vm->zones[0].points));
    free(vm->zones[1].points);
    free(vm->zones[1].design);
    vm->zones[1] = (pdf_tt_vm_zone){0};
    return 1;
}

static int vm_delta_cvt(pdf_tt_vm *vm, unsigned op) {
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
        if (index < 0 || (uint32_t)index >= vm->cvt_count)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_STORAGE);
        uint64_t target = (uint64_t)vm->graphics.delta_base + (op - 0x73) * 16 + (argument >> 4);
        if (target == ppem) {
            int step = (int)(argument & 15) - 8;
            if (step >= 0)
                step++;
            if (!cupidimage_pdf_tt_vm_cvt_delta(vm, index,
                                                step * (1 << (6 - vm->graphics.delta_shift))))
                return 0;
        }
    }
    return 1;
}

int cupidimage_pdf_tt_vm_size_op(pdf_tt_vm *vm, unsigned op) {
    unsigned count;
    switch (op) {
        case 0x4b:
        case 0x4c:
            if (!vm->size.configured)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_SIZE);
            if (op == 0x4b) {
                unsigned ppem;
                return cupidimage_pdf_tt_vm_current_ppem(vm, &ppem) &&
                       cupidimage_pdf_tt_vm_push(vm, ppem);
            }
            return cupidimage_pdf_tt_vm_push(vm, vm->size.point_size);
        case 0x73:
        case 0x74:
        case 0x75:
            if (!vm->size.configured)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_SIZE);
            return vm_delta_cvt(vm, op);
        case 0x1f:
        case 0x85:
        case 0x88:
        case 0x8d:
            count = 1;
            break;
        case 0x70:
        case 0x8e:
            count = 2;
            break;
        default:
            return -1;
    }
    if (!cupidimage_pdf_tt_vm_need(vm, count))
        return 0;
    int32_t a = vm->stack[vm->stack_size - 1];
    int32_t b = count == 2 ? vm->stack[vm->stack_size - 2] : 0;
    if (op == 0x88) {
        /* Legacy grayscale configuration; no variations or LCD modes advertised. */
        unsigned info = (a & 1) ? 35 : 0;
        if ((a & 2) && (vm->size.transform_flags & PDF_TT_VM_ROTATED))
            info |= 256;
        if ((a & 4) && (vm->size.transform_flags & PDF_TT_VM_STRETCHED))
            info |= 512;
        if ((a & 32) && vm->size.grayscale)
            info |= 4096;
        vm->stack[vm->stack_size - 1] = (int32_t)info;
        return 1;
    }
    if (op == 0x1f) {
        if (!vm_scale(vm, a, &vm->graphics.single_width))
            return 0;
    } else if (op == 0x70) {
        if (b < 0 || (uint32_t)b >= vm->cvt_count)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_STORAGE);
        if (!vm_scale(vm, a, &vm->cvt[b]))
            return 0;
    } else if (op == 0x85) {
        /* Preserve flags for the future scan converter, rather than dropping them. */
        vm->graphics.scan_flags = (unsigned)a & 65535;
    } else if (op == 0x8d) {
        if (a < 0 || a > 7)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
        vm->graphics.scan_type = (unsigned)a;
    } else {
        if (a < 1 || a > 3 || (b != 0 && b != (1 << (a - 1))))
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
        if (vm->stage == PDF_TT_VM_PREP || (vm->stage == PDF_TT_VM_GLYPH && a == 3)) {
            unsigned mask = 1u << (a - 1);
            vm->graphics.instruction_control =
                (vm->graphics.instruction_control & ~mask) | (unsigned)b;
        }
    }
    vm->stack_size -= count;
    return 1;
}
