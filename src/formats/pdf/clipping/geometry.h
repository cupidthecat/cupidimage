#ifndef CUPIDIMAGE_PDF_CLIP_GEOMETRY_H
#define CUPIDIMAGE_PDF_CLIP_GEOMETRY_H
#include "../raster/region.h"

typedef struct pdf_clip_geometry {
    struct pdf_clip_geometry *parent;
    size_t bytes;
    unsigned references, count, total_edges, depth, even_odd;
    double bounds[4];
    pdf_region_edge edges[];
} pdf_clip_geometry;

/* Immutable, device-space clipping regions. Each input contour must be closed;
 * separate contours may touch. The parent is retained only on success.
 * All nodes in a chain share the caller's byte accounting. */
pdf_clip_geometry *cupidimage_pdf_clip_geometry_create(pdf_clip_geometry *, const pdf_region_edge *,
                                                       unsigned, unsigned, size_t *, size_t *);
int cupidimage_pdf_clip_geometry_retain(pdf_clip_geometry *);
void cupidimage_pdf_clip_geometry_release(pdf_clip_geometry *, size_t *);
/* Copies the chain into region IDs starting at first_region. The caller owns
 * the output allocation. Failure clears output and output_count. */
int cupidimage_pdf_clip_geometry_collect(const pdf_clip_geometry *, unsigned, unsigned char[64],
                                         pdf_region_edge **, unsigned *, size_t *);
#endif
