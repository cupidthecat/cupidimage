#include "font_monochrome.h"
#include "monochrome.h"
#include <math.h>
#include <stdlib.h>

int cupidimage_pdf_font_monochrome(const pdf_tt_hint_shape *shape, int left, int top, int right,
                                   int bottom, size_t *work_left, unsigned char **output) {
    if (!output)
        return 0;
    *output = NULL;
    if (!shape || !work_left || shape->count > 65535 || (shape->count && !shape->points) ||
        (int64_t)right - left < 0 || (int64_t)right - left > 32767 || (int64_t)bottom - top < 0 ||
        (int64_t)bottom - top > 32767 || shape->count > *work_left)
        return 0;
    *work_left -= shape->count;
    pdf_coverage_point *points =
        shape->count ? malloc((size_t)shape->count * sizeof(*points)) : NULL;
    if (shape->count && !points)
        return 0;
    int ok = 1;
    for (unsigned i = 0; ok && i < shape->count; i++) {
        const pdf_tt_point *point = shape->points + i;
        unsigned scan = shape->scan_modes ? shape->scan_modes[i] : 0;
        double x = (point->x - left) * 64, y = (bottom - point->y) * 64;
        if (!isfinite(x) || !isfinite(y) || x < -2097152 || x > 2097152 || y < -2097152 ||
            y > 2097152 || point->on > 1 || point->end > 1 || scan > 8) {
            ok = 0;
            break;
        }
        unsigned tag = point->on | (scan ? 4 | ((scan - 1) << 5) : 0);
        points[i] = (pdf_coverage_point){(int32_t)floor(x + .5), (int32_t)floor(y + .5),
                                         (unsigned char)tag, point->end};
    }
    if (ok)
        ok = cupidimage_pdf_monochrome(points, shape->count, (unsigned)(right - left),
                                       (unsigned)(bottom - top), shape->scan_mode,
                                       (int)shape->high_precision, work_left, output);
    free(points);
    return ok;
}
