#include "matrix.h"
#include <math.h>
#include <string.h>

int cupidimage_pdf_color_matrix_apply(const double matrix[9], const double input[3],
                                      double output[3]) {
    if (!matrix || !input || !output)
        return 0;
    double values[3] = {input[0], input[1], input[2]}, result[3];
    for (unsigned i = 0; i < 3; i++) {
        result[i] = matrix[i * 3] * values[0] + matrix[i * 3 + 1] * values[1] +
                    matrix[i * 3 + 2] * values[2];
        if (!isfinite(result[i]))
            return 0;
    }
    memcpy(output, result, sizeof(result));
    return 1;
}

int cupidimage_pdf_color_matrix_inverse(const double matrix[9], double output[9]) {
    if (!output)
        return 0;
    double source[9] = {0};
    if (matrix)
        memcpy(source, matrix, sizeof(source));
    memset(output, 0, 9 * sizeof(*output));
    if (!matrix)
        return 0;
    long double scale = 0, norm = 0;
    for (unsigned i = 0; i < 9; i++) {
        if (!isfinite(source[i]) || fabs(source[i]) > 1e30)
            return 0;
        scale = fmaxl(scale, fabsl(source[i]));
    }
    if (!scale)
        return 0;
    long double rows[3][6] = {{0}};
    for (unsigned i = 0; i < 3; i++) {
        long double sum = 0;
        for (unsigned j = 0; j < 3; j++) {
            rows[i][j] = (long double)source[i * 3 + j] / scale;
            sum += fabsl(source[i * 3 + j]);
        }
        norm = fmaxl(norm, sum);
        rows[i][i + 3] = 1;
    }
    for (unsigned column = 0; column < 3; column++) {
        unsigned pivot = column;
        for (unsigned row = column + 1; row < 3; row++)
            if (fabsl(rows[row][column]) > fabsl(rows[pivot][column]))
                pivot = row;
        if (rows[pivot][column] == 0)
            return 0;
        if (pivot != column)
            for (unsigned j = 0; j < 6; j++) {
                long double swap = rows[pivot][j];
                rows[pivot][j] = rows[column][j];
                rows[column][j] = swap;
            }
        long double divisor = rows[column][column];
        for (unsigned j = 0; j < 6; j++)
            rows[column][j] /= divisor;
        for (unsigned row = 0; row < 3; row++)
            if (row != column) {
                long double factor = rows[row][column];
                for (unsigned j = 0; j < 6; j++)
                    rows[row][j] -= factor * rows[column][j];
            }
    }
    double result[9];
    long double inverse_norm = 0;
    for (unsigned i = 0; i < 3; i++) {
        long double sum = 0;
        for (unsigned j = 0; j < 3; j++) {
            long double value = rows[i][j + 3] / scale;
            if (!isfinite(value) || fabsl(value) > 1e64)
                return 0;
            result[i * 3 + j] = (double)value;
            sum += fabsl(value);
        }
        inverse_norm = fmaxl(inverse_norm, sum);
    }
    if (norm * inverse_norm > 1e12L)
        return 0;
    for (unsigned row = 0; row < 3; row++)
        for (unsigned column = 0; column < 3; column++) {
            long double value = 0;
            for (unsigned i = 0; i < 3; i++)
                value += (long double)source[row * 3 + i] * result[i * 3 + column];
            if (!isfinite(value) || fabsl(value - (row == column)) > 1e-9L)
                return 0;
        }
    memcpy(output, result, sizeof(result));
    return 1;
}
