#include "formats/pdf/pdf_render_internal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static double distance_squared(const pdf_seg *line, double x, double y) {
    double vx = (double)line->x1 - line->x0, vy = (double)line->y1 - line->y0;
    double dx = x - line->x0, dy = y - line->y0;
    double length = vx * vx + vy * vy;
    double t = length ? fmax(0, fmin(1, (dx * vx + dy * vy) / length)) : 0;
    dx -= t * vx;
    dy -= t * vy;
    return dx * dx + dy * dy;
}

static void initialize(pdf_render_state *state, pdf_form_budget *budget, cupidimage_image *image) {
    memset(state, 0, sizeof(*state));
    state->form_budget = budget;
    state->img = image;
    state->page_top_pts = 150;
    state->gs.ctm[0] = state->gs.ctm[3] = 1;
    state->gs.flatness = 1;
}

static int execute(pdf_render_state *state, const char *content) {
    return cupidimage_pdf_render_stream(state, (const unsigned char *)content, strlen(content));
}

int main(void) {
    pdf_render_state *state = calloc(1, sizeof(*state));
    assert(state);
    pdf_form_budget budget = {0};
    cupidimage_image image = {0};
    image.width = image.height = 240;
    const double values[] = {0, 100, 1, .25, .125, .01, .001};
    unsigned checks = 0;
    const float matrices[][6] = {
        {1, 0, 0, 1, 0, 0}, {2, 0, 0, .5, 5, 7}, {0, 1, -1, 0, 100, 0}, {1, .25, .5, 1, 0, 0}};
    for (unsigned matrix = 0; matrix < sizeof(matrices) / sizeof(*matrices); matrix++) {
        int previous = 0;
        for (unsigned i = 0; i < sizeof(values) / sizeof(*values); i++) {
            initialize(state, &budget, &image);
            memcpy(state->gs.ctm, matrices[matrix], sizeof(state->gs.ctm));
            char content[160];
            snprintf(content, sizeof(content), "%.6f i 0 0 m 0 75 75 75 75 0 c", values[i]);
            assert(execute(state, content) && cupidimage_pdf_path_flatten(state));
            assert(state->path.seg_count >= previous);
            previous = state->path.seg_count;
            double tolerance = fmin(values[i] ? values[i] : 1, .25);
            for (unsigned sample = 0; sample <= 1000; sample++) {
                double t = sample / 1000.0;
                double ux = 75 * (3 * t * t - 2 * t * t * t), uy = 225 * t * (1 - t);
                const float *m = matrices[matrix];
                double x = (m[0] * ux + m[2] * uy + m[4]) * (4.0 / 3.0);
                double y = (150 - m[1] * ux - m[3] * uy - m[5]) * (4.0 / 3.0);
                double closest = HUGE_VAL;
                for (int segment = 0; segment < state->path.seg_count; segment++)
                    closest = fmin(closest, distance_squared(state->path.segs + segment, x, y));
                assert(closest <= tolerance * tolerance);
                checks++;
            }
        }
    }
    initialize(state, &budget, &image);
    assert(execute(state, ".01 i q .001 i Q"));
    assert(state->gs.flatness == .01);
    assert(execute(state, "0 i") && state->gs.flatness == 1);
    checks += 2;
    const char *invalid[] = {"-1 i", "101 i", "i", "1 2 i", "true i", "[1] i", "/N i"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        initialize(state, &budget, &image);
        assert(!execute(state, invalid[i]));
        checks++;
    }
    initialize(state, &budget, &image);
    assert(!cupidimage_pdf_set_flatness(&state->gs, NAN));
    assert(!cupidimage_pdf_set_flatness(&state->gs, INFINITY));
    assert(execute(state, ".00000000000000000001 i 0 0 m 0 75 75 75 75 0 c"));
    assert(!cupidimage_pdf_path_flatten(state));
    assert(state->path.overflow);
    checks += 3;
    free(state);
    printf("PDF flatness: %u geometry and state checks passed\n", checks);
    return 0;
}
