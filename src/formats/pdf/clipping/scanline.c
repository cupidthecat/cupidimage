#include "scanline.h"
#include <math.h>

static int scan_sift(pdf_scan_crossing *items, unsigned root, unsigned count, size_t *work) {
    while (root < count / 2) {
        if (*work < 3)
            return 0;
        *work -= 3;
        unsigned child = root * 2 + 1;
        if (child + 1 < count && items[child].x < items[child + 1].x)
            child++;
        if (items[root].x >= items[child].x)
            break;
        pdf_scan_crossing swap = items[root];
        items[root] = items[child];
        items[child] = swap;
        root = child;
    }
    return 1;
}

int cupidimage_pdf_scanline(const pdf_region_edge *edges, unsigned count, double y,
                            pdf_scan_crossing *crossings, size_t *work) {
    if (!work || count > 262144 || (count && (!edges || !crossings)) || !isfinite(y) ||
        count > *work)
        return -1;
    *work -= count;
    unsigned used = 0;
    for (unsigned i = 0; i < count; i++) {
        double x0 = edges[i].x0, y0 = edges[i].y0;
        double x1 = edges[i].x1, y1 = edges[i].y1;
        if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1))
            return -1;
        if (!((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) || fabs(y0 - y1) <= 1e-6)
            continue;
        /* Use the shared geometric precision and the binary endpoint rule. */
        double dx = x1 - x0, dy = y1 - y0;
        double numerator = (y - y0) * dx;
        /* Divide the displacement once, rather than rounding a fractional
         * parameter before multiplication. Exact pixel-center crossings must
         * retain the endpoint rule. Scale first only if the product overflows. */
        double displacement = isfinite(numerator) ? numerator / dy : ((y - y0) / dy) * dx;
        double x = x0 + displacement;
        if (!isfinite(x))
            return -1;
        crossings[used++] = (pdf_scan_crossing){x, y1 > y0 ? 1 : -1};
    }
    for (unsigned i = used / 2; i > 0; i--)
        if (!scan_sift(crossings, i - 1, used, work))
            return -1;
    for (unsigned end = used; end > 1; end--) {
        if (!*work)
            return -1;
        (*work)--;
        pdf_scan_crossing swap = crossings[0];
        crossings[0] = crossings[end - 1];
        crossings[end - 1] = swap;
        if (!scan_sift(crossings, 0, end - 1, work))
            return -1;
    }
    return (int)used;
}
