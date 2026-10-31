#ifndef CUPIDIMAGE_PDF_REGION_H
#define CUPIDIMAGE_PDF_REGION_H
#include <stddef.h>

typedef struct {
    double x0, y0, x1, y1;
    unsigned region;
} pdf_region_edge;

typedef struct {
    double x, y;
    unsigned region, reverse;
} pdf_region_ellipse;

/* Each region contains closed directed contours. The result is their geometric
 * intersection. A region uses nonzero winding unless its even_odd byte is 1.
 * Empty regions cover nothing; zero regions impose no restriction.
 * Coordinates/crop: +/-32768; edges: 65536; regions: 64; output: 16 MiB.
 * Output rows increase with y. Failure returns NULL, never a partial mask. */
int cupidimage_pdf_region_coverage(const pdf_region_edge *edges, unsigned count,
                                   const unsigned char *even_odd, unsigned regions, int left,
                                   int top, int right, int bottom, size_t *work,
                                   unsigned char **output);
/* Optional congruent affine ellipses participate in the same region winding.
 * Default orientation matches a polygon with positive signed area. */
int cupidimage_pdf_region_coverage_ellipses(const pdf_region_edge *, unsigned,
                                            const pdf_region_ellipse *, unsigned, const double[4],
                                            const unsigned char *, unsigned, int, int, int, int,
                                            size_t *, unsigned char **);
#endif
