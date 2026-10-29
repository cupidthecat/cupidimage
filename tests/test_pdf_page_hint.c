#include "fixtures/pdf-truetype.inc"
#include "formats/pdf/fonts/page_hint.h"
#include "formats/pdf/fonts/program.h"
#include "formats/pdf/pdf_internal.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pdf_font_program *program_new(void) {
    pdf_font_program *program = calloc(1, sizeof(*program));
    assert(program);
    program->references = 1;
    assert(cupidimage_pdf_tt_init(&program->font, test_truetype_font, sizeof(test_truetype_font)));
    return program;
}

int main(void) {
    unsigned checks = 0;
    pdf_font_program *program = program_new();
    pdf_page_hint *cache = NULL;
    const double matrix[6] = {.024, 0, 0, -.024, 20, 80};
    for (size_t budget = 0; budget < 20000; budget += 17) {
        size_t work = budget;
        pdf_tt_hint_shape shape = {0};
        int ok = cupidimage_pdf_page_hint_shape(&cache, program, 1, matrix, 1, &work, &shape);
        assert(work <= budget);
        if (ok) {
            assert(cache && program->references == 2 && shape.count == 4);
            assert(shape.points[0].x == 20 && shape.points[0].y == 80);
        } else
            assert(!cache && !shape.points && !shape.count);
        cupidimage_pdf_page_hint_free(&cache);
        assert(!cache && program->references == 1);
        if (ok)
            assert(shape.points[0].x == 20); /* Output is independent of the prepared instance. */
        cupidimage_pdf_tt_hint_shape_free(&shape);
        checks++;
    }
    const double bad[][6] = {{NAN, 0, 0, -.024, 20, 80},    {1, 0, 0, INFINITY, 20, 80},
                             {.024, 0, 0, -32.768, 20, 80}, {1e20, 0, 0, -.024, 20, 80},
                             {.024, 0, 0, -.024, 1e20, 80}, {.024, 0, 0, -.024, 20, -1e20}};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        size_t work = 1000000;
        pdf_tt_hint_shape shape = {0};
        assert(cupidimage_pdf_page_hint_shape(&cache, program, 1, matrix, 1, &work, &shape));
        cupidimage_pdf_tt_hint_shape_free(&shape);
        assert(!cupidimage_pdf_page_hint_shape(&cache, program, 1, bad[i], 1, &work, &shape));
        assert(!cache && !shape.points && program->references == 1);
        checks++;
    }
    size_t work = 1000000;
    pdf_tt_hint_shape shape = {0};
    assert(!cupidimage_pdf_page_hint_shape(&cache, program, program->font.glyphs, matrix, 1, &work,
                                           &shape));
    assert(!cache && !shape.points && program->references == 1);
    checks++;
    work = SIZE_MAX;
    assert(cupidimage_pdf_page_hint_shape(&cache, program, 1, matrix, 1, &work, &shape));
    assert(work < SIZE_MAX && program->references == 2);
    cupidimage_pdf_tt_hint_shape_free(&shape);
    cupidimage_pdf_font_program_free(program); /* The cache now owns the only reference. */
    program = program_new();
    work = 1000000;
    assert(cupidimage_pdf_page_hint_shape(&cache, program, 1, matrix, 0, &work, &shape));
    assert(program->references == 2);
    cupidimage_pdf_font_program_free(program);
    cupidimage_pdf_page_hint_free(&cache);
    assert(!cache && shape.points[0].x == 20);
    cupidimage_pdf_tt_hint_shape_free(&shape);
    checks += 2;
    program = program_new();
    program->references = UINT_MAX;
    assert(!cupidimage_pdf_font_program_retain(program));
    program->references = 0;
    assert(cupidimage_pdf_font_program_retain(program) && program->references == 2);
    cupidimage_pdf_font_program_free(program);
    assert(program->references == 1);
    cupidimage_pdf_font_program_free(program);
    checks += 2;
    printf("PDF page hint frame/cache: %u bounds, budget and ownership checks passed\n", checks);
    return 0;
}
