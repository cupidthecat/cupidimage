#include "formats/pdf/pdf_render_internal.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            failures++;                                                                            \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
        }                                                                                          \
    } while (0)

int main(void) {
    pdf_render_state *state = calloc(1, sizeof(*state));
    CHECK(state != NULL);
    if (!state)
        return 1;
    state->gs.ctm[0] = state->gs.ctm[3] = 1;
    state->gs.flatness = 1;
    cupidimage_pdf_path_init(&state->path);
    cupidimage_pdf_path_move_user(state, 0, 0);
    for (unsigned i = 0; i < PDF_PATH_SEGMENT_LIMIT; i++) {
        cupidimage_pdf_path_line_user(state, (float)(i & 1), 1);
        CHECK(!state->path.overflow && state->path.seg_count == (int)i + 1);
        if (i + 1 == PDF_PATH_INLINE_SEGMENTS)
            CHECK(!state->path.extra);
        if (i == PDF_PATH_INLINE_SEGMENTS) {
            CHECK(state->path.extra && state->path.capacity == PDF_PATH_INLINE_SEGMENTS * 2);
            CHECK(!memcmp(state->path.extra, state->path.segs,
                          PDF_PATH_INLINE_SEGMENTS * sizeof(pdf_seg)));
        }
    }
    const pdf_seg *segments = cupidimage_pdf_path_segments(&state->path);
    for (unsigned i = 0; i < PDF_PATH_SEGMENT_LIMIT; i++) {
        CHECK(segments[i].x1 == (i & 1 ? (float)(4.0 / 3.0) : 0));
        CHECK(segments[i].starts_subpath == (i == 0));
    }
    CHECK(state->path.capacity == PDF_PATH_SEGMENT_LIMIT);
    cupidimage_pdf_path_line_user(state, 0, 0);
    CHECK(state->path.overflow && state->path.seg_count == (int)PDF_PATH_SEGMENT_LIMIT);
    CHECK(!cupidimage_pdf_path_valid(&state->path));
    cupidimage_pdf_path_clear(&state->path);
    CHECK(!state->path.extra && !state->path.capacity && !state->path.seg_count &&
          !state->path.overflow);
    CHECK(!cupidimage_pdf_path_reserve(&state->path, UINT_MAX));
    cupidimage_pdf_path_clear(&state->path);
    CHECK(!cupidimage_pdf_path_reserve(NULL, 1));
    state->path.seg_count = PDF_PATH_INLINE_SEGMENTS + 1;
    CHECK(!cupidimage_pdf_path_valid(&state->path));
    cupidimage_pdf_path_init(&state->path);
    for (unsigned i = 0; i < 5000; i++) {
        cupidimage_pdf_path_move_user(state, 0, 0);
        cupidimage_pdf_path_curve_to(state, 1, 0, 2, 0, 3, 0, 0);
    }
    CHECK(state->path.extra && state->path.seg_count == 5000);
    CHECK(cupidimage_pdf_path_flatten(state));
    CHECK(state->path.extra && state->path.seg_count == 5000);
    segments = cupidimage_pdf_path_segments(&state->path);
    for (unsigned i = 0; i < 5000; i++)
        CHECK(!segments[i].curve && segments[i].x0 == 0 && segments[i].x1 == 4 &&
              segments[i].starts_subpath);
    size_t work = 100000;
    pdf_region_edge *edges = NULL;
    unsigned count = 0;
    CHECK(cupidimage_pdf_path_fill_edges(&state->path, &work, &edges, &count));
    CHECK(count == 10000 && work == 80000);
    free(edges);
    cupidimage_pdf_path_clear(&state->path);
    free(state);
    printf("PDF path storage: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
