#include "internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

int cupidimage_pdf_tt_vm_set_zones(pdf_tt_vm *vm, unsigned twilight, const pdf_tt_vm_point *glyph,
                                   unsigned count, unsigned outline) {
    if (vm->error != PDF_TT_VM_OK)
        return 0;
    if (twilight > 65535 || count > 65540 || outline > count || outline > 65536 ||
        count - outline > 4 || (count && !glyph))
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
    pdf_tt_vm_point *zero = twilight ? calloc(twilight, sizeof(*zero)) : NULL;
    pdf_tt_vm_point *one = count ? malloc((size_t)count * sizeof(*one)) : NULL;
    if ((twilight && !zero) || (count && !one)) {
        free(zero);
        free(one);
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_MEMORY);
    }
    if (count)
        memcpy(one, glyph, (size_t)count * sizeof(*one));
    free(vm->zones[0].design);
    free(vm->zones[1].design);
    free(vm->zones[0].points);
    free(vm->zones[1].points);
    vm->zones[0] = (pdf_tt_vm_zone){.points = zero, .count = twilight};
    vm->zones[1] = (pdf_tt_vm_zone){.points = one, .count = count, .outline_count = outline};
    return 1;
}

int cupidimage_pdf_tt_vm_load_glyph_points(pdf_tt_vm *vm, const pdf_tt_vm_point *glyph,
                                           unsigned count, unsigned outline) {
    if (vm->error != PDF_TT_VM_OK)
        return 0;
    if (count > 65540 || outline > count || outline > 65536 || count - outline > 4 ||
        (count && !glyph))
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
    pdf_tt_vm_point *points = count ? malloc((size_t)count * sizeof(*points)) : NULL;
    if (count && !points)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_MEMORY);
    if (count)
        memcpy(points, glyph, (size_t)count * sizeof(*points));
    free(vm->zones[1].points);
    free(vm->zones[1].design);
    vm->zones[1] = (pdf_tt_vm_zone){.points = points, .count = count, .outline_count = outline};
    return 1;
}

pdf_tt_vm_point *cupidimage_pdf_tt_vm_get_point(pdf_tt_vm *vm, unsigned zone, int32_t index) {
    if (zone > 1 || index < 0 || (uint32_t)index >= vm->zones[zone].count) {
        cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_POINT);
        return NULL;
    }
    return &vm->zones[zone].points[index];
}

int64_t cupidimage_pdf_tt_vm_divide(int64_t numerator, int64_t denominator) {
    int negative = (numerator < 0) != (denominator < 0);
    uint64_t n = (uint64_t)(numerator < 0 ? -numerator : numerator);
    uint64_t d = (uint64_t)(denominator < 0 ? -denominator : denominator);
    int64_t value = (int64_t)((n + d / 2) / d);
    return negative ? -value : value;
}

int cupidimage_pdf_tt_vm_project(pdf_tt_vm *vm, const int32_t point[2], const int32_t vector[2],
                                 int32_t *result) {
    int64_t dot = (int64_t)point[0] * vector[0] + (int64_t)point[1] * vector[1];
    int64_t value = cupidimage_pdf_tt_vm_divide(dot, 16384);
    if (value < INT32_MIN || value > INT32_MAX) {
        cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
        return 0;
    }
    *result = (int32_t)value;
    return 1;
}

int cupidimage_pdf_tt_vm_distance(pdf_tt_vm *vm, const int32_t a[2], const int32_t b[2],
                                  const int32_t vector[2], int32_t *result) {
    int64_t x = (int64_t)a[0] - b[0], y = (int64_t)a[1] - b[1];
    int64_t value = cupidimage_pdf_tt_vm_divide(x * vector[0] + y * vector[1], 16384);
    if (value < INT32_MIN || value > INT32_MAX) {
        cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
        return 0;
    }
    *result = (int32_t)value;
    return 1;
}

int cupidimage_pdf_tt_vm_move(pdf_tt_vm *vm, pdf_tt_vm_point *point, int64_t distance) {
    const int32_t *freedom = vm->graphics.freedom, *projection = vm->graphics.projection;
    int64_t product = (int64_t)freedom[0] * projection[0] + (int64_t)freedom[1] * projection[1];
    /* The coupling coefficient drops the low 14 bits. Use explicit floor
     * division rather than implementation-defined signed right shift. */
    int64_t dot = product / 16384 - (product < 0 && product % 16384 != 0);
    if (!dot && distance)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    int64_t x = distance ? cupidimage_pdf_tt_vm_divide(distance * freedom[0], dot) : 0;
    int64_t y = distance ? cupidimage_pdf_tt_vm_divide(distance * freedom[1], dot) : 0;
    return cupidimage_pdf_tt_vm_shift(vm, point, x, y);
}

int cupidimage_pdf_tt_vm_shift(pdf_tt_vm *vm, pdf_tt_vm_point *point, int64_t x, int64_t y) {
    if (x < -INT64_C(4294967295) || x > INT64_C(4294967295) || y < -INT64_C(4294967295) ||
        y > INT64_C(4294967295))
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    x += point->current[0];
    y += point->current[1];
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    point->current[0] = (int32_t)x;
    point->current[1] = (int32_t)y;
    point->touched |=
        (vm->graphics.freedom[0] != 0 ? 1 : 0) | (vm->graphics.freedom[1] != 0 ? 2 : 0);
    return 1;
}

static int vm_line_vector(pdf_tt_vm *vm, const int32_t from[2], const int32_t to[2],
                          int perpendicular, int32_t vector[2]) {
    int64_t x = (int64_t)to[0] - from[0], y = (int64_t)to[1] - from[1];
    if (!x && !y) {
        vector[0] = 16384;
        vector[1] = 0;
        return 1;
    }
    if (perpendicular) {
        int64_t prior_x = x;
        x = -y;
        y = prior_x;
    }
    if (x < INT32_MIN || x > INT32_MAX || y < INT32_MIN || y > INT32_MAX)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
    return cupidimage_pdf_tt_vm_normalize(vm, (int32_t)x, (int32_t)y, vector);
}

int cupidimage_pdf_tt_vm_point_op(pdf_tt_vm *vm, unsigned op) {
    unsigned count;
    if ((op >= 0x06 && op <= 0x09) || op == 0x86 || op == 0x87 || op == 0x48)
        count = 2;
    else if (op == 0x29 || op == 0x2e || op == 0x2f || op == 0x46 || op == 0x47)
        count = 1;
    else
        return -1;
    if (!cupidimage_pdf_tt_vm_need(vm, count))
        return 0;
    int32_t a = vm->stack[vm->stack_size - 1];
    int32_t b = count == 2 ? vm->stack[vm->stack_size - 2] : 0;
    pdf_tt_vm_graphics *g = &vm->graphics;
    /* No glyph outline is associated with the preparation program. */
    if (vm->stage == PDF_TT_VM_PREP) {
        int missing_glyph;
        if (op <= 0x09 || op == 0x86 || op == 0x87)
            missing_glyph = g->zone[1] == 1 || g->zone[2] == 1;
        else if (op == 0x46 || op == 0x47 || op == 0x48)
            missing_glyph = g->zone[2] == 1;
        else
            missing_glyph = g->zone[0] == 1;
        if (missing_glyph) {
            if (op == 0x46 || op == 0x47)
                vm->stack[vm->stack_size - 1] = 0;
            else
                vm->stack_size -= count;
            return 1;
        }
    }
    if (op <= 0x09 || op == 0x86 || op == 0x87) {
        pdf_tt_vm_point *from = cupidimage_pdf_tt_vm_get_point(vm, g->zone[2], a);
        pdf_tt_vm_point *to = cupidimage_pdf_tt_vm_get_point(vm, g->zone[1], b);
        if (!from || !to)
            return 0;
        int perpendicular = op & 1;
        if ((op == 0x86 || op == 0x87) && from->original[0] == to->original[0] &&
            from->original[1] == to->original[1])
            perpendicular = 0;
        int32_t *vector = op == 0x08 || op == 0x09 ? g->freedom : g->projection;
        if (!vm_line_vector(vm, from->current, to->current, perpendicular, vector))
            return 0;
        if (op == 0x86 || op == 0x87) {
            if (!vm_line_vector(vm, from->original, to->original, op & 1, g->dual_projection))
                return 0;
        } else if (op <= 0x07) {
            memcpy(g->dual_projection, g->projection, sizeof(g->projection));
        }
    } else if (op == 0x46 || op == 0x47) {
        pdf_tt_vm_point *point = cupidimage_pdf_tt_vm_get_point(vm, g->zone[2], a);
        int32_t value;
        if (!point ||
            !cupidimage_pdf_tt_vm_project(vm, op & 1 ? point->original : point->current,
                                          op & 1 ? g->dual_projection : g->projection, &value))
            return 0;
        vm->stack[vm->stack_size - 1] = value;
        return 1;
    } else if (op == 0x48) {
        pdf_tt_vm_point *point = cupidimage_pdf_tt_vm_get_point(vm, g->zone[2], b);
        int32_t current;
        if (!point || !cupidimage_pdf_tt_vm_project(vm, point->current, g->projection, &current) ||
            !cupidimage_pdf_tt_vm_move(vm, point, (int64_t)a - current))
            return 0;
        if (!g->zone[2])
            memcpy(point->original, point->current, sizeof(point->current));
    } else {
        pdf_tt_vm_point *point = cupidimage_pdf_tt_vm_get_point(vm, g->zone[0], a);
        if (!point)
            return 0;
        if (op == 0x29) {
            point->touched &=
                (unsigned char)~((g->freedom[0] != 0 ? 1 : 0) | (g->freedom[1] != 0 ? 2 : 0));
        } else {
            int32_t current = 0;
            int64_t distance = 0;
            if (op & 1) {
                if (!cupidimage_pdf_tt_vm_project(vm, point->current, g->projection, &current))
                    return 0;
                int64_t rounded = cupidimage_pdf_tt_vm_round(&g->rounding, current);
                if (rounded < INT32_MIN || rounded > INT32_MAX)
                    return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_ARITHMETIC);
                distance = rounded - current;
            }
            if (!cupidimage_pdf_tt_vm_move(vm, point, distance))
                return 0;
            g->reference[0] = g->reference[1] = (uint32_t)a;
        }
    }
    vm->stack_size -= count;
    return 1;
}
