#include "fault_alloc.h"
#include <assert.h>
#define malloc fault_malloc
#define calloc fault_calloc
#define free fault_free
#include "../src/formats/pdf/clipping/geometry.c"
#include "../src/formats/pdf/raster/region.c"
#include "../src/formats/pdf/raster/region_ellipse.c"
#undef malloc
#undef calloc
#undef free

static uint32_t seed = 91;

static double value(void) {
    seed = seed * 1664525u + 1013904223u;
    return (int)(seed >> 24) / 16.0 - 6;
}

static void rect(pdf_region_edge *p, double x, double y, double w, double h) {
    p[0] = (pdf_region_edge){x, y, x + w, y, 0};
    p[1] = (pdf_region_edge){x + w, y, x + w, y + h, 0};
    p[2] = (pdf_region_edge){x + w, y + h, x, y + h, 0};
    p[3] = (pdf_region_edge){x, y + h, x, y, 0};
}

int main(void) {
    (void)fault_realloc;
    pdf_region_edge input[128], outer[4];
    rect(outer, -.25, .125, 4.5, 3.5);
    unsigned checks = 0;
    for (unsigned trial = 0; trial < 512; trial++) {
        unsigned count = 4 * (1 + trial % 32);
        for (unsigned i = 0; i < count; i += 4) {
            double x = value(), y = value(), w = value() / 2, h = value() / 2;
            rect(input + i, x, y, w, h);
            if (i % 8 == 0) {
                /* A connected nonrectangular contour with the same outer bounds. */
                input[i + 1].x1 = input[i + 2].x0 = x + w * .25;
            }
        }
        if (trial % 3 == 0)
            rect(input, -100, -100, 200, 200);
        size_t work = 10000000, bytes = 0;
        pdf_clip_geometry *parent =
            cupidimage_pdf_clip_geometry_create(NULL, outer, 4, 0, &bytes, &work);
        assert(parent);
        pdf_clip_geometry *child =
            cupidimage_pdf_clip_geometry_create(parent, input, count, trial % 2, &bytes, &work);
        assert(child);
        cupidimage_pdf_clip_geometry_release(parent, &bytes);
        pdf_region_edge *all = NULL, *local = NULL;
        unsigned all_count, local_count;
        unsigned char full_rules[64] = {0}, crop_rules[64] = {0};
        assert(cupidimage_pdf_clip_geometry_collect(child, 0, full_rules, &all, &all_count, &work));
        assert(cupidimage_pdf_clip_geometry_collect_crop(child, 0, crop_rules, &local, &local_count,
                                                         &work, 0, 0, 4, 4));
        assert(local_count <= all_count && !memcmp(full_rules, crop_rules, sizeof(full_rules)));
        unsigned char *full = NULL, *cropped = NULL;
        assert(cupidimage_pdf_region_coverage(all, all_count, full_rules, child->depth, 0, 0, 4, 4,
                                              &work, &full));
        assert(cupidimage_pdf_region_coverage(local, local_count, crop_rules, child->depth, 0, 0, 4,
                                              4, &work, &cropped));
        assert(!memcmp(full, cropped, 16));
        fault_free(full);
        fault_free(cropped);
        fault_free(all);
        fault_free(local);
        cupidimage_pdf_clip_geometry_release(child, &bytes);
        assert(!bytes && !live_count);
        checks++;
    }
    for (unsigned i = 0; i < 128; i += 4)
        rect(input + i, (double)i - 4, .25, 2, 2);
    for (size_t failure = 0; failure < 4; failure++)
        for (size_t quota = 0; quota < 1000; quota++) {
            allocation_count = 0;
            fail_at = failure;
            size_t bytes = 0, work = quota;
            pdf_clip_geometry *node =
                cupidimage_pdf_clip_geometry_create(NULL, input, 128, 0, &bytes, &work);
            if (node) {
                pdf_region_edge *edges = (void *)1;
                unsigned count = 999;
                unsigned char rules[64] = {0};
                int ok = cupidimage_pdf_clip_geometry_collect_crop(node, 0, rules, &edges, &count,
                                                                   &work, 0, 0, 4, 4);
                if (ok) {
                    assert(count == 8);
                    fault_free(edges);
                } else
                    assert(!edges && !count);
                cupidimage_pdf_clip_geometry_release(node, &bytes);
            }
            assert(!bytes && !live_count && work <= quota);
            checks++;
        }
    printf("PDF contour crops: %u exact raster, budget and ownership checks passed\n", checks);
    return 0;
}
