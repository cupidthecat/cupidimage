#include "sample.h"
#include <math.h>

int cupidimage_pdf_image_taps(unsigned width, unsigned height, double u, double v, int interpolate,
                              pdf_image_tap taps[4], unsigned *count) {
    if (!count)
        return 0;
    *count = 0;
    if (!taps || !width || !height || !isfinite(u) || !isfinite(v) ||
        (interpolate != 0 && interpolate != 1))
        return 0;
    if (u < 0 || u >= 1 || v <= 0 || v > 1)
        return 1;
    double x = u * width, y = (1 - v) * height;
    if (!interpolate) {
        if (x >= width || y >= height)
            return 1;
        taps[0] = (pdf_image_tap){(unsigned)x, (unsigned)y, 1};
        *count = 1;
        return 1;
    }
    x = fmax(0, fmin(width - 1.0, x - .5));
    y = fmax(0, fmin(height - 1.0, y - .5));
    unsigned left = (unsigned)x, top = (unsigned)y;
    double dx = x - left, dy = y - top;
    for (unsigned row = 0; row < 2; row++) {
        double wy = row ? dy : 1 - dy;
        if (!wy)
            continue;
        for (unsigned column = 0; column < 2; column++) {
            double weight = wy * (column ? dx : 1 - dx);
            if (weight)
                taps[(*count)++] = (pdf_image_tap){left + column, top + row, weight};
        }
    }
    return 1;
}
