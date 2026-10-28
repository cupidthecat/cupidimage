#include "pdf_icc_curve.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

static unsigned icc_curve_u16(const unsigned char *p) {
    return (unsigned)p[0] * 256 + p[1];
}

static uint32_t icc_curve_u32(const unsigned char *p) {
    return (uint32_t)p[0] * 16777216u + (uint32_t)p[1] * 65536u + (unsigned)p[2] * 256u + p[3];
}

static double icc_curve_fixed(const unsigned char *p) {
    uint32_t value = icc_curve_u32(p);
    return (value <= INT32_MAX ? (double)value : (double)value - 4294967296.0) / 65536.0;
}

int cupidimage_pdf_icc_curve_value(const pdf_icc_curve *curve, double input, double *output) {
    double x = fmax(0, fmin(1, input)), value;
    if (curve->parametric) {
        const double *p = curve->parameters;
        if (!curve->function)
            value = pow(x, p[0]);
        else if (curve->function <= 2)
            value = (x >= -p[2] / p[1] ? pow(p[1] * x + p[2], p[0]) : 0) +
                    (curve->function == 2 ? p[3] : 0);
        else if (x >= p[4])
            value = pow(p[1] * x + p[2], p[0]) + (curve->function == 4 ? p[5] : 0);
        else
            value = p[3] * x + (curve->function == 4 ? p[6] : 0);
    } else if (!curve->count)
        value = x;
    else if (curve->count == 1)
        value = pow(x, curve->parameters[0]);
    else {
        double at = x * (curve->count - 1);
        unsigned index = (unsigned)floor(at);
        value = icc_curve_u16(curve->samples + index * 2) / 65535.0;
        if (index + 1 < curve->count)
            value +=
                (at - index) * (icc_curve_u16(curve->samples + (index + 1) * 2) / 65535.0 - value);
    }
    if (!isfinite(value))
        return 0;
    *output = fmax(0, fmin(1, value));
    return 1;
}

int cupidimage_pdf_icc_curve_parse(const unsigned char *data, size_t length, pdf_icc_curve *curve,
                                   size_t *used) {
    memset(curve, 0, sizeof(*curve));
    if (length < 12 || icc_curve_u32(data + 4))
        return 0;
    if (!memcmp(data, "curv", 4)) {
        curve->count = icc_curve_u32(data + 8);
        if (curve->count > 65536 || curve->count > (length - 12) / 2)
            return 0;
        if (used)
            *used = 12 + (size_t)curve->count * 2;
        curve->samples = data + 12;
        if (curve->count == 1) {
            curve->parameters[0] = icc_curve_u16(data + 12) / 256.0;
            if (curve->parameters[0] <= 0)
                return 0;
        }
        return 1;
    }
    if (memcmp(data, "para", 4) || icc_curve_u16(data + 10))
        return 0;
    static const unsigned parameters[] = {1, 3, 4, 5, 7};
    curve->parametric = 1;
    curve->function = icc_curve_u16(data + 8);
    if (curve->function > 4 || length < 12 + parameters[curve->function] * 4)
        return 0;
    if (used)
        *used = 12 + parameters[curve->function] * 4;
    for (unsigned i = 0; i < parameters[curve->function]; i++)
        curve->parameters[i] = icc_curve_fixed(data + 12 + i * 4);
    if (curve->parameters[0] <= 0 || (curve->function && curve->parameters[1] <= 0))
        return 0;
    double ignored;
    return cupidimage_pdf_icc_curve_value(curve, 0, &ignored) &&
           cupidimage_pdf_icc_curve_value(curve, 1, &ignored) &&
           (curve->function < 3 ||
            cupidimage_pdf_icc_curve_value(curve, curve->parameters[4], &ignored));
}
