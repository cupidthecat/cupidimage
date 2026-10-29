#include "gridfit.h"
#include <limits.h>
#include <math.h>

static int64_t ps_transform_round(int64_t value) {
    return value < 0 ? -((-value + 32768) / 65536) : (value + 32768) / 65536;
}

static int ps_transform_point(const double point[2], const int32_t matrix[4],
                              const int32_t offset[2], double result[2]) {
    int64_t p[2];
    for (unsigned i = 0; i < 2; i++) {
        double value = point[i] * 64;
        if (!isfinite(value) || value < INT32_MIN || value > INT32_MAX || floor(value) != value)
            return 0;
        p[i] = (int32_t)value;
    }
    for (unsigned axis = 0; axis < 2; axis++) {
        int64_t value = ps_transform_round(p[0] * matrix[axis]) +
                        ps_transform_round(p[1] * matrix[axis + 2]) + offset[axis];
        if (value < INT32_MIN || value > INT32_MAX)
            return 0;
        result[axis] = value / 64.0;
    }
    return 1;
}

int cupidimage_pdf_ps_outline_transform(pdf_cff_outline *outline, const int32_t matrix[4],
                                        const int32_t offset[2], size_t *work_left) {
    if (!outline || !matrix || !offset || !work_left || outline->count > 65536 ||
        (outline->count && !outline->segments))
        return 0;
    size_t cost = (size_t)outline->count * 12;
    if (cost > *work_left) {
        *work_left = 0;
        return 0;
    }
    *work_left -= cost;
    for (unsigned i = 0; i < outline->count; i++) {
        const pdf_cff_segment *segment = outline->segments + i;
        if (segment->kind > PDF_CFF_END)
            return 0;
        for (unsigned j = 0; j < (segment->kind == PDF_CFF_CURVE ? 3u : 1u); j++) {
            double result[2];
            if (!ps_transform_point(segment->points + j * 2, matrix, offset, result))
                return 0;
        }
    }
    for (unsigned i = 0; i < outline->count; i++) {
        pdf_cff_segment *segment = outline->segments + i;
        for (unsigned j = 0; j < (segment->kind == PDF_CFF_CURVE ? 3u : 1u); j++) {
            double result[2];
            ps_transform_point(segment->points + j * 2, matrix, offset, result);
            segment->points[j * 2] = result[0];
            segment->points[j * 2 + 1] = result[1];
        }
    }
    return 1;
}
