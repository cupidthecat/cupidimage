#include "internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int cupidimage_pdf_tt_vm_set_design(pdf_tt_vm *vm, const int32_t (*coordinates)[2], unsigned count,
                                    int32_t scale) {
    return cupidimage_pdf_tt_vm_set_design_xy(vm, coordinates, count, scale, scale);
}

int cupidimage_pdf_tt_vm_set_design_xy(pdf_tt_vm *vm, const int32_t (*coordinates)[2],
                                       unsigned count, int32_t x_scale, int32_t y_scale) {
    if (vm->error != PDF_TT_VM_OK)
        return 0;
    if (count != vm->zones[1].count || (count && !coordinates) || x_scale <= 0 || y_scale <= 0)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
    int32_t (*copy)[2] = count ? malloc((size_t)count * sizeof(*copy)) : NULL;
    if (count && !copy)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_MEMORY);
    if (count)
        memcpy(copy, coordinates, (size_t)count * sizeof(*copy));
    free(vm->zones[1].design);
    vm->zones[1].design = copy;
    vm->zones[1].design_scale = x_scale;
    vm->zones[1].design_scale_y = y_scale;
    return 1;
}

int cupidimage_pdf_tt_vm_design_distance(pdf_tt_vm *vm, unsigned za, int32_t a, unsigned zb,
                                         int32_t b, int32_t *result) {
    pdf_tt_vm_point *pa = cupidimage_pdf_tt_vm_get_point(vm, za, a);
    pdf_tt_vm_point *pb = cupidimage_pdf_tt_vm_get_point(vm, zb, b);
    if (!pa || !pb)
        return 0;
    if (za != 1 || zb != 1 || !vm->zones[1].design)
        return cupidimage_pdf_tt_vm_distance(vm, pa->original, pb->original,
                                             vm->graphics.dual_projection, result);
    if (vm->zones[1].design_scale != vm->zones[1].design_scale_y) {
        int32_t delta[2];
        for (unsigned axis = 0; axis < 2; axis++) {
            int64_t distance = (int64_t)vm->zones[1].design[a][axis] - vm->zones[1].design[b][axis];
            int32_t scale = axis ? vm->zones[1].design_scale_y : vm->zones[1].design_scale;
            int64_t value = cupidimage_pdf_tt_vm_divide(distance * scale, 65536);
            if (value < INT32_MIN || value > INT32_MAX)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
            delta[axis] = (int32_t)value;
        }
        return cupidimage_pdf_tt_vm_project(vm, delta, vm->graphics.dual_projection, result);
    }
    /* Measure before scaling: subtracting rounded device coordinates loses
     * design-space differences, especially at small sizes. */
    int32_t distance;
    if (!cupidimage_pdf_tt_vm_distance(vm, vm->zones[1].design[a], vm->zones[1].design[b],
                                       vm->graphics.dual_projection, &distance))
        return 0;
    int64_t scaled =
        cupidimage_pdf_tt_vm_divide((int64_t)distance * vm->zones[1].design_scale, 65536);
    if (scaled < INT32_MIN || scaled > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    *result = (int32_t)scaled;
    return 1;
}
