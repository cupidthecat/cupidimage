#include "fault_alloc.h"
#include "formats/pdf/raster/font_coverage.h"

static void bitmap_free(void *buffer) {
    free(buffer);
}

#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/formats/pdf/fonts/postscript/gridfit.c"
#include "../src/formats/pdf/fonts/postscript/page.c"
#include "../src/formats/pdf/fonts/postscript/type1_gridfit.c"
#include "../src/formats/pdf/fonts/postscript/type1_shape.c"
#include "../src/formats/pdf/fonts/type1/counter.c"
#include "../src/formats/pdf/fonts/type1/trace.c"
#include "../src/formats/pdf/fonts/type1_charstring.c"
#include "../src/formats/pdf/fonts/type2.c"

static unsigned checks, failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            failures++;                                                                            \
            fprintf(stderr, "Type 1 fitting failure at %u\n", __LINE__);                           \
        }                                                                                          \
    } while (0)

static int font_probe(int argc, char **argv) {
    if ((argc != 6 && argc != 7) || (argc == 7 && strcmp(argv[6], "--bitmaps")))
        return 1;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 1;
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return 1;
    }
    long length = ftell(file);
    if (length < 0 || length > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return 1;
    }
    unsigned char *data = malloc((size_t)length + 1);
    if (!data) {
        fclose(file);
        return 1;
    }
    int read_ok = fread(data, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    pdf_type1_font font;
    if (!read_ok ||
        !cupidimage_pdf_type1_init(&font, data, (size_t)length, strtoul(argv[2], NULL, 10),
                                   strtoul(argv[3], NULL, 10))) {
        free(data);
        return 2;
    }
    unsigned long xp = strtoul(argv[4], NULL, 10), yp = strtoul(argv[5], NULL, 10);
    if (!xp || xp > 2000 || !yp || yp > 2000) {
        cupidimage_pdf_type1_free(&font);
        free(data);
        return 2;
    }
    int32_t xs = (int32_t)(((uint64_t)xp * 64 * 65536 + 500) / 1000);
    int32_t ys = (int32_t)(((uint64_t)yp * 64 * 65536 + 500) / 1000);
    pdf_ps_gridfit_size size = {.x_scale = (xs + 32) / 64, .y_scale = (ys + 32) / 64};
    for (unsigned glyph = 0; glyph < font.glyph_count; glyph++) {
        pdf_cff_outline out = {0};
        size_t work = 10000000;
        int ok = cupidimage_pdf_type1_hint_outline(&font, glyph, &size, &out, &work);
        printf("glyph %s %d %.17g %.17g\n", font.glyphs[glyph].name, ok, out.width, out.width_y);
        if (ok) {
            for (unsigned i = 0; i < out.count; i++) {
                pdf_cff_segment *s = out.segments + i;
                printf("segment %u", s->kind);
                if (s->kind != PDF_CFF_CLOSE)
                    for (unsigned j = 0; j < (s->kind == PDF_CFF_CURVE ? 6u : 2u); j++)
                        printf(" %d", (int)round(s->points[j] * 64));
                putchar('\n');
            }
            if (argc == 7 && !strcmp(argv[6], "--bitmaps")) {
                double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                int first = 1;
                for (unsigned i = 0; i < out.count; i++)
                    for (unsigned j = 0; j < (out.segments[i].kind == PDF_CFF_CURVE ? 3u : 1u);
                         j++) {
                        double x = out.segments[i].points[j * 2],
                               y = out.segments[i].points[j * 2 + 1];
                        if (first) {
                            x0 = x1 = x;
                            y0 = y1 = y;
                            first = 0;
                        }
                        x0 = fmin(x0, x);
                        x1 = fmax(x1, x);
                        y0 = fmin(y0, y);
                        y1 = fmax(y1, y);
                    }
                int left = (int)floor(x0), right = (int)ceil(x1), top = (int)ceil(y1),
                    bottom = (int)floor(y0);
                const double matrix[6] = {1, 0, 0, -1, 0, 0};
                unsigned char *mask = NULL;
                size_t raster_work = 10000000;
                int rendered = cupidimage_pdf_font_coverage(NULL, &out, matrix, left, -top, right,
                                                            -bottom, &raster_work, &mask);
                printf("bitmap %d %d %d %d %d", rendered, left, top, right - left, top - bottom);
                if (rendered)
                    for (int y = top - bottom - 1; y >= 0; y--)
                        for (int x = 0; x < right - left; x++)
                            printf(" %02x", mask[(size_t)y * (right - left) + x]);
                putchar('\n');
                bitmap_free(mask);
            }
        }
        cupidimage_pdf_type2_free(&out);
    }
    cupidimage_pdf_type1_free(&font);
    free(data);
    return live_count != 0;
}

int main(int argc, char **argv) {
    if (argc > 1)
        return font_probe(argc, argv);
    pdf_cff_segment segments[] = {{{0, 0}, PDF_CFF_MOVE},
                                  {{100, 0}, PDF_CFF_LINE},
                                  {{100, 700}, PDF_CFF_LINE},
                                  {{0, 700}, PDF_CFF_LINE},
                                  {{0, 0}, PDF_CFF_CLOSE}};
    pdf_cff_outline source = {.segments = segments, .count = 5, .width = 600, .width_y = -20};
    pdf_type1_hint_event events[] = {{PDF_T1_HINT_STEM, 0, 0, {0, 20}},
                                     {PDF_T1_HINT_STEM, 0, 0, {680, 20}}};
    pdf_type1_hint_trace trace = {.events = events, .count = 2};
    pdf_ps_private_hints hints;
    cupidimage_pdf_ps_hints_init(&hints);
    const double blues[] = {-20, 0, 680, 700};
    cupidimage_pdf_ps_hint_set(&hints, PDF_PS_BLUE_VALUES, blues, 4, 0, 1);
    pdf_ps_gridfit_size size = {.x_scale = 1049, .y_scale = 1049};
    pdf_ps_private_hints saved_hints = hints;
    pdf_cff_segment saved_segments[5];
    pdf_type1_hint_event saved_events[2];
    memcpy(saved_segments, segments, sizeof(segments));
    memcpy(saved_events, events, sizeof(events));
    pdf_cff_outline out;
    size_t work = 1000000;
    CHECK(cupidimage_pdf_ps_gridfit_type1(&hints, &trace, &source, &size, &out, &work));
    size_t used = 1000000 - work;
    CHECK(out.count && out.width == 600 && out.width_y == -20);
    for (unsigned i = 0; i < out.count; i++)
        for (unsigned j = 0; j < (out.segments[i].kind == PDF_CFF_CURVE ? 6u : 2u); j++)
            CHECK(out.segments[i].points[j] * 64 == floor(out.segments[i].points[j] * 64));
    cupidimage_pdf_type2_free(&out);
    CHECK(!memcmp(&hints, &saved_hints, sizeof(hints)) &&
          !memcmp(segments, saved_segments, sizeof(segments)) &&
          !memcmp(events, saved_events, sizeof(events)));
    for (size_t budget = 0; budget <= used; budget++) {
        work = budget;
        int ok = cupidimage_pdf_ps_gridfit_type1(&hints, &trace, &source, &size, &out, &work);
        CHECK(ok == (budget == used));
        CHECK(work <= budget);
        if (!ok)
            CHECK(!out.segments && !out.count);
        cupidimage_pdf_type2_free(&out);
        CHECK(live_count == 0);
    }
    size_t total = 0;
    for (size_t failure = 0; failure <= total; failure++) {
        allocation_count = 0;
        fail_at = failure;
        work = 1000000;
        int ok = cupidimage_pdf_ps_gridfit_type1(&hints, &trace, &source, &size, &out, &work);
        if (!failure)
            total = allocation_count;
        CHECK(ok == !failure);
        if (!ok)
            CHECK(!out.segments && !out.count);
        cupidimage_pdf_type2_free(&out);
        CHECK(!live_count);
    }
    fail_at = 0;
    for (unsigned kind = 0; kind < 20; kind++) {
        pdf_ps_private_hints h = hints;
        pdf_type1_hint_trace t = trace;
        pdf_cff_outline s = source;
        pdf_ps_gridfit_size z = size;
        pdf_type1_hint_event e[2];
        memcpy(e, events, sizeof(e));
        t.events = e;
        pdf_cff_segment p[5];
        memcpy(p, segments, sizeof(p));
        s.segments = p;
        switch (kind) {
            case 0:
                t.count = 65537;
                break;
            case 1:
                t.events = NULL;
                break;
            case 2:
                e[0].kind = PDF_T1_HINT_COUNTER + 1;
                break;
            case 3:
                e[0].vertical = 2;
                break;
            case 4:
                e[0].segment = 6;
                break;
            case 5:
                e[0].segment = 3;
                e[1].segment = 2;
                break;
            case 6:
                e[0].values[0] = NAN;
                break;
            case 7:
                e[0].values[1] = -22;
                break;
            case 8:
                p[1].points[0] = INFINITY;
                break;
            case 9:
                p[1].points[0] = 4097;
                break;
            case 10:
                p[0].kind = PDF_CFF_LINE;
                break;
            case 11:
                p[1].kind = PDF_CFF_MOVE;
                break;
            case 12:
                p[4].kind = PDF_CFF_LINE;
                break;
            case 13:
                s.composite = 1;
                break;
            case 14:
                s.segments = NULL;
                break;
            case 15:
                z.x_scale = 0;
                break;
            case 16:
                z.y_scale = 2 * 65536 + 1;
                break;
            case 17:
                z.origin_y = 4096 * 65536 + 1;
                break;
            case 18:
                h.invalid = 1u << PDF_PS_BLUE_SCALE;
                break;
            case 19:
                h.counts[0] = 15;
                break;
        }
        work = 1000000;
        CHECK(!cupidimage_pdf_ps_gridfit_type1(&h, &t, &s, &z, &out, &work));
        CHECK(!out.segments && !out.count && !live_count);
    }
    hints.invalid = (1u << PDF_PS_STEM_SNAP_H) | (1u << PDF_PS_FORCE_BOLD);
    work = 1000000;
    CHECK(cupidimage_pdf_ps_gridfit_type1(&hints, &trace, &source, &size, &out, &work));
    cupidimage_pdf_type2_free(&out);
    hints = saved_hints;
    segments[4].kind = PDF_CFF_END;
    work = 1000000;
    CHECK(cupidimage_pdf_ps_gridfit_type1(&hints, &trace, &source, &size, &out, &work));
    CHECK(out.count == 5 && out.segments[out.count - 1].kind == PDF_CFF_END);
    const int32_t matrix[4] = {65536, 0, 0, 65536}, offset[2] = {64, -128};
    work = 1000000;
    CHECK(cupidimage_pdf_ps_outline_transform(&out, matrix, offset, &work));
    CHECK(out.segments[0].points[0] == 1 && out.segments[0].points[1] == -2 &&
          out.segments[4].kind == PDF_CFF_END && out.width == 600);
    cupidimage_pdf_type2_free(&out);
    segments[4].kind = PDF_CFF_CLOSE;
    pdf_type1_hint_event many[97];
    for (unsigned i = 0; i < 97; i++)
        many[i] = (pdf_type1_hint_event){PDF_T1_HINT_STEM, 0, 0, {i * 3, 2}};
    trace.events = many;
    for (unsigned n = 96; n <= 97; n++) {
        trace.count = n;
        work = 10000000;
        int ok = cupidimage_pdf_ps_gridfit_type1(&hints, &trace, &source, &size, &out, &work);
        CHECK(ok == (n == 96));
        cupidimage_pdf_type2_free(&out);
        CHECK(!live_count);
    }
    trace.events = events;
    trace.count = 2;
    uint32_t seed = 0x1251;
    for (unsigned i = 0; i < 2000; i++) {
        seed = seed * 1664525u + 1013904223u;
        pdf_ps_gridfit_size z = size;
        z.x_scale = 128 + (int32_t)(seed % 4096);
        z.y_scale = 128 + (int32_t)((seed >> 12) % 4096);
        z.origin_x = (int32_t)((seed >> 24) % 32) * 65536;
        z.origin_y = -((int32_t)((seed >> 16) % 32)) * 65536;
        segments[4].kind = i % 2 ? PDF_CFF_END : PDF_CFF_CLOSE;
        work = 1000000;
        CHECK(cupidimage_pdf_ps_gridfit_type1(&hints, &trace, &source, &z, &out, &work));
        cupidimage_pdf_type2_free(&out);
        CHECK(!live_count);
    }
    segments[4].kind = PDF_CFF_CLOSE;
    const unsigned char empty[] = {139, 248, 236, 13, 14};
    const unsigned char base[] = {139, 248, 236, 13, 139, 159, 1,   248, 216, 159,
                                  1,   139, 139, 21, 248, 36,  139, 5,   139, 248,
                                  236, 5,   252, 36, 139, 5,   9,   14};
    const unsigned char accent[] = {139, 247, 92,  13, 139, 159, 1, 219, 159, 1, 139, 139,
                                    21,  239, 139, 5,  139, 239, 5, 39,  139, 5, 9,   14};
    const unsigned char composite[] = {139, 248, 236, 13, 139, 239, 248, 236, 204, 247, 86, 12, 6};
    pdf_type1_glyph glyphs[4] = {0};
    strcpy(glyphs[0].name, ".notdef");
    glyphs[0].code = (pdf_cff_slice){empty, sizeof(empty)};
    strcpy(glyphs[1].name, "A");
    glyphs[1].code = (pdf_cff_slice){base, sizeof(base)};
    strcpy(glyphs[2].name, "Aacute");
    glyphs[2].code = (pdf_cff_slice){composite, sizeof(composite)};
    strcpy(glyphs[3].name, "acute");
    glyphs[3].code = (pdf_cff_slice){accent, sizeof(accent)};
    pdf_type1_font font = {0};
    font.glyphs = glyphs;
    font.glyph_count = 4;
    font.hints = hints;
    total = 0;
    for (size_t failure = 0; failure <= total; failure++) {
        allocation_count = 0;
        fail_at = failure;
        work = 1000000;
        int ok = cupidimage_pdf_type1_hint_outline(&font, 2, &size, &out, &work);
        if (!failure)
            total = allocation_count;
        CHECK(ok == !failure);
        if (ok)
            CHECK(out.count == 12 && out.width == 600);
        else
            CHECK(!out.segments && !out.count);
        cupidimage_pdf_type2_free(&out);
        CHECK(!live_count);
    }
    fail_at = 0;
    glyphs[1].code = glyphs[2].code;
    work = 1000000;
    CHECK(!cupidimage_pdf_type1_hint_outline(&font, 1, &size, &out, &work));
    CHECK(!out.segments && !out.count && !live_count);
    work = 1000000;
    CHECK(!cupidimage_pdf_type1_hint_outline(&font, 2, &size, &out, &work));
    CHECK(!out.segments && !out.count && !live_count);
    work = 1000000;
    CHECK(cupidimage_pdf_type1_hint_outline(&font, 0, &size, &out, &work));
    CHECK(!out.segments && !out.count && out.width == 600);
    cupidimage_pdf_type2_free(&out);
    pdf_ps_gridfit_size zero = size;
    zero.y_scale = 0;
    work = 1000000;
    CHECK(!cupidimage_pdf_type1_hint_outline(&font, 0, &zero, &out, &work));
    CHECK(!out.segments && !out.count && !live_count);
    glyphs[1].code = (pdf_cff_slice){base, sizeof(base)};
    font.matrix[0] = font.matrix[3] = .001;
    double text_matrix[6] = {24, 0, 0, -24, 40, 80};
    int fitted = 0;
    total = 0;
    for (size_t failure = 0; failure <= total; failure++) {
        allocation_count = 0;
        fail_at = failure;
        work = 1000000;
        int ok = cupidimage_pdf_type1_page_shape(&font, 2, text_matrix, &work, &out, &fitted);
        if (!failure)
            total = allocation_count;
        CHECK(ok == !failure && fitted == !failure);
        if (ok)
            CHECK(out.count == 10 && out.width == 600 && out.segments[0].points[0] == 40 &&
                  out.segments[0].points[1] == 80);
        else
            CHECK(!out.segments && !out.count);
        cupidimage_pdf_type2_free(&out);
        CHECK(!live_count && work <= 1000000);
    }
    fail_at = 0;
    for (unsigned field = 0; field < 6; field++) {
        double original = text_matrix[field];
        text_matrix[field] = NAN;
        work = 1000000;
        CHECK(!cupidimage_pdf_type1_page_shape(&font, 1, text_matrix, &work, &out, &fitted));
        CHECK(!fitted && !out.segments && !out.count);
        text_matrix[field] = original;
        original = font.matrix[field];
        font.matrix[field] = NAN;
        work = 1000000;
        CHECK(!cupidimage_pdf_type1_page_shape(&font, 1, text_matrix, &work, &out, &fitted));
        CHECK(!fitted && !out.segments && !out.count);
        font.matrix[field] = original;
    }
    text_matrix[0] = 0;
    work = 1000000;
    CHECK(cupidimage_pdf_type1_page_shape(&font, 1, text_matrix, &work, &out, &fitted));
    CHECK(!fitted && !out.segments && !out.count);
    text_matrix[0] = 24;
    font.matrix[0] = 0;
    work = 1000000;
    CHECK(cupidimage_pdf_type1_page_shape(&font, 1, text_matrix, &work, &out, &fitted));
    CHECK(!fitted && !out.segments && !out.count);
    font.matrix[0] = .001;
    font.matrix[4] = .00025;
    font.matrix[5] = -.00025;
    text_matrix[4] = 40.00390625;
    text_matrix[5] = 80.00390625;
    work = 1000000;
    CHECK(cupidimage_pdf_type1_page_shape(&font, 1, text_matrix, &work, &out, &fitted));
    CHECK(fitted && out.segments[0].points[0] == 40.015625 &&
          out.segments[0].points[1] == 80.015625);
    cupidimage_pdf_type2_free(&out);
    CHECK(!live_count);
    text_matrix[4] = 1e9;
    work = 1000000;
    CHECK(!cupidimage_pdf_type1_page_shape(&font, 1, text_matrix, &work, &out, &fitted));
    CHECK(!fitted && !out.segments && !out.count && !live_count);
    printf("Type 1 grid fitting: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
