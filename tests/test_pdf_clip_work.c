#include "formats/pdf/pdf_render_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    pdf_render_state *state = calloc(1, sizeof(*state));
    assert(state);
    cupidimage_image image = {0};
    image.width = image.height = 512;
    state->img = &image;
    unsigned checks = 0;
    for (unsigned empty = 0; empty < 2; empty++) {
        pdf_region_edge edges[4] = {
            {1, 1, 3, 1, 0}, {3, 1, 3, 3, 0}, {3, 3, 1, 3, 0}, {1, 3, 1, 1, 0}};
        if (empty)
            for (unsigned i = 0; i < 4; i++) {
                edges[i].x0 += 1024;
                edges[i].x1 += 1024;
            }
        pdf_clip_budget budget = {0};
        budget.tests_left = 1000000;
        state->clip_budget = &budget;
        pdf_clip_geometry *geometry = cupidimage_pdf_clip_geometry_create(
            NULL, edges, 4, 0, &budget.bytes_used, &budget.tests_left);
        assert(geometry && geometry->rectangle);
        size_t geometry_bytes = budget.bytes_used;
        for (unsigned insufficient = 0; insufficient < 2; insufficient++) {
            pdf_clip_mask *clip = calloc(1, sizeof(*clip) + (512 * 512) / 8);
            assert(clip);
            clip->geometry = geometry;
            clip->bytes = (512 * 512) / 8;
            memset(clip->bits, 255, clip->bytes);
            size_t required = clip->bytes + (empty ? 0 : 4);
            budget.tests_left = required - insufficient;
            int ok = cupidimage_pdf_clip_render_geometry(state, clip);
            assert(ok == !insufficient);
            if (ok) {
                assert(!budget.tests_left && clip->coverage_bytes == 512 * 512);
                assert(budget.bytes_used == geometry_bytes + clip->coverage_bytes);
                for (unsigned y = 0; y < 512; y++)
                    for (unsigned x = 0; x < 512; x++) {
                        unsigned pixel = y * 512 + x;
                        unsigned covered = !empty && x >= 1 && x < 3 && y >= 1 && y < 3;
                        assert(clip->coverage[pixel] == (covered ? 255 : 0));
                        assert(((clip->bits[pixel / 8] >> (pixel % 8)) & 1) == covered);
                        checks++;
                    }
                budget.bytes_used -= clip->coverage_bytes;
                free(clip->coverage);
            } else {
                assert(!clip->coverage && !clip->coverage_bytes);
                assert(budget.tests_left == required - 1 && budget.bytes_used == geometry_bytes);
            }
            free(clip);
            checks++;
        }
        cupidimage_pdf_clip_geometry_release(geometry, &budget.bytes_used);
        assert(!budget.bytes_used);
    }
    free(state);
    printf("PDF clip work: %u bounded-work and bitmap checks passed\n", checks);
    return 0;
}
