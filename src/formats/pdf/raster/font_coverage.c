#include "font_coverage.h"
#include "coverage.h"
#include <math.h>
#include <stdlib.h>

static int coverage_point(pdf_coverage_point *point, double x, double y, const double m[6],
                          int left, int bottom, unsigned tag, unsigned end) {
    double px = (m[0] * x + m[2] * y + m[4] - left) * 64;
    double py = (bottom - (m[1] * x + m[3] * y + m[5])) * 64;
    if (!isfinite(px) || !isfinite(py) || px < -2097152 || px > 2097152 || py < -2097152 ||
        py > 2097152)
        return 0;
    *point = (pdf_coverage_point){(int32_t)floor(px + .5), (int32_t)floor(py + .5), tag, end};
    return 1;
}

int cupidimage_pdf_font_coverage(const pdf_tt_outline *tt, const pdf_cff_outline *cff,
                                 const double matrix[6], int left, int top, int right, int bottom,
                                 size_t *work_left, unsigned char **output) {
    if (!output)
        return 0;
    *output = NULL;
    if (!work_left || !matrix || ((tt == NULL) == (cff == NULL)) || (int64_t)right - left < 0 ||
        (int64_t)right - left > 32767 || (int64_t)bottom - top < 0 || (int64_t)bottom - top > 32767)
        return 0;
    for (unsigned i = 0; i < 6; i++)
        if (!isfinite(matrix[i]))
            return 0;
    unsigned count = 0;
    if (tt) {
        count = tt->count;
        if (count > 65535 || (count && !tt->points))
            return 0;
    } else {
        if (cff->count > 65536 || (cff->count && !cff->segments))
            return 0;
        if (cff->count > *work_left)
            return 0;
        *work_left -= cff->count;
        for (unsigned i = 0; i < cff->count; i++) {
            unsigned kind = cff->segments[i].kind;
            if (kind > PDF_CFF_SETPOINT)
                return 0;
            if (kind == PDF_CFF_MOVE || kind == PDF_CFF_LINE)
                count++;
            else if (kind == PDF_CFF_CURVE)
                count += 3;
            if (count > 65535)
                return 0;
        }
    }
    if (count > *work_left)
        return 0;
    *work_left -= count;
    pdf_coverage_point *points = count ? malloc((size_t)count * sizeof(*points)) : NULL;
    if (count && !points)
        return 0;
    int ok = 0;
    unsigned used = 0;
    if (tt) {
        for (unsigned i = 0; i < count; i++) {
            if (tt->points[i].on > 1 || tt->points[i].end > 1)
                goto done;
            if (!coverage_point(points + used++, tt->points[i].x, tt->points[i].y, matrix, left,
                                bottom, tt->points[i].on, tt->points[i].end))
                goto done;
        }
    } else {
        int open = 0;
        for (unsigned i = 0; i < cff->count; i++) {
            const pdf_cff_segment *s = cff->segments + i;
            if (s->kind == PDF_CFF_SETPOINT)
                continue; /* Absolute coordinates already include the current-point change. */
            if (s->kind == PDF_CFF_CLOSE || s->kind == PDF_CFF_END) {
                if (open)
                    points[used - 1].end = 1;
                open = 0;
                continue;
            }
            if (s->kind == PDF_CFF_MOVE) {
                if (open)
                    points[used - 1].end = 1;
                open = 1;
            } else if (!open)
                goto done;
            unsigned n = s->kind == PDF_CFF_CURVE ? 3 : 1;
            for (unsigned j = 0; j < n; j++)
                if (!coverage_point(points + used++, s->points[j * 2], s->points[j * 2 + 1], matrix,
                                    left, bottom, j + 1 < n ? 2 : 1, 0))
                    goto done;
        }
        if (open)
            points[used - 1].end = 1;
    }
    ok = cupidimage_pdf_coverage(points, used, (unsigned)(right - left), (unsigned)(bottom - top),
                                 0, work_left, output);
done:
    free(points);
    return ok;
}
