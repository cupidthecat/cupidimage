#include "geometry.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CLIP_GEOMETRY_MEMORY (64u * 1024u * 1024u)

int cupidimage_pdf_clip_geometry_retain(pdf_clip_geometry *geometry) {
    if (!geometry)
        return 1;
    if (geometry->references == UINT_MAX)
        return 0;
    geometry->references++;
    return 1;
}

void cupidimage_pdf_clip_geometry_release(pdf_clip_geometry *geometry, size_t *bytes_used) {
    while (geometry && --geometry->references == 0) {
        pdf_clip_geometry *parent = geometry->parent;
        *bytes_used -= geometry->bytes;
        free(geometry);
        geometry = parent;
    }
}

static int clip_is_rectangle(const pdf_clip_geometry *geometry) {
    if (!geometry->count)
        return 1;
    if (geometry->count != 4)
        return 0;
    for (unsigned i = 0; i < 4; i++) {
        const pdf_region_edge *a = geometry->edges + i;
        const pdf_region_edge *b = geometry->edges + (i + 1) % 4;
        int horizontal = a->y0 == a->y1, vertical = a->x0 == a->x1;
        if (horizontal == vertical || a->x1 != b->x0 || a->y1 != b->y0 ||
            horizontal == (b->y0 == b->y1))
            return 0;
    }
    return 1;
}

pdf_clip_geometry *cupidimage_pdf_clip_geometry_create(pdf_clip_geometry *parent,
                                                       const pdf_region_edge *edges, unsigned count,
                                                       unsigned even_odd, size_t *bytes_used,
                                                       size_t *work) {
    if (!bytes_used || !work || (count && !edges) || count > 65536 || even_odd > 1 ||
        (parent && parent->references == UINT_MAX) || *bytes_used > CLIP_GEOMETRY_MEMORY ||
        (size_t)count + 1 > *work)
        return NULL;
    *work -= (size_t)count + 1;
    unsigned input_count = count, nonzero = 0;
    double sx = 0, sy = 0, cx = 0, cy = 0, bounds[4] = {INFINITY, INFINITY, -INFINITY, -INFINITY};
    for (unsigned i = 0; i < count; i++) {
        const pdf_region_edge *e = edges + i;
        if (!isfinite(e->x0) || !isfinite(e->y0) || !isfinite(e->x1) || !isfinite(e->y1) ||
            fabs(e->x0) > 32768 || fabs(e->y0) > 32768 || fabs(e->x1) > 32768 ||
            fabs(e->y1) > 32768)
            return NULL;
        if (!i || e->x0 != cx || e->y0 != cy) {
            if (i && (cx != sx || cy != sy))
                return NULL;
            sx = e->x0;
            sy = e->y0;
        }
        cx = e->x1;
        cy = e->y1;
        if (e->x0 == e->x1 && e->y0 == e->y1)
            continue;
        nonzero++;
        bounds[0] = fmin(bounds[0], fmin(e->x0, e->x1));
        bounds[1] = fmin(bounds[1], fmin(e->y0, e->y1));
        bounds[2] = fmax(bounds[2], fmax(e->x0, e->x1));
        bounds[3] = fmax(bounds[3], fmax(e->y0, e->y1));
    }
    if (count && (cx != sx || cy != sy))
        return NULL;
    count = nonzero;
    if (parent && parent->count == count && parent->even_odd == even_odd) {
        if (count > *work)
            return NULL;
        *work -= count;
        unsigned j = 0;
        int same = 1;
        for (unsigned i = 0; i < input_count; i++) {
            const pdf_region_edge *e = edges + i;
            if (e->x0 == e->x1 && e->y0 == e->y1)
                continue;
            const pdf_region_edge *p = parent->edges + j++;
            if (e->x0 != p->x0 || e->y0 != p->y0 || e->x1 != p->x1 || e->y1 != p->y1) {
                same = 0;
                break;
            }
        }
        if (same) {
            (void)cupidimage_pdf_clip_geometry_retain(parent);
            return parent;
        }
    }
    if (parent && (parent->depth == 63 || count > 65536 - parent->total_edges))
        return NULL;
    size_t bytes = sizeof(pdf_clip_geometry) + (size_t)count * sizeof(*edges);
    if (bytes > CLIP_GEOMETRY_MEMORY - *bytes_used)
        return NULL;
    pdf_clip_geometry *geometry = malloc(bytes);
    if (!geometry)
        return NULL;
    memset(geometry, 0, sizeof(*geometry));
    geometry->references = 1;
    geometry->bytes = bytes;
    geometry->count = count;
    geometry->total_edges = count + (parent ? parent->total_edges : 0);
    geometry->depth = 1 + (parent ? parent->depth : 0);
    geometry->even_odd = even_odd;
    if (!count)
        memset(bounds, 0, sizeof(bounds));
    if (parent) {
        bounds[0] = fmax(bounds[0], parent->bounds[0]);
        bounds[1] = fmax(bounds[1], parent->bounds[1]);
        bounds[2] = fmin(bounds[2], parent->bounds[2]);
        bounds[3] = fmin(bounds[3], parent->bounds[3]);
    }
    memcpy(geometry->bounds, bounds, sizeof(bounds));
    unsigned j = 0;
    for (unsigned i = 0; i < input_count; i++) {
        if (edges[i].x0 == edges[i].x1 && edges[i].y0 == edges[i].y1)
            continue;
        geometry->edges[j] = edges[i];
        geometry->edges[j++].region = 0;
    }
    geometry->rectangle = !count || bounds[2] <= bounds[0] || bounds[3] <= bounds[1] ||
                          ((!parent || parent->rectangle) && clip_is_rectangle(geometry));
    geometry->parent = parent;
    (void)cupidimage_pdf_clip_geometry_retain(parent);
    *bytes_used += bytes;
    return geometry;
}

int cupidimage_pdf_clip_geometry_collect(const pdf_clip_geometry *geometry, unsigned first_region,
                                         unsigned char rules[64], pdf_region_edge **output,
                                         unsigned *output_count, size_t *work) {
    if (!output || !output_count)
        return 0;
    *output = NULL;
    *output_count = 0;
    if (!work || !rules || first_region > 64 || (geometry && geometry->depth > 64 - first_region))
        return 0;
    if (!geometry)
        return 1;
    if ((size_t)geometry->total_edges + geometry->depth > *work)
        return 0;
    *work -= (size_t)geometry->total_edges + geometry->depth;
    pdf_region_edge *edges =
        malloc((geometry->total_edges ? geometry->total_edges : 1) * sizeof(*edges));
    if (!edges)
        return 0;
    unsigned count = 0, region = first_region;
    for (const pdf_clip_geometry *node = geometry; node; node = node->parent) {
        rules[region] = (unsigned char)node->even_odd;
        for (unsigned i = 0; i < node->count; i++) {
            edges[count] = node->edges[i];
            edges[count++].region = region;
        }
        region++;
    }
    *output = edges;
    *output_count = count;
    return 1;
}
