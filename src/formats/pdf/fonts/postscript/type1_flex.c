#include "type1_flex.h"
#include <math.h>
#include <string.h>

/* Character-space controls: first cubic, then second cubic. */
int cupidimage_pdf_ps_type1_flex(const double input[12], const double reference[2],
                                 double threshold, const double matrix[6], int stroked,
                                 double erosion, double output[12], int *line) {
    if (!input || !reference || !matrix || !output || !line || !isfinite(threshold) ||
        fabs(threshold) > 1e30 || !isfinite(erosion) || (stroked != 0 && stroked != 1))
        return 0;
    for (unsigned i = 0; i < 12; i++)
        if (!isfinite(input[i]) || fabs(input[i]) > 1e30)
            return 0;
    for (unsigned i = 0; i < 2; i++)
        if (!isfinite(reference[i]) || fabs(reference[i]) > 1e30)
            return 0;
    for (unsigned i = 0; i < 6; i++)
        if (!isfinite(matrix[i]) || fabs(matrix[i]) > 1e30)
            return 0;
    double determinant = matrix[0] * matrix[3] - matrix[1] * matrix[2];
    if (!isfinite(determinant) || determinant == 0)
        return 0;
    double result[12];
    memcpy(result, input, sizeof(result));
    unsigned axis = fabs(reference[1] - input[5]) > fabs(reference[0] - input[4]);
    /* Alignment tolerances and axis rounding follow the standard OtherSubrs
     * procedure, rather than the Type 2 perpendicular-distance policy. */
    int alignment = fabs(matrix[0]) < .00001 || fabs(matrix[3]) < .00001   ? -1
                    : fabs(matrix[1]) < .00001 || fabs(matrix[2]) < .00001 ? 1
                                                                           : 0;
    double join = input[4 + axis], end = input[10 + axis], base = reference[axis];
    if (alignment && join != end) {
        unsigned device_axis = alignment == 1 ? axis : 1 - axis;
        double coefficient = matrix[axis * 2 + device_axis];
        double depth = fabs(floor(join - base + .5) * coefficient);
        if (depth < fabs(threshold) / 100) {
            double scale = fabs((base - end) / (join - end));
            for (unsigned point = 0; point < 5; point++)
                result[point * 2 + axis] = (input[point * 2 + axis] - end) * scale + end;
            result[4] = reference[0];
            result[5] = reference[1];
        }
        if (fabs(result[4 + axis] - base) > .001) {
            double device[2] = {reference[0] * matrix[0] + reference[1] * matrix[2] + matrix[4],
                                reference[0] * matrix[1] + reference[1] * matrix[3] + matrix[5]};
            double distance = floor(result[4 + axis] - base + .5) * coefficient;
            double integer = floor(distance + .5);
            if (!integer)
                integer = distance < 0 ? -1 : 1;
            double erode = !stroked && erosion >= .5 ? .5 : 0;
            double target = device[device_axis] - erode + integer;
            device[device_axis] = ceil(target) - target + floor(target) + erode;
            double dx = device[0] - matrix[4], dy = device[1] - matrix[5];
            double adjusted = axis ? (matrix[0] * dy - matrix[1] * dx) / determinant
                                   : (matrix[3] * dx - matrix[2] * dy) / determinant;
            double shift = adjusted - result[4 + axis];
            result[2 + axis] += shift;
            result[4 + axis] += shift;
            result[6 + axis] += shift;
        }
    }
    for (unsigned i = 0; i < 12; i++)
        if (!isfinite(result[i]) || fabs(result[i]) > 1e30)
            return 0;
    *line = result[4] == result[10] || result[5] == result[11];
    memcpy(output, result, sizeof(result));
    return 1;
}
