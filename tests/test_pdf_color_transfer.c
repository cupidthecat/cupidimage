#include "formats/pdf/pdf_render_internal.h"
#include <math.h>
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

static int select_state(pdf_render_state *st, const char *text) {
    pdf_object_view view = {(const unsigned char *)text, strlen(text), NULL, NULL};
    pdf_graphics_state next = st->gs;
    if (!cupidimage_pdf_color_function_state(st, &view, &next))
        return 0;
    st->gs = next;
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 10) {
        double source[4], backdrop[4], result[4], aliased[4];
        int mode = atoi(argv[1]);
        if (mode < 0 || mode > 15)
            return 1;
        for (unsigned i = 0; i < 4; i++) {
            source[i] = strtod(argv[2 + i], NULL);
            backdrop[i] = strtod(argv[6 + i], NULL);
            aliased[i] = source[i];
        }
        cupidimage_pdf_blend_components(mode, source, backdrop, result, 4);
        cupidimage_pdf_blend_components(mode, aliased, backdrop, aliased, 4);
        for (unsigned i = 0; i < 4; i++) {
            if (result[i] != aliased[i])
                return 2;
            printf("%s%.17g", i ? " " : "", result[i]);
        }
        putchar('\n');
        return 0;
    }
    if (argc != 1)
        return 1;
    pdf_color_function_store store = {0};
    pdf_clip_budget budget = {0, 10000000, 4096};
    pdf_render_state st = {0};
    st.color_functions = &store;
    st.clip_budget = &budget;
    st.document = (const unsigned char *)"";
    double rgb[4] = {.2, .4, .6, 0}, result[4];
    CHECK(cupidimage_pdf_rgb_to_cmyk(&st, rgb, 1, result));
    CHECK(fabs(result[0] - .4) < 1e-12 && fabs(result[1] - .2) < 1e-12 && result[2] == 0 &&
          fabs(result[3] - .4) < 1e-12);
    const char *custom = "<< /BG << /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [.5] /N 1 >> /UCR << "
                         "/FunctionType 2 /Domain [0 1] /C0 [0] /C1 [-1] /N 1 >> >>";
    CHECK(select_state(&st, custom));
    unsigned count = store.count;
    for (unsigned i = 0; i < 40; i++)
        CHECK(select_state(&st, custom));
    CHECK(store.count == count);
    CHECK(cupidimage_pdf_rgb_to_cmyk(&st, rgb, 1, result));
    CHECK(result[0] == 1 && result[1] == 1 && fabs(result[2] - .8) < 1e-12 &&
          fabs(result[3] - .2) < 1e-12);
    pdf_graphics_state saved = st.gs;
    CHECK(select_state(&st, "<< /BG /Invalid /BG2 /Default >>"));
    CHECK(cupidimage_pdf_rgb_to_cmyk(&st, rgb, 1, result));
    CHECK(fabs(result[3] - .4) < 1e-12 && fabs(result[2] - .8) < 1e-12);
    CHECK(select_state(&st, "<< /UCR /Invalid /UCR2 /Default >>"));
    CHECK(cupidimage_pdf_rgb_to_cmyk(&st, rgb, 1, result));
    CHECK(result[2] == 0);
    st.gs = saved;
    CHECK(select_state(&st, "<< /BG2 null /UCR null >>"));
    CHECK(st.gs.black_generation == saved.black_generation &&
          st.gs.undercolor_removal == saved.undercolor_removal);
    CHECK(!select_state(&st, "<< /BG /Default >>"));
    CHECK(!select_state(&st, "<< /BG2 /Identity >>"));
    CHECK(!select_state(&st, "<< /UCR2 17 >>"));
    CHECK(!select_state(&st,
                        "<< /BG << /FunctionType 2 /Domain [0 1] /C0 [0 1] /C1 [1 0] /N 1 >> >>"));
    CHECK(st.gs.black_generation == saved.black_generation);
    for (unsigned a = 0; a < 11; a++)
        for (unsigned b = 0; b < 11; b++)
            for (unsigned c = 0; c < 11; c++) {
                double value[4] = {a / 10.0, b / 10.0, c / 10.0, 0};
                double k = 1 - fmax(value[0], fmax(value[1], value[2]));
                CHECK(cupidimage_pdf_rgb_to_cmyk(&st, value, 1, value));
                CHECK(fabs(value[3] - k * .5) < 1e-12);
                CHECK(fabs(value[0] - fmin(1, 1 - a / 10.0 + k)) < 1e-12);
                CHECK(fabs(value[1] - fmin(1, 1 - b / 10.0 + k)) < 1e-12);
                CHECK(fabs(value[2] - fmin(1, 1 - c / 10.0 + k)) < 1e-12);
            }
    budget.tests_left = 0;
    CHECK(!cupidimage_pdf_rgb_to_cmyk(&st, rgb, 1, result));
    CHECK(cupidimage_pdf_rgb_to_cmyk(NULL, rgb, 0, result));
    rgb[0] = NAN;
    CHECK(!cupidimage_pdf_rgb_to_cmyk(NULL, rgb, 0, result));
    cupidimage_pdf_color_functions_free(&store);
    CHECK(!store.head && !store.count && !store.bytes);
    printf("PDF black generation: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
