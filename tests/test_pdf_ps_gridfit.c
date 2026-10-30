#include "fault_alloc.h"
#include "formats/pdf/raster/font_coverage.h"

static void bitmap_free(void *buffer) {
    free(buffer);
}

#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/formats/pdf/fonts/cff.c"
#include "../src/formats/pdf/fonts/postscript/cff_shape.c"
#include "../src/formats/pdf/fonts/postscript/gridfit.c"
#include "../src/formats/pdf/fonts/postscript/page.c"
#include "../src/formats/pdf/fonts/postscript/private.c"
#include "../src/formats/pdf/fonts/postscript/transform.c"
#include "../src/formats/pdf/fonts/type2.c"
#include "fixtures/pdf-cff.inc"

static unsigned checks, failures;
#define CHECK(test)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(test)) {                                                                             \
            failures++;                                                                            \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test);                                \
        }                                                                                          \
    } while (0)

static int font_probe(const char *path, unsigned x_ppem, unsigned y_ppem, int bitmap) {
    FILE *file = fopen(path, "rb");
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
    int ok = fread(data, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    pdf_cff_font font = {0};
    if (!ok || !cupidimage_pdf_cff_init(&font, data, (size_t)length)) {
        free(data);
        return 2;
    }
    int32_t x_scale = (int32_t)(((uint64_t)x_ppem * 64 * 65536 + 500) / 1000);
    int32_t y_scale = (int32_t)(((uint64_t)y_ppem * 64 * 65536 + 500) / 1000);
    pdf_ps_gridfit_size size = {.x_scale = (x_scale + 32) / 64, .y_scale = (y_scale + 32) / 64};
    for (unsigned gid = 0; gid < font.chars.count; gid++) {
        pdf_cff_outline output = {0};
        size_t work = 10000000;
        int fitted = cupidimage_pdf_cff_hint_outline(&font, gid, &size, &output, &work);
        printf("glyph %u %d %.17g %.17g\n", gid, fitted, output.width, output.width_y);
        if (fitted)
            for (unsigned i = 0; i < output.count; i++) {
                const pdf_cff_segment *s = output.segments + i;
                printf("segment %u", s->kind);
                for (unsigned j = 0; j < (s->kind == PDF_CFF_CURVE ? 6u : 2u); j++)
                    printf(" %d", (int)round(s->points[j] * 64));
                putchar('\n');
            }
        if (fitted && bitmap) {
            double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            int first = 1;
            for (unsigned i = 0; i < output.count; i++) {
                const pdf_cff_segment *segment = output.segments + i;
                for (unsigned j = 0; j < (segment->kind == PDF_CFF_CURVE ? 3u : 1u); j++) {
                    double x = segment->points[j * 2], y = segment->points[j * 2 + 1];
                    if (first) {
                        x0 = x1 = x;
                        y0 = y1 = y;
                        first = 0;
                    }
                    if (x < x0)
                        x0 = x;
                    if (x > x1)
                        x1 = x;
                    if (y < y0)
                        y0 = y;
                    if (y > y1)
                        y1 = y;
                }
            }
            int left = (int)floor(x0), top = (int)ceil(y1), right = (int)ceil(x1),
                bottom = (int)floor(y0);
            unsigned char *mask = NULL;
            size_t raster_work = 10000000;
            const double matrix[6] = {1, 0, 0, -1, 0, 0};
            int rendered = cupidimage_pdf_font_coverage(NULL, &output, matrix, left, -top, right,
                                                        -bottom, &raster_work, &mask);
            printf("bitmap %d %d %d %d %d", rendered, left, top, right - left, top - bottom);
            if (rendered)
                for (int y = top - bottom - 1; y >= 0; y--)
                    for (int x = 0; x < right - left; x++)
                        printf(" %02x", mask[(size_t)y * (right - left) + x]);
            putchar('\n');
            bitmap_free(mask);
        }
        cupidimage_pdf_type2_free(&output);
    }
    cupidimage_pdf_cff_free(&font);
    free(data);
    return ok && !live_count ? 0 : 3;
}

int main(int argc, char **argv) {
    if (argc == 4 || (argc == 5 && !strcmp(argv[4], "--bitmaps")))
        return font_probe(argv[1], (unsigned)strtoul(argv[2], NULL, 10),
                          (unsigned)strtoul(argv[3], NULL, 10), argc == 5);
    if (argc != 1)
        return 1;
    pdf_ps_private_hints priv;
    cupidimage_pdf_ps_hints_init(&priv);
    pdf_ps_hint_program hints = {0};
    pdf_cff_segment segments[] = {{{10, 20}, PDF_CFF_MOVE},
                                  {{110, 20}, PDF_CFF_LINE},
                                  {{110, 220}, PDF_CFF_LINE},
                                  {{10, 220}, PDF_CFF_LINE},
                                  {{10, 20}, PDF_CFF_CLOSE}};
    pdf_cff_outline source = {0};
    source.segments = segments;
    source.count = 5;
    source.width = 650;
    source.width_y = 12;
    pdf_ps_gridfit_size size = {.x_scale = 1049, .y_scale = 1049};
    for (unsigned mode = 0; mode < 3; mode++) {
        hints.stem_count = mode ? 1 : 0;
        hints.stems[0] = (pdf_ps_stem){20, 200, 0};
        pdf_ps_hint_event event = {0, 1, mode == 2, {0x80}};
        hints.events = mode ? &event : NULL;
        hints.event_count = mode ? 1 : 0;
        size_t total = 0, needed = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            allocation_count = 0;
            fail_at = failure;
            size_t work = 1000000;
            pdf_cff_outline output;
            int ok = cupidimage_pdf_ps_gridfit(&priv, &hints, &source, &size, &output, &work);
            if (!failure) {
                total = allocation_count;
                needed = 1000000 - work;
            }
            CHECK(ok == !failure);
            if (ok) {
                CHECK(output.width == 650 && output.width_y == 12);
                CHECK(output.count == 6);
            } else
                CHECK(!output.segments && !output.count);
            cupidimage_pdf_type2_free(&output);
            CHECK(!live_count);
        }
        fail_at = 0;
        for (size_t budget = 0; budget <= needed + 1; budget++) {
            size_t work = budget;
            pdf_cff_outline output;
            int ok = cupidimage_pdf_ps_gridfit(&priv, &hints, &source, &size, &output, &work);
            CHECK(ok == (budget >= needed));
            CHECK(work <= budget);
            if (!ok)
                CHECK(!output.segments && !output.count);
            cupidimage_pdf_type2_free(&output);
            CHECK(!live_count);
        }
    }
    {
        pdf_cff_segment many[200], saved[200];
        for (unsigned i = 0; i < 40; i++) {
            memcpy(many + i * 5, segments, sizeof(segments));
            for (unsigned j = 0; j < 5; j++)
                many[i * 5 + j].points[0] += i * 3;
        }
        memcpy(saved, many, sizeof(many));
        pdf_cff_outline src = source;
        src.segments = many;
        src.count = 200;
        pdf_ps_hint_program trace = {0};
        trace.stem_count = 96;
        for (unsigned i = 0; i < 96; i++)
            trace.stems[i] = (pdf_ps_stem){i * 2, 1, 0};
        pdf_ps_hint_event events[2] = {{0, 96, 1, {0}}, {0, 96, 0, {0}}};
        memset(events[0].mask, 255, 12);
        memset(events[1].mask, 255, 12);
        trace.events = events;
        trace.event_count = 2;
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            allocation_count = 0;
            fail_at = failure;
            pdf_cff_outline output;
            size_t budget = 10000000;
            int fitted = cupidimage_pdf_ps_gridfit(&priv, &trace, &src, &size, &output, &budget);
            if (!failure)
                total = allocation_count;
            CHECK(fitted == !failure);
            CHECK(!memcmp(saved, many, sizeof(many)));
            if (!fitted)
                CHECK(!output.segments && !output.count);
            cupidimage_pdf_type2_free(&output);
            CHECK(!live_count);
        }
        CHECK(total >= 8);
        fail_at = 0;
    }
    hints.events = NULL;
    hints.event_count = 0;
    hints.stem_count = 0;
    for (unsigned field = 0; field < 10; field++) {
        pdf_ps_private_hints p = priv;
        pdf_ps_hint_program h = hints;
        pdf_ps_gridfit_size z = size;
        pdf_cff_outline src = source;
        switch (field) {
            case 0:
                p.counts[0] = 15;
                break;
            case 1:
                p.scalars[0] = NAN;
                break;
            case 2:
                p.invalid = 1;
                break;
            case 3:
                h.stem_count = 97;
                break;
            case 4:
                h.event_count = 1;
                break;
            case 5:
                h.diagnostics = 1;
                break;
            case 6:
                z.x_scale = 0;
                break;
            case 7:
                z.y_scale = 131073;
                break;
            case 8:
                src.composite = 1;
                break;
            case 9:
                src.count = 65537;
                break;
        }
        pdf_cff_outline output;
        size_t work = 1000000;
        CHECK(!cupidimage_pdf_ps_gridfit(&p, &h, &src, &z, &output, &work));
        CHECK(!output.segments && !output.count);
        CHECK(!live_count);
    }
    size_t work = 1000000;
    CHECK(!cupidimage_pdf_ps_gridfit(&priv, &hints, &source, &size, &source, &work));
    CHECK(source.segments == segments && source.count == 5);
    {
        const unsigned char *programs[] = {test_cff_font, test_cff_cid_font,
                                           test_cff_composite_font};
        const size_t lengths[] = {sizeof(test_cff_font), sizeof(test_cff_cid_font),
                                  sizeof(test_cff_composite_font)};
        for (unsigned sample = 0; sample < 3; sample++) {
            size_t total = 0;
            for (size_t failure = 0; failure <= total; failure++) {
                allocation_count = 0;
                fail_at = failure;
                pdf_cff_font font = {0};
                pdf_cff_outline output = {0};
                size_t budget = 10000000;
                int fitted = cupidimage_pdf_cff_init(&font, programs[sample], lengths[sample]) &&
                             cupidimage_pdf_cff_hint_outline(&font, font.chars.count - 1, &size,
                                                             &output, &budget);
                if (!failure)
                    total = allocation_count;
                CHECK(fitted == !failure);
                if (!fitted)
                    CHECK(!output.segments && !output.count);
                if (fitted) {
                    CHECK(output.width == 600 || output.width == 601);
                    const int32_t matrix[4] = {65536, 0, 0, 65536}, offset[2] = {64, -32};
                    double x = output.segments[0].points[0], y = output.segments[0].points[1];
                    CHECK(cupidimage_pdf_ps_outline_transform(&output, matrix, offset, &budget));
                    CHECK(output.segments[0].points[0] == x + 1 &&
                          output.segments[0].points[1] == y - .5);
                    if (output.count > 1) {
                        output.segments[output.count - 1].points[0] = NAN;
                        pdf_cff_segment saved = output.segments[0];
                        CHECK(
                            !cupidimage_pdf_ps_outline_transform(&output, matrix, offset, &budget));
                        CHECK(!memcmp(&saved, output.segments, sizeof(saved)));
                    }
                }
                cupidimage_pdf_type2_free(&output);
                cupidimage_pdf_cff_free(&font);
                CHECK(!live_count);
            }
        }
        fail_at = 0;
    }
    {
        pdf_cff_font font = {0};
        CHECK(cupidimage_pdf_cff_init(&font, test_cff_composite_font,
                                      sizeof(test_cff_composite_font)));
        double matrix[6] = {24, 0, 0, -24, 30.5, 80.25};
        pdf_cff_outline output = {0};
        int fitted = 0;
        size_t budget = 1000000;
        CHECK(cupidimage_pdf_cff_page_shape(&font, 4, matrix, &budget, &output, &fitted));
        CHECK(fitted && output.count == 10 && output.width == 600);
        size_t needed = 1000000 - budget;
        cupidimage_pdf_type2_free(&output);
        const size_t budgets[] = {0, 1, needed - 1, needed, needed + 1};
        for (unsigned i = 0; i < sizeof(budgets) / sizeof(*budgets); i++) {
            budget = budgets[i];
            fitted = 0;
            int ok = cupidimage_pdf_cff_page_shape(&font, 4, matrix, &budget, &output, &fitted);
            CHECK(ok == (budgets[i] >= needed));
            CHECK(fitted == ok);
            if (!ok)
                CHECK(!output.count && !output.segments);
            cupidimage_pdf_type2_free(&output);
        }
        for (unsigned sample = 0; sample < 5; sample++) {
            double m[6];
            memcpy(m, matrix, sizeof(m));
            if (sample == 0)
                m[0] = NAN;
            if (sample == 1)
                m[3] = -40000;
            if (sample == 2)
                m[4] = 1e30;
            if (sample == 3)
                m[3] = 0;
            if (sample == 4)
                m[0] = INFINITY;
            budget = 1000000;
            int ok = cupidimage_pdf_cff_page_shape(&font, 4, m, &budget, &output, &fitted);
            CHECK(ok == (sample == 3));
            CHECK(!fitted && !output.count && !output.segments);
            cupidimage_pdf_type2_free(&output);
        }
        cupidimage_pdf_cff_free(&font);
        CHECK(!live_count);
    }
    uint32_t random = 5177;
    for (unsigned trial = 0; trial < 2000; trial++) {
        random = random * 1664525u + 1013904223u;
        double low = (int)(random % 8193) - 4096;
        random = random * 1664525u + 1013904223u;
        double high = low + (random % (unsigned)(4097 - low));
        pdf_cff_segment points[] = {{{low, low}, PDF_CFF_MOVE},
                                    {{high, low}, PDF_CFF_LINE},
                                    {{high, high}, PDF_CFF_LINE},
                                    {{low, high}, PDF_CFF_LINE},
                                    {{low, low}, PDF_CFF_CLOSE}};
        pdf_cff_outline src = source;
        src.segments = points;
        pdf_ps_hint_program trace = {0};
        trace.stem_count = 1;
        trace.stems[0] = (pdf_ps_stem){low, high - low, 0};
        pdf_ps_private_hints p = priv;
        const double scales[] = {0, .00001, .039625, 1, 2, 4096};
        p.scalars[0] = scales[trial % 6];
        p.scalars[1] = trial % 4097;
        p.scalars[2] = trial % 4097;
        p.counts[0] = 2;
        p.arrays[0][0] = low;
        p.arrays[0][1] = high;
        p.counts[2] = 2;
        p.arrays[2][0] = low / 2;
        p.arrays[2][1] = high / 2;
        p.scalars[6] = trial % 2;
        random = random * 1664525u + 1013904223u;
        pdf_ps_gridfit_size z = {.x_scale = (int32_t)(random % 131072 + 1),
                                 .y_scale = (int32_t)(random % 131072 + 1)};
        pdf_cff_outline output;
        size_t budget = 1000000;
        int fitted = cupidimage_pdf_ps_gridfit(&p, &trace, &src, &z, &output, &budget);
        CHECK(fitted || (!output.segments && !output.count));
        for (unsigned i = 0; i < output.count; i++)
            for (unsigned j = 0; j < 6; j++)
                CHECK(isfinite(output.segments[i].points[j]));
        cupidimage_pdf_type2_free(&output);
        CHECK(!live_count);
    }
    printf("PostScript grid fitting: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
