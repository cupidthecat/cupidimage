#include "internal.h"
#include <limits.h>

/* Nearest integer square root, with no floating-point or signed overflow. */
static uint64_t vm_ratio_root(uint64_t value) {
    uint64_t remaining = value, root = 0, bit = UINT64_C(1) << 62;
    while (bit > remaining)
        bit >>= 2;
    while (bit) {
        if (remaining >= root + bit) {
            remaining -= root + bit;
            root = (root >> 1) + bit;
        } else
            root >>= 1;
        bit >>= 2;
    }
    return root + (value - root * root > root);
}

static int vm_cvt_ratio(pdf_tt_vm *vm, int32_t *ratio) {
    if (!vm->size.configured || vm->size.axis_ratio[0] == vm->size.axis_ratio[1]) {
        *ratio = 65536;
        return 1;
    }
    if (!vm->graphics.projection[1]) {
        *ratio = vm->size.axis_ratio[0];
        return *ratio > 0 || cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    }
    if (!vm->graphics.projection[0]) {
        *ratio = vm->size.axis_ratio[1];
        return *ratio > 0 || cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    }
    int64_t values[2];
    for (unsigned axis = 0; axis < 2; axis++) {
        values[axis] = cupidimage_pdf_tt_vm_divide(
            (int64_t)vm->size.axis_ratio[axis] * vm->graphics.projection[axis], 16384);
        if (values[axis] < -65536 || values[axis] > 65536)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    }
    uint64_t length = vm_ratio_root((uint64_t)(values[0] * values[0] + values[1] * values[1]));
    if (!length || length > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    *ratio = (int32_t)length;
    return 1;
}

int cupidimage_pdf_tt_vm_current_ppem(pdf_tt_vm *vm, unsigned *ppem) {
    if (!vm->size.configured)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_SIZE);
    int32_t ratio = 0;
    if (!vm_cvt_ratio(vm, &ratio))
        return 0;
    *ppem = (unsigned)cupidimage_pdf_tt_vm_divide((int64_t)vm->size.ppem * ratio, 65536);
    return 1;
}

static int vm_cvt_convert(pdf_tt_vm *vm, int32_t value, int inverse, int32_t *result) {
    int32_t ratio = 0;
    if (!vm_cvt_ratio(vm, &ratio))
        return 0;
    int64_t converted = inverse ? cupidimage_pdf_tt_vm_divide((int64_t)value * 65536, ratio)
                                : cupidimage_pdf_tt_vm_divide((int64_t)value * ratio, 65536);
    if (converted < INT32_MIN || converted > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    *result = (int32_t)converted;
    return 1;
}

static int vm_cvt_index(pdf_tt_vm *vm, int32_t index) {
    return (index >= 0 && (uint32_t)index < vm->cvt_count) ||
           cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_STORAGE);
}

int cupidimage_pdf_tt_vm_cvt_read(pdf_tt_vm *vm, int32_t index, int32_t *result) {
    return vm_cvt_index(vm, index) && vm_cvt_convert(vm, vm->cvt[index], 0, result);
}

int cupidimage_pdf_tt_vm_cvt_write(pdf_tt_vm *vm, int32_t index, int32_t value) {
    return vm_cvt_index(vm, index) && vm_cvt_convert(vm, value, 1, &vm->cvt[index]);
}

int cupidimage_pdf_tt_vm_cvt_delta(pdf_tt_vm *vm, int32_t index, int32_t delta) {
    int32_t converted = 0;
    if (!vm_cvt_index(vm, index) || !vm_cvt_convert(vm, delta, 1, &converted))
        return 0;
    int64_t value = (int64_t)vm->cvt[index] + converted;
    if (value < INT32_MIN || value > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    vm->cvt[index] = (int32_t)value;
    return 1;
}
