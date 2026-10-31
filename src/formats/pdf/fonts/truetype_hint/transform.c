#include "internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

void cupidimage_pdf_tt_hint_shape_free(pdf_tt_hint_shape *shape) {
    free(shape->points);
    free(shape->scan_modes);
    memset(shape, 0, sizeof(*shape));
}

int cupidimage_pdf_tt_hint_shape_build(pdf_tt_hint *hint, const int32_t matrix[4],
                                       const int32_t offset[2], pdf_tt_hint_shape *shape) {
    pdf_tt_vm *vm = &hint->vm;
    if (!shape)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
    memset(shape, 0, sizeof(*shape));
    if (vm->error != PDF_TT_VM_OK)
        return 0;
    if (!matrix || !offset || vm->stage != PDF_TT_VM_GLYPH)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
    const pdf_tt_vm_zone *zone = &vm->zones[1];
    if (zone->outline_count > 65536 || zone->count != zone->outline_count + 4 || !zone->points)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
    if (!cupidimage_pdf_tt_hint_take(hint, &vm->work_left, zone->outline_count))
        return 0;
    shape->scan_mode = vm->graphics.scan_control ? vm->graphics.scan_type : 2;
    shape->high_precision = vm->size.ppem < 24;
    if (!zone->outline_count)
        return 1;
    pdf_tt_point *points = malloc((size_t)zone->outline_count * sizeof(*points));
    unsigned char *scan_modes = vm->size.grayscale ? NULL : malloc(zone->outline_count);
    if (!points || (!vm->size.grayscale && !scan_modes)) {
        free(points);
        free(scan_modes);
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_MEMORY);
    }
    for (unsigned i = 0; i < zone->outline_count; i++) {
        int64_t x =
            (int64_t)zone->points[i].current[0] - zone->points[zone->outline_count].current[0];
        int64_t y = zone->points[i].current[1];
        int32_t result[2];
        for (unsigned axis = 0; axis < 2; axis++) {
            int64_t value = cupidimage_pdf_tt_vm_divide(x * matrix[axis], 65536) +
                            cupidimage_pdf_tt_vm_divide(y * matrix[axis + 2], 65536) + offset[axis];
            if (value < INT32_MIN || value > INT32_MAX) {
                free(points);
                free(scan_modes);
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
            }
            result[axis] = (int32_t)value;
        }
        if (scan_modes)
            scan_modes[i] = zone->points[i].scan;
        points[i] = (pdf_tt_point){result[0] / 64.0, result[1] / 64.0, zone->points[i].on,
                                   zone->points[i].end};
    }
    shape->points = points;
    shape->scan_modes = scan_modes;
    shape->count = zone->outline_count;
    return 1;
}
