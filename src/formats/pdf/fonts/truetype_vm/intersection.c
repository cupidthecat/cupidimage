#include "internal.h"
#include <limits.h>

/* Evaluate a cross-product ratio without overflowing its 96-bit numerator.
 * Cross products of signed 32-bit deltas have magnitude below 2^63. */
static int vm_intersection_ratio(pdf_tt_vm *vm, int64_t numerator, int32_t factor,
                                 int64_t denominator, int64_t *result) {
    int negative = (numerator < 0) ^ (factor < 0) ^ (denominator < 0);
    uint64_t n = numerator < 0 ? (uint64_t)-numerator : (uint64_t)numerator;
    uint64_t d = denominator < 0 ? (uint64_t)-denominator : (uint64_t)denominator;
    uint64_t b = factor < 0 ? (uint64_t)-(int64_t)factor : (uint64_t)factor;
    uint64_t whole = n / d, part = n % d, quotient = 0, remainder = 0;
    if (b && whole > UINT32_MAX / b)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    for (unsigned bit = 32; bit-- > 0;) {
        quotient *= 2;
        remainder *= 2;
        if (remainder >= d) {
            remainder -= d;
            quotient++;
        }
        if (b & (UINT64_C(1) << bit)) {
            remainder += part;
            if (remainder >= d) {
                remainder -= d;
                quotient++;
            }
        }
    }
    quotient += whole * b;
    if (remainder >= (d + 1) / 2)
        quotient++;
    if (quotient > UINT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    *result = negative ? -(int64_t)quotient : (int64_t)quotient;
    return 1;
}

int cupidimage_pdf_tt_vm_intersection_op(pdf_tt_vm *vm, unsigned op) {
    if (op != 0x0f)
        return -1;
    if (!cupidimage_pdf_tt_vm_need(vm, 5))
        return 0;
    unsigned *zones = vm->graphics.zone;
    if (zones[0] > 1 || zones[1] > 1 || zones[2] > 1)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
    if (vm->stage == PDF_TT_VM_PREP && (zones[0] || zones[1] || zones[2])) {
        vm->stack_size -= 5;
        return 1;
    }
    int32_t *args = vm->stack + vm->stack_size - 5;
    pdf_tt_vm_point *p = cupidimage_pdf_tt_vm_get_point(vm, zones[2], args[0]);
    pdf_tt_vm_point *a0 = cupidimage_pdf_tt_vm_get_point(vm, zones[1], args[1]);
    pdf_tt_vm_point *a1 = cupidimage_pdf_tt_vm_get_point(vm, zones[1], args[2]);
    pdf_tt_vm_point *b0 = cupidimage_pdf_tt_vm_get_point(vm, zones[0], args[3]);
    pdf_tt_vm_point *b1 = cupidimage_pdf_tt_vm_get_point(vm, zones[0], args[4]);
    if (!p || !a0 || !a1 || !b0 || !b1)
        return 0;
    int32_t da[2], db[2], offset[2], result[2];
    for (unsigned axis = 0; axis < 2; axis++) {
        int64_t a = (int64_t)a1->current[axis] - a0->current[axis];
        int64_t b = (int64_t)b1->current[axis] - b0->current[axis];
        int64_t c = (int64_t)b0->current[axis] - a0->current[axis];
        if (a < INT32_MIN || a > INT32_MAX || b < INT32_MIN || b > INT32_MAX || c < INT32_MIN ||
            c > INT32_MAX)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
        da[axis] = (int32_t)a;
        db[axis] = (int32_t)b;
        offset[axis] = (int32_t)c;
    }
    int64_t cross = (int64_t)da[0] * db[1] - (int64_t)da[1] * db[0];
    int64_t numerator = (int64_t)offset[0] * db[1] - (int64_t)offset[1] * db[0];
    for (unsigned axis = 0; axis < 2; axis++) {
        int64_t value = 0;
        if (!cross)
            value = ((int64_t)a0->current[axis] + a1->current[axis] + b0->current[axis] +
                     b1->current[axis]) /
                    4;
        else {
            if (!vm_intersection_ratio(vm, numerator, da[axis], cross, &value))
                return 0;
            value += a0->current[axis];
        }
        if (value < INT32_MIN || value > INT32_MAX)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
        result[axis] = (int32_t)value;
    }
    p->current[0] = result[0];
    p->current[1] = result[1];
    p->touched = 3;
    vm->stack_size -= 5;
    return 1;
}
