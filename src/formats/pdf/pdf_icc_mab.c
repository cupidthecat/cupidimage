#include "pdf_icc_mab.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

static uint32_t mab_u32(const unsigned char *p) {
    return (uint32_t)p[0] * 16777216u + (uint32_t)p[1] * 65536u + (unsigned)p[2] * 256u + p[3];
}

static double mab_fixed(const unsigned char *p) {
    uint32_t value = mab_u32(p);
    return (value <= INT32_MAX ? (double)value : (double)value - 4294967296.0) / 65536.0;
}

static int mab_curves(pdf_icc_curve *curves, unsigned count, const unsigned char *data,
                      size_t length, size_t *used) {
    size_t position = 0;
    for (unsigned i = 0; i < count; i++) {
        size_t size;
        if (!cupidimage_pdf_icc_curve_parse(data + position, length - position, curves + i, &size))
            return 0;
        position += size;
        size_t padding = (4 - size % 4) % 4;
        if (padding > length - position) {
            if (i + 1 != count || position != length)
                return 0;
            break;
        }
        for (size_t j = 0; j < padding; j++)
            if (data[position++])
                return 0;
    }
    *used = position;
    return 1;
}

int cupidimage_pdf_icc_mab_parse(pdf_icc_mab *mab, const unsigned char *data, size_t length,
                                 unsigned inputs) {
    memset(mab, 0, sizeof(*mab));
    if (length < 32 || memcmp(data, "mAB ", 4) || mab_u32(data + 4) ||
        (inputs != 1 && inputs != 3 && inputs != 4) || data[8] != inputs || data[9] != 3 ||
        data[10] || data[11])
        return 0;
    size_t offsets[5], sizes[5] = {0};
    for (unsigned i = 0; i < 5; i++) {
        offsets[i] = mab_u32(data + 12 + i * 4);
        if (offsets[i] && ((offsets[i] & 3) || offsets[i] < 32 || offsets[i] > length))
            return 0;
    }
    if (!offsets[0] || (!!offsets[1] != !!offsets[2]) || (!!offsets[3] != !!offsets[4]) ||
        (!offsets[3] && inputs != 3))
        return 0;
    if (!mab_curves(mab->b, 3, data + offsets[0], length - offsets[0], &sizes[0]))
        return 0;
    if (offsets[1]) {
        if (length - offsets[1] < 48 ||
            !mab_curves(mab->m, 3, data + offsets[2], length - offsets[2], &sizes[2]))
            return 0;
        sizes[1] = 48;
        for (unsigned i = 0; i < 12; i++)
            mab->matrix[i] = mab_fixed(data + offsets[1] + i * 4);
        mab->has_matrix = 1;
    }
    if (offsets[3]) {
        if (!cupidimage_pdf_icc_clut_parse(&mab->clut, data + offsets[3], length - offsets[3],
                                           inputs) ||
            !mab_curves(mab->a, inputs, data + offsets[4], length - offsets[4], &sizes[4]))
            return 0;
        size_t samples = mab->clut.strides[0] * mab->clut.grids[0];
        sizes[3] = 20 + samples * 3 * mab->clut.bytes;
        mab->has_clut = 1;
    }
    /* Curve elements may be shared; matrices and CLUTs may not overlap stages. */
    for (unsigned i = 0; i < 5; i++)
        for (unsigned j = i + 1; j < 5; j++)
            if (offsets[i] && offsets[j] && (i == 1 || i == 3 || j == 1 || j == 3) &&
                offsets[i] < offsets[j] + sizes[j] && offsets[j] < offsets[i] + sizes[i])
                return 0;
    mab->inputs = inputs;
    return 1;
}

int cupidimage_pdf_icc_mab_value(const pdf_icc_mab *mab, const double *values, double out[3]) {
    if (!mab->inputs)
        return 0;
    double incoming[4] = {0};
    for (unsigned i = 0; i < mab->inputs; i++) {
        if (!isfinite(values[i]))
            return 0;
        incoming[i] = fmax(0, fmin(1, values[i]));
        if (mab->has_clut && !cupidimage_pdf_icc_curve_value(&mab->a[i], incoming[i], &incoming[i]))
            return 0;
    }
    if (mab->has_clut) {
        if (!cupidimage_pdf_icc_lut_value(&mab->clut, incoming, out))
            return 0;
    } else
        memcpy(out, incoming, 3 * sizeof(double));
    if (mab->has_matrix) {
        double middle[3];
        for (unsigned i = 0; i < 3; i++)
            if (!cupidimage_pdf_icc_curve_value(&mab->m[i], out[i], &middle[i]))
                return 0;
        for (unsigned i = 0; i < 3; i++) {
            double value = mab->matrix[i * 3] * middle[0] + mab->matrix[i * 3 + 1] * middle[1] +
                           mab->matrix[i * 3 + 2] * middle[2] + mab->matrix[9 + i];
            out[i] = fmax(0, fmin(1, value));
        }
    }
    for (unsigned i = 0; i < 3; i++)
        if (!cupidimage_pdf_icc_curve_value(&mab->b[i], out[i], &out[i]))
            return 0;
    return 1;
}
