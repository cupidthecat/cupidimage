#include "fault_alloc.h"
#include <assert.h>
#define malloc fault_malloc
#define calloc fault_calloc
#define free fault_free
#include "../src/formats/pdf/clipping/geometry.c"
#undef malloc
#undef calloc
#undef free

int main(void) {
    (void)fault_calloc;
    (void)fault_realloc;
    pdf_region_edge rectangle[] = {
        {0, 0, 1, 0, 0}, {1, 0, 1, 1, 0}, {1, 1, 0, 1, 0}, {0, 1, 0, 0, 0}};
    unsigned checks = 0;
    for (size_t failure = 0; failure <= 6; failure++)
        for (size_t quota = 0; quota < 40; quota++) {
            allocation_count = 0;
            fail_at = failure;
            size_t bytes = 0, work = quota;
            pdf_clip_geometry *a =
                cupidimage_pdf_clip_geometry_create(NULL, rectangle, 4, 0, &bytes, &work);
            if (a) {
                assert(a->references == 1 && a->depth == 1 && a->total_edges == 4);
                pdf_clip_geometry *b =
                    cupidimage_pdf_clip_geometry_create(a, rectangle, 4, 1, &bytes, &work);
                if (b) {
                    assert(a->references == 2 && b->depth == 2 && b->total_edges == 8);
                    cupidimage_pdf_clip_geometry_release(a, &bytes);
                    a = NULL;
                    unsigned char rules[64] = {0};
                    pdf_region_edge *edges = (void *)1;
                    unsigned count = 999;
                    int ok =
                        cupidimage_pdf_clip_geometry_collect(b, 1, rules, &edges, &count, &work);
                    if (ok) {
                        assert(count == 8 && rules[1] == 1 && rules[2] == 0);
                        for (unsigned i = 0; i < count; i++)
                            assert(edges[i].region == (i < 4 ? 1u : 2u));
                        fault_free(edges);
                    } else
                        assert(!edges && !count);
                    cupidimage_pdf_clip_geometry_release(b, &bytes);
                }
                cupidimage_pdf_clip_geometry_release(a, &bytes);
            }
            assert(bytes == 0 && !live_count && work <= quota);
            checks++;
        }
    fail_at = 0;
    for (unsigned bad = 0; bad < 4; bad++) {
        pdf_region_edge p[4];
        memcpy(p, rectangle, sizeof(p));
        if (bad == 0)
            p[3].x1 = .5;
        if (bad == 1)
            p[1].x0 = .5;
        if (bad == 2)
            p[0].x0 = NAN;
        if (bad == 3)
            p[0].x0 = 32769;
        size_t bytes = 0, work = 1000;
        assert(!cupidimage_pdf_clip_geometry_create(NULL, p, 4, 0, &bytes, &work));
        assert(!bytes && !live_count);
        checks++;
    }
    size_t bytes = 0, work = 10000;
    pdf_clip_geometry *head = NULL;
    for (unsigned i = 0; i < 63; i++) {
        double right = 1 - i / 128.0;
        rectangle[0].x1 = rectangle[1].x0 = rectangle[1].x1 = rectangle[2].x0 = right;
        pdf_clip_geometry *next =
            cupidimage_pdf_clip_geometry_create(head, rectangle, 4, 0, &bytes, &work);
        assert(next);
        cupidimage_pdf_clip_geometry_release(head, &bytes);
        head = next;
    }
    size_t before = bytes;
    pdf_clip_geometry *same =
        cupidimage_pdf_clip_geometry_create(head, rectangle, 4, 0, &bytes, &work);
    assert(same == head && bytes == before && head->references == 2);
    cupidimage_pdf_clip_geometry_release(same, &bytes);
    checks++;
    rectangle[0].x1 = rectangle[1].x0 = rectangle[1].x1 = rectangle[2].x0 = .1;
    assert(!cupidimage_pdf_clip_geometry_create(head, rectangle, 4, 0, &bytes, &work));
    cupidimage_pdf_clip_geometry_release(head, &bytes);
    assert(!bytes && !live_count);
    checks++;
    printf("PDF clip geometry: %u scope, limit and ownership checks passed\n", checks);
    return 0;
}
