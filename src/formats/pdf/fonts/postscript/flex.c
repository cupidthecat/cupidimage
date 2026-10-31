#include "flex.h"
#include <math.h>

int cupidimage_pdf_ps_flex_test(const double start[2], const pdf_cff_segment curves[2],
                                double threshold, const double matrix[4], int *flatten) {
    if (!flatten)
        return 0;
    *flatten = 0;
    if (!start || !curves || !matrix || !isfinite(threshold) || fabs(threshold) > 1e30 ||
        curves[0].kind != PDF_CFF_CURVE || curves[1].kind != PDF_CFF_CURVE)
        return 0;
    for (unsigned i = 0; i < 4; i++)
        if (!isfinite(matrix[i]) || fabs(matrix[i]) > 1e30)
            return 0;
    for (unsigned axis = 0; axis < 2; axis++) {
        if (!isfinite(start[axis]) || fabs(start[axis]) > 1e30)
            return 0;
        for (unsigned curve = 0; curve < 2; curve++)
            for (unsigned i = axis; i < 6; i += 2)
                if (!isfinite(curves[curve].points[i]) || fabs(curves[curve].points[i]) > 1e30)
                    return 0;
    }
    double dx = curves[1].points[4] - start[0], dy = curves[1].points[5] - start[1];
    double jx = curves[0].points[4] - start[0], jy = curves[0].points[5] - start[1];
    double end_x = dx * matrix[0] + dy * matrix[2];
    double end_y = dx * matrix[1] + dy * matrix[3];
    double join_x = jx * matrix[0] + jy * matrix[2];
    double join_y = jx * matrix[1] + jy * matrix[3];
    double length = hypot(end_x, end_y);
    if (threshold <= 0 || length == 0)
        return 1;
    double cross = fabs(join_x * end_y - join_y * end_x);
    *flatten = cross < (threshold / 100) * length;
    return 1;
}
