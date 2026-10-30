#include "region_ellipse.h"
#include <math.h>

int cupidimage_pdf_region_ellipse_frame(const double axes[4], pdf_region_ellipse_frame *frame) {
    if (!axes)
        return 0;
    for (unsigned i = 0; i < 4; i++) {
        if (!isfinite(axes[i]) || fabs(axes[i]) > 65536)
            return 0;
        frame->axes[i] = axes[i];
    }
    const long double *a = frame->axes;
    long double det = a[0] * a[3] - a[1] * a[2];
    frame->height = hypotl(a[1], a[3]);
    frame->extent_x = hypotl(a[0], a[2]);
    if (!det || !frame->height)
        return 0;
    frame->inverse[0] = a[3] / det;
    frame->inverse[1] = -a[1] / det;
    frame->inverse[2] = -a[2] / det;
    frame->inverse[3] = a[0] / det;
    for (unsigned i = 0; i < 4; i++)
        if (!isfinite(frame->inverse[i]) || fabsl(frame->inverse[i]) > 1e64L)
            return 0;
    frame->half_width = fabsl(det) / frame->height;
    frame->skew = (a[0] * a[1] + a[2] * a[3]) / (frame->height * frame->height);
    return 1;
}

long double cupidimage_pdf_region_ellipse_x(const pdf_region_ellipse_frame *f,
                                            const pdf_region_ellipse *e, long double y, int side) {
    long double t = (y - e->y) / f->height;
    return e->x + f->skew * (y - e->y) + side * f->half_width * sqrtl(fmaxl(0, 1 - t * t));
}

long double cupidimage_pdf_region_ellipse_integral(const pdf_region_ellipse_frame *f,
                                                   const pdf_region_ellipse *e, int side,
                                                   long double x, long double lo, long double hi) {
    long double u = fmaxl(-1, fminl(1, (lo - e->y) / f->height)),
                v = fmaxl(-1, fminl(1, (hi - e->y) / f->height));
    long double arc =
        v * sqrtl(fmaxl(0, 1 - v * v)) + asinl(v) - u * sqrtl(fmaxl(0, 1 - u * u)) - asinl(u);
    return (hi - lo) * (e->x - x + f->skew * ((lo + hi) / 2 - e->y)) +
           side * f->half_width * f->height * .5L * arc;
}

unsigned cupidimage_pdf_region_line_ellipse(const pdf_region_ellipse_frame *f, long double x0,
                                            long double y0, long double x1, long double y1,
                                            const pdf_region_ellipse *e, long double ys[2]) {
    const long double *m = f->inverse;
    long double dx = x1 - x0, dy = y1 - y0, x = x0 - e->x, y = y0 - e->y;
    long double ux = m[0] * dx + m[2] * dy, uy = m[1] * dx + m[3] * dy;
    long double vx = m[0] * x + m[2] * y, vy = m[1] * x + m[3] * y;
    long double aa = ux * ux + uy * uy, bb = ux * vx + uy * vy, cc = vx * vx + vy * vy - 1;
    if (!aa)
        return 0;
    long double d = bb * bb - aa * cc;
    if (d < 0)
        return 0;
    long double q = -bb - copysignl(sqrtl(d), bb), roots[2] = {q / aa, q ? cc / q : -bb / aa};
    unsigned n = 0;
    for (unsigned i = 0; i < 2; i++)
        if (roots[i] >= 0 && roots[i] <= 1)
            ys[n++] = y0 + roots[i] * dy;
    return n;
}

unsigned cupidimage_pdf_region_ellipse_pair(const pdf_region_ellipse_frame *f,
                                            const pdf_region_ellipse *a,
                                            const pdf_region_ellipse *b, long double ys[2]) {
    long double x = b->x - a->x, y = b->y - a->y;
    long double u = f->inverse[0] * x + f->inverse[2] * y,
                v = f->inverse[1] * x + f->inverse[3] * y, d2 = u * u + v * v;
    if (!d2 || d2 > 4)
        return 0;
    long double scale = sqrtl(fmaxl(0, 1 - d2 / 4) / d2);
    long double mid = a->y + f->axes[1] * u / 2 + f->axes[3] * v / 2;
    long double offset = (-f->axes[1] * v + f->axes[3] * u) * scale;
    ys[0] = mid - offset;
    ys[1] = mid + offset;
    return 2;
}
