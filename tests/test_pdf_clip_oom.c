#include "fault_alloc.h"

#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/compression/ccitt.c"
#include "../src/core/image.c"
#include "../src/formats/jpeg/cupidimage_jpeg.c"
#include "../src/formats/jpeg/jpeg_arithmetic.c"
#include "../src/formats/jpeg/jpeg_entropy.c"
#include "../src/formats/jpeg/jpeg_output.c"
#include "../src/formats/pdf/cupidimage_pdf.c"
#include "../src/formats/pdf/fonts/bitmap.c"
#include "../src/formats/pdf/fonts/cff.c"
#include "../src/formats/pdf/fonts/cmap.c"
#include "../src/formats/pdf/fonts/encoding.c"
#include "../src/formats/pdf/fonts/page_hint.c"
#include "../src/formats/pdf/fonts/postscript/cff_shape.c"
#include "../src/formats/pdf/fonts/postscript/gridfit.c"
#include "../src/formats/pdf/fonts/postscript/page.c"
#include "../src/formats/pdf/fonts/postscript/private.c"
#include "../src/formats/pdf/fonts/postscript/transform.c"
#include "../src/formats/pdf/fonts/postscript/type1_gridfit.c"
#include "../src/formats/pdf/fonts/postscript/type1_shape.c"
#include "../src/formats/pdf/fonts/standard.c"
#include "../src/formats/pdf/fonts/standard_outlines.c"
#include "../src/formats/pdf/fonts/truetype.c"
#include "../src/formats/pdf/fonts/truetype_cmap.c"
#include "../src/formats/pdf/fonts/truetype_names.c"
#include "../src/formats/pdf/fonts/type1.c"
#include "../src/formats/pdf/fonts/type1/counter.c"
#include "../src/formats/pdf/fonts/type1/hints.c"
#include "../src/formats/pdf/fonts/type1/lexer.c"
#include "../src/formats/pdf/fonts/type1/trace.c"
#include "../src/formats/pdf/fonts/type1_charstring.c"
#include "../src/formats/pdf/fonts/type2.c"
#include "../src/formats/pdf/pdf_calculator.c"
#include "../src/formats/pdf/pdf_calibrated.c"
#include "../src/formats/pdf/pdf_content_syntax.c"
#include "../src/formats/pdf/pdf_document.c"
#include "../src/formats/pdf/pdf_filters.c"
#include "../src/formats/pdf/pdf_font_cid.c"
#include "../src/formats/pdf/pdf_font_encoding.c"
#include "../src/formats/pdf/pdf_font_outline.c"
#include "pdf_tt_loader_sources.inc"
static int test_outline_cache_enabled = 1;

static int test_font_outline(pdf_font_program *program, unsigned glyph, pdf_tt_outline *truetype,
                             pdf_cff_outline *postscript, size_t *budget, int *borrowed) {
    return cupidimage_pdf_font_outline(program, glyph, truetype, postscript,
                                       test_outline_cache_enabled ? budget : NULL, borrowed);
}

#define cupidimage_pdf_font_outline test_font_outline
#include "../src/formats/pdf/pdf_font_program.c"
#undef cupidimage_pdf_font_outline
#include "../src/formats/pdf/pdf_font_substitution.c"
#include "../src/formats/pdf/pdf_font_truetype.c"
#include "../src/formats/pdf/pdf_font_type3.c"
#include "../src/formats/pdf/pdf_fonts.c"
#include "../src/formats/pdf/pdf_functions.c"
#include "../src/formats/pdf/pdf_groups.c"
#include "../src/formats/pdf/pdf_icc.c"
#include "../src/formats/pdf/pdf_icc_curve.c"
#include "../src/formats/pdf/pdf_icc_lut.c"
#include "../src/formats/pdf/pdf_icc_mab.c"
#include "../src/formats/pdf/pdf_image_colors.c"
#include "../src/formats/pdf/pdf_images.c"
#include "../src/formats/pdf/pdf_inline.c"
#include "../src/formats/pdf/pdf_mesh.c"
#include "../src/formats/pdf/pdf_object_values.c"
#include "../src/formats/pdf/pdf_objects.c"
#include "../src/formats/pdf/pdf_palettes.c"
#include "../src/formats/pdf/pdf_patch.c"
#include "../src/formats/pdf/pdf_path.c"
#include "../src/formats/pdf/pdf_pattern_render.c"
#include "../src/formats/pdf/pdf_patterns.c"
#include "../src/formats/pdf/pdf_render.c"
#include "../src/formats/pdf/pdf_resources.c"
#include "../src/formats/pdf/pdf_shading.c"
#include "../src/formats/pdf/pdf_soft_masks.c"
#include "../src/formats/pdf/pdf_standard_font.c"
#include "../src/formats/pdf/pdf_stroke.c"
#include "../src/formats/pdf/pdf_text.c"
#include "../src/formats/pdf/pdf_tint.c"
static int test_text_lazy_enabled = 1;

static int test_text_surface(const pdf_render_state *parent, pdf_transparency_group *group,
                             int isolated, const double background[3]) {
    if (!test_text_lazy_enabled)
        group->lazy_backdrop = 0;
    return cupidimage_pdf_group_surface(parent, group, isolated, background);
}

#define cupidimage_pdf_group_surface test_text_surface
#include "../src/formats/pdf/pdf_paint_groups.c"
#include "../src/formats/pdf/pdf_text_groups.c"
#undef cupidimage_pdf_group_surface
#define cupidimage_pdf_font_outline test_font_outline
#include "../src/formats/pdf/pdf_text_outline.c"
#include "../src/formats/pdf/raster/coverage.c"
#include "../src/formats/pdf/raster/font_coverage.c"
#undef cupidimage_pdf_font_outline
#include "../src/formats/pdf/pdf_intents.c"
#include "../src/formats/pdf/pdf_transparency.c"
#include "../src/formats/pdf/pdf_xref.c"
#include "fixtures/pdf-cff.inc"
#include "fixtures/pdf-truetype.inc"
#include "fixtures/pdf-type1.inc"
#undef malloc
#undef calloc
#undef realloc
#undef free

static int test_document_allocations(const char *path, int expected_failure, int antialias,
                                     int hinting, int cff_hinting, int type1_hinting) {
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END))
        return 2;
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return 2;
    }
    unsigned char *document = malloc((size_t)length);
    if (!document || fread(document, 1, (size_t)length, file) != (size_t)length) {
        free(document);
        fclose(file);
        return 2;
    }
    fclose(file);
    cupidimage_pdf_options options = {antialias, hinting, cff_hinting, type1_hinting};
    size_t total = 0, checks = 0, failures = 0, expected_size = 0;
    unsigned char *expected = NULL;
    unsigned width = 0, height = 0;
    /* Establish uncached pixels before testing cached rendering and failures. */
    test_outline_cache_enabled = 0;
    test_text_lazy_enabled = 0;
    fail_at = 0;
    allocation_count = 0;
    cupidimage_image uncached = {0};
    char baseline_error[256];
    int baseline_ok = cupidimage_load_pdf_page_with_options(
        document, (size_t)length, &uncached, 0, &options, baseline_error, sizeof(baseline_error));
    if (baseline_ok == expected_failure)
        failures++;
    if (baseline_ok) {
        width = uncached.width;
        height = uncached.height;
        expected_size = (size_t)width * height * 4;
        expected = malloc(expected_size);
        if (!expected)
            abort();
        memcpy(expected, uncached.rgba, expected_size);
    }
    fault_free(uncached.rgba);
    if (live_count) {
        fprintf(stderr, "uncached document leaked %zu blocks\n", live_count);
        abort();
    }
    checks++;
    test_outline_cache_enabled = 1;
    test_text_lazy_enabled = 1;
    for (size_t failure = 0; failure <= total; failure++) {
        allocation_count = 0;
        fail_at = failure;
        cupidimage_image image = {0};
        char error[256];
        int ok = cupidimage_load_pdf_page_with_options(document, (size_t)length, &image, 0,
                                                       &options, error, sizeof(error));
        if (!failure) {
            total = allocation_count;
            if (ok == expected_failure)
                failures++;
        }
        if (ok && (expected_failure || !expected || image.width != width ||
                   image.height != height || memcmp(expected, image.rgba, expected_size)))
            failures++;
        fault_free(image.rgba);
        checks++;
        if (live_count) {
            fprintf(stderr, "document allocation %zu leaked %zu blocks\n", failure, live_count);
            failures++;
            while (live_count)
                fault_free(live[0]);
        }
    }
    free(expected);
    free(document);
    printf("PDF document allocation failures: %zu checks, %zu failures\n", checks, failures);
    return failures != 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && argc <= 7)
        return test_document_allocations(
            argv[1], atoi(argv[2]), argc >= 4 ? atoi(argv[3]) : 0, argc >= 5 ? atoi(argv[4]) : 0,
            argc >= 6 ? atoi(argv[5]) : 0, argc >= 7 ? atoi(argv[6]) : 0);

    static const struct {
        const char *content;
        int valid;
    } fixtures[] = {{"1 J 2 j 2 M [1 2 3] .5 d 3 w 1 1 m 17 1 l 17 11 l S", 1},
                    {"[.01 .01] 0 d 1 w 0 6 m 18 6 l S", 1},
                    {"[0 0] 0 d 1 1 m 17 11 l S", 0},
                    {"0 0 9 6 re W n q 3 0 6 12 re W* n 1 0 0 rg 0 0 18 12 re f Q q q", 1},
                    {"q 0 0 9 12 re W n q 0 0 18 6 re W n Q Q 0 0 18 12 re W n", 1},
                    {"0 0 9 12 re W n q (unterminated", 0},
                    {"0 0 9 12 re W n /F0 Do 0 0 1 rg 0 6 18 6 re f", 1},
                    {"0 0 9 12 re W n 18 0 0 12 0 0 cm /Im0 Do", 1},
                    {"18 0 0 12 0 0 cm /Im1 Do", 1},
                    {"/G gs 3 w 0 6 m 18 6 l 0 6 m 18 6 l S", 1},
                    {"/G gs 0 0 18 12 re B", 1},
                    {"/FontForm Do", 1},
                    {"/Sampled sh", 1},
                    {"/Broken sh", 0},
                    {"/Calculator sh", 1},
                    {"18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /G ID x EI", 1},
                    {"18 0 0 12 0 0 cm BI /W 1 /H 1 /BPC 8 /CS /G /F /AHx ID ff> EI", 1},
                    {"BI /W 1 /H 1 /BPC 8 /CS /G ID x EIx", 0},
                    {"BT /F1 12 Tf 1 Tr (AB) Tj ET", 1},
                    {"BT /F1 12 Tf 6 Tr (AB) Tj ET 0 0 18 12 re f", 1},
                    {"BT /F1 12 Tf 7 Tr (AB) Tj ET 0 0 18 12 re f", 1},
                    {"/BadCalculator sh", 0},
                    {"BT /F1 6 Tf (AA) Tj ET", 1},
                    {"BT /F1 6 Tf (AA) Tj", 0},
                    {"BT /F1 6 Tf (AA) Tj (unterminated", 0},
                    {"BT /F1 6 Tf [(A) (B)] TJ ET", 1},
                    {"BT /F1 6 Tf [(A) true] TJ ET", 0},
                    {"BT /F1 6 Tf <4141> Tj ET", 1},
                    {"BT /F1 6 Tf <4141", 0},
                    {"BT /F1 6 Tf "
                     "(AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                     "AAAAAAAAAAAAAAAAAAAA) Tj ET",
                     1},
                    {"BT /F#00 6 Tf (A) Tj ET", 0}};

    const char *form_content = "q 0 0 18 6 re W n 1 0 0 rg 0 0 18 12 re f q";
    const char *font_content = "BT /F1 6 Tf 0 6 Td (Z) Tj ET";
    const char *cmap = "1 beginbfchar <5A> <0041> endbfchar";
    char document[3072];
    int document_size = snprintf(
        document, sizeof(document),
        "3 0 obj << /Resources << /XObject << /F0 5 0 R /Im0 6 0 R /Im1 8 0 R /FontForm 11 0 R >> "
        "/ExtGState << /G << /ca .5 /CA .5 /BM /Multiply >> >> /Shading << /Sampled 14 0 R /Broken "
        "17 0 R /Calculator 18 0 R /BadCalculator 20 0 R >> >> >> endobj\n"
        "5 0 obj << /Type /XObject /Subtype /Form /BBox [0 0 18 12] /Length %zu >>\n"
        "stream\n%s\nendstream\nendobj\n"
        "6 0 obj << /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceRGB "
        "/BitsPerComponent 8 /SMask 7 0 R /Filter /ASCIIHexDecode /Length 7 >> "
        "stream\n524742>\nendstream endobj\n"
        "7 0 obj << /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceGray "
        "/BitsPerComponent 8 /Matte [1 1 1] /Filter /ASCIIHexDecode /Length 3 >> "
        "stream\n4D>\nendstream endobj\n"
        "8 0 obj << /Subtype /Image /Width 1 /Height 1 /ColorSpace [/Indexed /DeviceRGB 0 9 0 R] "
        "/BitsPerComponent 8 /SMask 10 0 R /Filter /ASCIIHexDecode /Length 3 >> "
        "stream\n00>\nendstream endobj\n"
        "9 0 obj << /Filter /ASCIIHexDecode /Length 7 >> stream\n524742>\nendstream endobj\n"
        "10 0 obj << /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceGray "
        "/BitsPerComponent 8 /Length 1 >> stream\nM\nendstream endobj\n"
        "11 0 obj << /Subtype /Form /BBox [0 0 18 12] /Resources << /Font << /F1 12 0 R >> >> "
        "/Length %zu >> stream\n%s\nendstream endobj\n"
        "12 0 obj << /Type /Font /ToUnicode 13 0 R >> endobj\n"
        "13 0 obj << /Length %zu >> stream\n%s\nendstream endobj\n"
        "14 0 obj << /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 18 0] /Function 15 0 R >> "
        "endobj\n"
        "15 0 obj << /FunctionType 0 /Domain [0 1] /Range [0 1] /Size [2] /BitsPerSample 8 /Filter "
        "/ASCIIHexDecode /Length 5 >> stream\n00ff>\nendstream endobj\n"
        "16 0 obj << /FunctionType 2 /Domain [0 1] /C0 [0 1] /C1 [1] /N 1 >> endobj\n"
        "17 0 obj << /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 18 0] /Function [15 0 R 16 "
        "0 R 15 0 R] >> endobj\n"
        "18 0 obj << /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 18 0] /Function 19 0 R >> "
        "endobj\n"
        "19 0 obj << /FunctionType 4 /Domain [0 1] /Range [0 1] /Length 2 >> stream\n{}\nendstream "
        "endobj\n"
        "20 0 obj << /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 18 0] /Function [19 0 R 16 "
        "0 R 19 0 R] >> endobj\n",
        strlen(form_content), form_content, strlen(font_content), font_content, strlen(cmap), cmap);
    if (document_size < 0 || (size_t)document_size >= sizeof(document))
        return 1;
    size_t checks = 0, failures = 0;
    {
        unsigned char code[256] = {139, 149, 18};
        size_t length = 3;
        for (unsigned i = 0; i < 40; i++) {
            const unsigned char event[] = {19, 128, 139, 139, 21};
            memcpy(code + length, event, sizeof(event));
            length += sizeof(event);
        }
        code[length++] = 14;
        unsigned char offsets[] = {1, (unsigned char)(length + 1)};
        pdf_cff_private private_dict = {0};
        pdf_cff_font font = {0};
        font.chars = (pdf_cff_index){{offsets, sizeof(offsets)}, {code, length}, 1, 1};
        font.private_dicts = &private_dict;
        font.private_count = 1;
        for (unsigned truncated = 0; truncated < 2; truncated++) {
            offsets[1] = (unsigned char)(length + 1 - truncated);
            size_t total = 0;
            for (size_t failure = 0; failure <= total; failure++) {
                allocation_count = 0;
                fail_at = failure;
                pdf_cff_outline outline;
                pdf_ps_hint_program hints;
                int ok = cupidimage_pdf_type2_outline_hints(&font, 0, &outline, &hints);
                if (!failure)
                    total = allocation_count;
                checks++;
                if (ok != (!failure && !truncated))
                    failures++;
                if (ok && (hints.event_count != 40 || hints.stem_count != 1 || outline.count != 80))
                    failures++;
                if (!ok && (hints.events || hints.event_count || outline.segments || outline.count))
                    failures++;
                cupidimage_pdf_ps_hint_program_free(&hints);
                cupidimage_pdf_type2_free(&outline);
                if (live_count)
                    failures++;
            }
        }
        fail_at = 0;
    }

    {
        unsigned char code[512] = {139, 248, 236, 13};
        size_t length = 4;
        for (unsigned i = 0; i < 40; i++) {
            const unsigned char event[] = {149, 159, 1, 139, 139, 21};
            memcpy(code + length, event, sizeof(event));
            length += sizeof(event);
        }
        code[length++] = 14;
        pdf_type1_glyph glyph = {0};
        pdf_type1_font font = {0};
        font.glyphs = &glyph;
        font.glyph_count = 1;
        for (unsigned truncated = 0; truncated < 2; truncated++) {
            glyph.code = (pdf_cff_slice){code, length - truncated};
            size_t total = 0;
            for (size_t failure = 0; failure <= total; failure++) {
                allocation_count = 0;
                fail_at = failure;
                pdf_cff_outline outline;
                pdf_type1_hint_trace trace;
                int ok = cupidimage_pdf_type1_outline_hints(&font, 0, &outline, &trace);
                if (!failure)
                    total = allocation_count;
                checks++;
                if (ok != (!failure && !truncated))
                    failures++;
                if (ok && (trace.count != 40 || outline.count != 80))
                    failures++;
                if (!ok && (trace.events || trace.count || outline.segments || outline.count))
                    failures++;
                cupidimage_pdf_type1_trace_free(&trace);
                cupidimage_pdf_type2_free(&outline);
                if (live_count)
                    failures++;
            }
        }
        fail_at = 0;
    }

    for (size_t fixture = 0; fixture < sizeof(fixtures) / sizeof(fixtures[0]); fixture++) {
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            uint8_t pixels[24 * 16 * 4];
            cupidimage_image image = {0};
            image.width = 24;
            image.height = 16;
            image.rgba = pixels;
            pdf_page_geometry geometry = {0, 0, 18, 12, 0, 24, 16, 1};
            char error[128] = {0};
            allocation_count = 0;
            fail_at = failure;
            int ok = cupidimage_pdf_render_content_stream(
                (const unsigned char *)fixtures[fixture].content, strlen(fixtures[fixture].content),
                (const unsigned char *)document, (size_t)document_size, 3, NULL, &image, &geometry,
                error, sizeof(error));
            if (failure == 0)
                total = allocation_count;
            checks++;
            if (live_count || (failure == 0 && ok != fixtures[fixture].valid)) {
                fprintf(stderr,
                        "FAIL fixture %zu allocation %zu: %zu live allocations, result %d\n",
                        fixture, failure, live_count, ok);
                failures++;
            }
            while (live_count)
                fault_free(live[0]);
        }
    }
    {
        const char *shading_doc =
            "3 0 obj << /Resources << /Shading << /Sh 4 0 R >> >> >> endobj\n"
            "4 0 obj << /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 1.5 0] "
            "/Function << /FunctionType 2 /Domain [0 1] /N 1 >> >> endobj\n";
        uint8_t pixels[16];
        memset(pixels, 255, sizeof(pixels));
        cupidimage_image image = {0};
        image.width = 2;
        image.height = 2;
        image.rgba = pixels;
        pdf_form_budget budget = {0, 3, 3, 0, 0, 0, 0, 0, NULL};
        pdf_render_state state = {0};
        state.img = &image;
        state.document = (const unsigned char *)shading_doc;
        state.document_size = strlen(shading_doc);
        state.page_object = 3;
        state.page_top_pts = 1.5f;
        state.gs.ctm[0] = state.gs.ctm[3] = 1;
        state.gs.fill_alpha = 1;
        state.form_budget = &budget;
        fail_at = 0;
        allocation_count = 0;
        checks++;
        if (cupidimage_pdf_draw_shading(&state, "Sh") || budget.shade_evaluations_left != 3)
            failures++;
        budget.shade_evaluations_left = 4;
        checks++;
        if (!cupidimage_pdf_draw_shading(&state, "Sh") || budget.shade_evaluations_left ||
            pixels[0] != 64 || pixels[4] != 191)
            failures++;
        checks++;
        if (cupidimage_pdf_draw_shading(&state, "Sh"))
            failures++;
        if (live_count) {
            failures++;
            while (live_count)
                fault_free(live[0]);
        }
    }
    /* Triangle shading buffers, lattice rows and optional functions unwind together. */
    for (unsigned variant = 0; variant < 9; variant++) {
        int lattice = variant == 1 || variant == 4;
        int function = variant == 2 || variant == 5 || variant == 7;
        const char *hex = lattice    ? (variant == 4 ? "0000ff0000ff00ff000000ffff0000>"
                                                     : "0000ff0000ff00ff000000ffff0000ffffff0000>")
                          : function ? "0000000000ff00ff0000ff00>"
                          : variant == 3 ? "000000ff000000ff0000ff000000ff0000>"
                                         : "000000ff000000ff0000ff000000ff0000ff>";
        const char *content = variant == 6   ? "/Pattern cs /P scn 0 0 18 12 re f"
                              : variant == 8 ? "/M gs 0 0 18 12 re f"
                                             : "/Sh sh";
        const char *function_body =
            variant == 7   ? "<< /FunctionType 99 /Domain [0 1] >>"
            : variant == 5 ? "<< /FunctionType 4 /Domain [0 1] /Range [0 1] /Length 11 >> "
                             "stream\n{ dup mul }\nendstream"
                           : "<< /FunctionType 2 /Domain [0 1] /C0 [0 0 0] /C1 [1 0 0] /N 2 >>";
        char mesh_document[2048];
        int length = snprintf(
            mesh_document, sizeof(mesh_document),
            "3 0 obj << /Resources << /Shading << /Sh 5 0 R >> /Pattern << /P 7 0 R >> /ExtGState "
            "<< /M << /SMask << /S /Alpha /G 9 0 R >> >> >> >> >> endobj\n"
            "5 0 obj << /ShadingType %d /ColorSpace /%s /BitsPerCoordinate 8 /BitsPerComponent 8 "
            "%s /Decode [0 18 0 12 %s] %s /Filter /ASCIIHexDecode /Length %zu >> "
            "stream\n%s\nendstream endobj\n"
            "7 0 obj << /PatternType 2 /Shading 5 0 R >> endobj\n"
            "8 0 obj %s endobj\n"
            "9 0 obj << /Subtype /Form /BBox [0 0 18 12] /Group << /S /Transparency /I true /CS "
            "/DeviceRGB >> /Length 6 >> stream\n/Sh sh\nendstream endobj\n",
            lattice ? 5 : 4, variant == 5 ? "DeviceGray" : "DeviceRGB",
            lattice ? "/VerticesPerRow 2" : "/BitsPerFlag 8", function ? "0 1" : "0 1 0 1 0 1",
            function ? "/Function 8 0 R" : "", strlen(hex), hex, function_body);
        if (length < 0 || (size_t)length >= sizeof(mesh_document))
            return 1;
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            uint8_t pixels[24 * 16 * 4];
            memset(pixels, 255, sizeof(pixels));
            cupidimage_image image = {0};
            image.width = 24;
            image.height = 16;
            image.rgba = pixels;
            pdf_page_geometry geometry = {0, 0, 18, 12, 0, 24, 16, 1};
            char error[128] = {0};
            allocation_count = 0;
            fail_at = failure;
            int ok = cupidimage_pdf_render_content_stream(
                (const unsigned char *)content, strlen(content),
                (const unsigned char *)mesh_document, (size_t)length, 3, NULL, &image, &geometry,
                error, sizeof(error));
            if (!failure)
                total = allocation_count;
            checks++;
            if ((!failure && ok != (variant != 3 && variant != 4 && variant != 7)) || live_count) {
                fprintf(stderr, "FAIL mesh %u allocation %zu: %zu live, result %d\n", variant,
                        failure, live_count, ok);
                failures++;
            }
            if (!ok)
                for (size_t i = 0; i < sizeof(pixels); i++)
                    if (pixels[i] != 255) {
                        failures++;
                        break;
                    }
            while (live_count)
                fault_free(live[0]);
        }
        fail_at = 0;
    }
    {
        const char *data = "3 0 obj << >> endobj\n"
                           "5 0 obj << /ShadingType 4 /ColorSpace /DeviceRGB /BitsPerCoordinate 8 "
                           "/BitsPerComponent 8 /BitsPerFlag 8 /Decode [0 18 0 12 0 1 0 1 0 1] "
                           "/Filter /ASCIIHexDecode /Length 37 >> "
                           "stream\n000000ff000000ff0000ff000000ff0000ff>\nendstream endobj\n";
        for (unsigned limit = 0; limit < 3; limit++) {
            uint8_t pixels[24 * 16 * 4];
            memset(pixels, 255, sizeof(pixels));
            cupidimage_image image = {0};
            image.width = 24;
            image.height = 16;
            image.rgba = pixels;
            pdf_form_budget budget = {0};
            budget.bytes_left = 1024;
            budget.shade_evaluations_left = limit ? 1000000 : 0;
            budget.transparency_bytes_live = limit == 1 ? PDF_TRANSPARENCY_MEMORY_LIMIT : 0;
            size_t initial = budget.transparency_bytes_live;
            pdf_clip_budget clips = {0};
            clips.tests_left = limit == 2 ? 0 : 1024;
            pdf_render_state state = {0};
            state.img = &image;
            state.form_budget = &budget;
            state.clip_budget = &clips;
            state.document = (const unsigned char *)data;
            state.document_size = strlen(data);
            state.page_object = 3;
            state.page_top_pts = 12;
            state.gs.ctm[0] = state.gs.ctm[3] = 1;
            state.gs.fill_alpha = 1;
            pdf_object_view mesh = {0};
            allocation_count = 0;
            fail_at = 0;
            checks++;
            int got =
                cupidimage_pdf_get_object_content(state.document, state.document_size, 5, &mesh);
            if (!got || cupidimage_pdf_paint_mesh(&state, &mesh, 4, 0) ||
                budget.transparency_bytes_live != initial)
                failures++;
            cupidimage_pdf_object_view_release(&mesh);
            if (live_count) {
                failures++;
                while (live_count)
                    fault_free(live[0]);
            }
            for (size_t i = 0; i < sizeof(pixels); i++)
                if (pixels[i] != 255) {
                    failures++;
                    break;
                }
        }
    }
    /* Patch inverse maps add parameter-priority buffers to the mesh transaction. */
    for (unsigned variant = 0; variant < 9; variant++) {
        static const unsigned char order[16][2] = {{0, 0}, {0, 1}, {0, 2}, {0, 3}, {1, 3}, {2, 3},
                                                   {3, 3}, {3, 2}, {3, 1}, {3, 0}, {2, 0}, {1, 0},
                                                   {1, 1}, {1, 2}, {2, 2}, {2, 1}};
        int type = variant % 2 ? 7 : 6, function = variant == 2 || variant == 3;
        unsigned points = type == 6 ? 12 : 16;
        unsigned char raw[64];
        size_t used = 0;
        raw[used++] = variant == 5 ? 1 : 0;
        for (unsigned point = 0; point < points; point++) {
            raw[used++] = order[point][0] * 85;
            raw[used++] = variant == 7 ? (order[point][1] == 0 || order[point][1] == 3 ? 0 : 255)
                                       : order[point][1] * 85;
        }
        for (unsigned corner = 0; corner < 4; corner++) {
            if (function)
                raw[used++] = corner >= 2 ? 255 : 0;
            else {
                raw[used++] = 255;
                raw[used++] = 0;
                raw[used++] = 0;
            }
        }
        if (variant == 4)
            used--;
        char hex[129];
        for (size_t j = 0; j < used; j++)
            snprintf(hex + j * 2, 3, "%02x", raw[j]);
        hex[used * 2] = '>';
        hex[used * 2 + 1] = 0;
        const char *content = variant == 6   ? "/PS cs /P scn 0 0 18 12 re f"
                              : variant == 8 ? "/M gs 0 0 18 12 re f"
                                             : "/Sh sh";
        char patch_document[2048];
        int length = snprintf(
            patch_document, sizeof(patch_document),
            "3 0 obj << /Resources << /Shading << /Sh 5 0 R >> /Pattern << /P 7 0 R >> /ColorSpace "
            "<< /PS [/Pattern] >> /ExtGState << /M << /SMask << /S /Alpha /G 9 0 R >> >> >> >> >> "
            "endobj\n"
            "5 0 obj << /ShadingType %d /ColorSpace /DeviceRGB /BitsPerCoordinate 8 "
            "/BitsPerComponent 8 /BitsPerFlag 8 /Decode [0 18 0 12 %s] %s /Filter /ASCIIHexDecode "
            "/Length %zu >> stream\n%s\nendstream endobj\n"
            "7 0 obj << /PatternType 2 /Shading 5 0 R >> endobj\n"
            "8 0 obj << /FunctionType 2 /Domain [0 1] /C0 [0 0 0] /C1 [1 0 0] /N 2 >> endobj\n"
            "9 0 obj << /Subtype /Form /BBox [0 0 18 12] /Group << /S /Transparency /I true /CS "
            "/DeviceRGB >> /Length 6 >> stream\n/Sh sh\nendstream endobj\n",
            type, function ? "0 1" : "0 1 0 1 0 1", function ? "/Function 8 0 R" : "", strlen(hex),
            hex);
        if (length < 0 || (size_t)length >= sizeof(patch_document))
            return 1;
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            uint8_t pixels[24 * 16 * 4];
            memset(pixels, 255, sizeof(pixels));
            cupidimage_image image = {0};
            image.width = 24;
            image.height = 16;
            image.rgba = pixels;
            pdf_page_geometry geometry = {0, 0, 18, 12, 0, 24, 16, 1};
            char error[128] = {0};
            allocation_count = 0;
            fail_at = failure;
            int ok = cupidimage_pdf_render_content_stream(
                (const unsigned char *)content, strlen(content),
                (const unsigned char *)patch_document, (size_t)length, 3, NULL, &image, &geometry,
                error, sizeof(error));
            if (!failure)
                total = allocation_count;
            checks++;
            if ((!failure && ok != (variant != 4 && variant != 5)) || live_count) {
                fprintf(stderr, "FAIL patch %u allocation %zu: %zu live, result %d\n", variant,
                        failure, live_count, ok);
                failures++;
            }
            if (!ok)
                for (size_t j = 0; j < sizeof(pixels); j++)
                    if (pixels[j] != 255) {
                        failures++;
                        break;
                    }
            while (live_count)
                fault_free(live[0]);
        }
        fail_at = 0;
        if (variant == 0)
            for (unsigned limit = 0; limit < 3; limit++) {
                uint8_t pixels[24 * 16 * 4];
                memset(pixels, 255, sizeof(pixels));
                cupidimage_image image = {0};
                image.width = 24;
                image.height = 16;
                image.rgba = pixels;
                pdf_form_budget budget = {0};
                budget.bytes_left = 1024;
                budget.shade_evaluations_left = limit ? 1000000 : 100;
                budget.transparency_bytes_live =
                    limit == 1 ? PDF_TRANSPARENCY_MEMORY_LIMIT - sizeof(pixels) : 0;
                size_t initial = budget.transparency_bytes_live;
                pdf_clip_budget clips = {0};
                clips.tests_left = limit == 2 ? 0 : 1024;
                pdf_render_state state = {0};
                state.img = &image;
                state.form_budget = &budget;
                state.clip_budget = &clips;
                state.document = (const unsigned char *)patch_document;
                state.document_size = (size_t)length;
                state.page_object = 3;
                state.page_top_pts = 12;
                state.gs.ctm[0] = state.gs.ctm[3] = 1;
                state.gs.fill_alpha = 1;
                pdf_object_view patch = {0};
                allocation_count = 0;
                checks++;
                int got = cupidimage_pdf_get_object_content(state.document, state.document_size, 5,
                                                            &patch);
                if (!got || cupidimage_pdf_paint_mesh(&state, &patch, 6, 0) ||
                    budget.transparency_bytes_live != initial)
                    failures++;
                cupidimage_pdf_object_view_release(&patch);
                if (live_count) {
                    failures++;
                    while (live_count)
                        fault_free(live[0]);
                }
                for (size_t j = 0; j < sizeof(pixels); j++)
                    if (pixels[j] != 255) {
                        failures++;
                        break;
                    }
            }
    }
    /* Page isolation preserves the caller's pixels until a complete render succeeds. */
    for (unsigned variant = 0; variant < 4; variant++) {
        const char *attribute = variant == 0 ? ""
                                : variant == 3
                                    ? "/Group << /S /Transparency /CS /DeviceCMYK >>"
                                    : "/Group << /S /Transparency /CS /DeviceGray /I false >>";
        const char *body =
            variant == 2 ? "0 0 18 12 re f (unterminated" : "1 0 0 rg 0 0 18 12 re f";
        char page_document[256];
        int length =
            snprintf(page_document, sizeof(page_document), "3 0 obj << %s >> endobj", attribute);
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            uint8_t pixels[24 * 16 * 4];
            memset(pixels, 255, sizeof(pixels));
            cupidimage_image image = {0};
            image.width = 24;
            image.height = 16;
            image.rgba = pixels;
            pdf_page_geometry geometry = {0, 0, 18, 12, 0, 24, 16, 1};
            char error[128] = {0};
            allocation_count = 0;
            fail_at = failure;
            int ok = cupidimage_pdf_render_content_stream(
                (const unsigned char *)body, strlen(body), (const unsigned char *)page_document,
                (size_t)length, 3, NULL, &image, &geometry, error, sizeof(error));
            if (!failure)
                total = allocation_count;
            checks++;
            if ((!failure && ok != (variant < 2)) || live_count)
                failures++;
            if (!ok)
                for (size_t i = 0; i < sizeof(pixels); i++)
                    if (pixels[i] != 255) {
                        failures++;
                        break;
                    }
            while (live_count)
                fault_free(live[0]);
        }
        fail_at = 0;
    }
    /* Group buffers are atomic with respect to the parent and unwind at every allocation. */
    for (unsigned variant = 0; variant < 12; variant++) {
        const char *body = variant % 3 == 2   ? "/Inner Do"
                           : variant % 3 == 1 ? "0 0 18 12 re f (unterminated"
                                              : "0 0 18 12 re f";
        const char *inner = "0 0 18 12 re f";
        char group_document[1024];
        int length =
            snprintf(group_document, sizeof(group_document),
                     "3 0 obj << /Resources << /XObject << /G 5 0 R /Inner 6 0 R >> >> >> endobj\n"
                     "5 0 obj << /Subtype /Form /BBox [0 0 18 12] /Group 10 0 R /Length %zu >> "
                     "stream\n%s\nendstream endobj\n"
                     "6 0 obj << /Subtype /Form /BBox [0 0 18 12] /Group 10 0 R /Length %zu >> "
                     "stream\n%s\nendstream endobj\n"
                     "10 0 obj << /S /Transparency /I 11 0 R /K %s /CS /DeviceRGB >> endobj\n"
                     "11 0 obj %s endobj\n",
                     strlen(body), body, strlen(inner), inner, variant >= 6 ? "true" : "false",
                     variant % 6 >= 3 ? "false" : "true");
        if (length < 0 || (size_t)length >= sizeof(group_document))
            return 1;
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            uint8_t pixels[24 * 16 * 4];
            memset(pixels, 255, sizeof(pixels));
            cupidimage_image image = {0};
            image.width = 24;
            image.height = 16;
            image.rgba = pixels;
            pdf_page_geometry geometry = {0, 0, 18, 12, 0, 24, 16, 1};
            char error[128] = {0};
            allocation_count = 0;
            fail_at = failure;
            const char *content = "/G Do";
            int ok = cupidimage_pdf_render_content_stream(
                (const unsigned char *)content, strlen(content),
                (const unsigned char *)group_document, (size_t)length, 3, NULL, &image, &geometry,
                error, sizeof(error));
            if (!failure)
                total = allocation_count;
            checks++;
            if ((!failure && ok != (variant % 3 != 1)) || live_count)
                failures++;
            if (!ok)
                for (size_t i = 0; i < sizeof(pixels); i++)
                    if (pixels[i] != 255) {
                        failures++;
                        break;
                    }
            while (live_count)
                fault_free(live[0]);
        }
        fail_at = 0;
    }
    {
        const unsigned char data[] = "<< /Group << /S /Transparency /I true >> >>";
        pdf_object_view form = {data, sizeof(data) - 1, NULL, NULL};
        cupidimage_image image = {0};
        image.width = image.height = 5000;
        pdf_form_budget budget = {0};
        pdf_render_state parent = {0}, child = {0};
        parent.img = &image;
        parent.form_budget = &budget;
        parent.document = data;
        parent.document_size = sizeof(data) - 1;
        pdf_transparency_group group = {0};
        allocation_count = 0;
        fail_at = 0;
        checks++;
        if (cupidimage_pdf_group_begin(&child, &parent, &form, &group) || allocation_count ||
            budget.transparency_bytes_live)
            failures++;
        image.width = image.height = 1;
        budget.transparency_bytes_live = PDF_TRANSPARENCY_MEMORY_LIMIT;
        checks++;
        if (cupidimage_pdf_group_begin(&child, &parent, &form, &group) || allocation_count ||
            budget.transparency_bytes_live != PDF_TRANSPARENCY_MEMORY_LIMIT)
            failures++;
        cupidimage_pdf_group_free(&parent, &group);
    }
    /* Soft-mask state owns its sampled values independently of temporary group surfaces. */
    for (unsigned variant = 0; variant < 8; variant++) {
        const char *body = variant == 6   ? "/Nested gs 1 g 0 0 18 12 re f"
                           : variant == 7 ? "/Nested gs 0 0 18 12 re f (unterminated"
                           : variant == 3 ? "/M gs 0 0 18 12 re f"
                           : variant == 4 ? "0 0 18 12 re f (unterminated"
                                          : "1 g 0 0 8.8 12 re f";
        const char *transfer = variant == 2 ? "/TR 9 0 R"
                               : variant == 1
                                   ? "/TR << /FunctionType 2 /Domain [0 1] /C0 [1] /C1 [0] /N 1 >>"
                                   : "";
        const char *calculator = "{ 1 exch sub }";
        const char *nested_body = "1 g 0 0 8.8 12 re f";
        const char *paint = variant == 5 ? "18 0 0 12 0 0 cm /Im Do" : "0 0 18 12 re f";
        char mask_document[3072];
        int length = snprintf(
            mask_document, sizeof(mask_document),
            "3 0 obj << /Resources << /ExtGState << /M << /SMask 6 0 R >> /Nested << /SMask 7 0 R "
            ">> /Clear << /SMask /None >> >> /XObject << /F 8 0 R >> >> >> endobj\n"
            "5 0 obj << /Subtype /Form /BBox [0 0 18 12] /Group << /S /Transparency /I true /CS "
            "/DeviceGray >> /Length %zu >> stream\n%s\nendstream endobj\n"
            "6 0 obj << /S /%s /G 5 0 R %s >> endobj\n"
            "7 0 obj << /S /Alpha /G 12 0 R >> endobj\n"
            "8 0 obj << /Subtype /Form /BBox [0 0 18 12] /Resources << /XObject << /Im 10 0 R >> "
            ">> /Length %zu >> stream\n%s\nendstream endobj\n"
            "9 0 obj << /FunctionType 4 /Domain [0 1] /Range [0 1] /Length %zu >> "
            "stream\n%s\nendstream endobj\n"
            "10 0 obj << /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceRGB "
            "/BitsPerComponent 8 /SMask 11 0 R /Filter /ASCIIHexDecode /Length 7 >> "
            "stream\nff0000>\nendstream endobj\n"
            "11 0 obj << /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceGray "
            "/BitsPerComponent 8 /Filter /ASCIIHexDecode /Length 3 >> stream\n80>\nendstream "
            "endobj\n"
            "12 0 obj << /Subtype /Form /BBox [0 0 18 12] /Group << /S /Transparency /I true /CS "
            "/DeviceGray >> /Length %zu >> stream\n%s\nendstream endobj\n",
            strlen(body), body, variant == 1 ? "Luminosity" : "Alpha", transfer, strlen(paint),
            paint, strlen(calculator), calculator, strlen(nested_body), nested_body);
        if (length < 0 || (size_t)length >= sizeof(mask_document))
            return 1;
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            unsigned char pixels[24 * 16 * 4];
            memset(pixels, 255, sizeof(pixels));
            cupidimage_image image = {0};
            image.width = 24;
            image.height = 16;
            image.rgba = pixels;
            pdf_page_geometry geometry = {0, 0, 18, 12, 0, 24, 16, 1};
            char error[128] = {0};
            allocation_count = 0;
            fail_at = failure;
            const char *content = "/M gs q /Clear gs Q /F Do q /Clear gs q Q Q";
            int ok = cupidimage_pdf_render_content_stream(
                (const unsigned char *)content, strlen(content),
                (const unsigned char *)mask_document, (size_t)length, 3, NULL, &image, &geometry,
                error, sizeof(error));
            if (!failure)
                total = allocation_count;
            checks++;
            if ((!failure && ok != (variant < 3 || variant == 5 || variant == 6)) || live_count)
                failures++;
            while (live_count)
                fault_free(live[0]);
        }
        fail_at = 0;
    }
    /* Every fax allocation, including output growth, must unwind cleanly. */
    for (int malformed = 0; malformed < 2; malformed++) {
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            const unsigned char encoded[] = {0x9c, 0xe7, 0x38}; /* Four white 8-pixel rows. */
            cupidimage_ccitt_options options = {0, 8, 4, 0, 0, 0, 0, 0};
            unsigned char *decoded = NULL;
            size_t size = 0;
            allocation_count = 0;
            fail_at = failure;
            int ok = cupidimage_ccitt_decode(encoded, malformed ? 1 : sizeof(encoded), &options,
                                             &decoded, &size);
            if (!failure)
                total = allocation_count;
            checks++;
            if ((!failure && !malformed && (!ok || size != 4)) || (failure && ok) ||
                (!ok && (decoded || size)))
                failures++;
            fault_free(decoded);
            if (live_count) {
                failures++;
                while (live_count)
                    fault_free(live[0]);
            }
        }
    }
    {
        const char *glyph = "100 0 0 0 100 100 d1 100 0 0 100 0 0 cm /Im Do";
        char type3_document[2048];
        int length =
            snprintf(type3_document, sizeof(type3_document),
                     "3 0 obj << /Resources << /Font << /F 5 0 R >> >> >> endobj\n"
                     "5 0 obj << /Type /Font /Subtype /Type3 /FontBBox [0 0 100 100] /FontMatrix "
                     "[.01 0 0 .01 0 0] "
                     "/CharProcs << /A 6 0 R >> /Encoding << /Differences [65 /A] >> /FirstChar 65 "
                     "/LastChar 65 "
                     "/Widths [100] /Resources << /XObject << /Im 8 0 R >> >> >> endobj\n"
                     "6 0 obj << /Length %zu >> stream\n%s\nendstream endobj\n"
                     "8 0 obj << /Subtype /Image /Width 1 /Height 1 /ImageMask true /Filter "
                     "/ASCIIHexDecode /Length 3 >> stream\n00>\nendstream endobj\n",
                     strlen(glyph), glyph);
        if (length < 0 || (size_t)length >= sizeof(type3_document))
            return 1;
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            unsigned char pixels[24 * 16 * 4];
            cupidimage_image image = {0};
            image.width = 24;
            image.height = 16;
            image.rgba = pixels;
            pdf_page_geometry geometry = {0, 0, 18, 12, 0, 24, 16, 1};
            char error[128] = {0};
            pdf_font_table fonts;
            cupidimage_pdf_font_table_init(&fonts);
            allocation_count = 0;
            fail_at = failure;
            const char *content = "BT /F 10 Tf (AA) Tj ET";
            int ok =
                cupidimage_pdf_collect_page_fonts((unsigned char *)type3_document, (size_t)length,
                                                  3, &fonts, error, sizeof(error)) &&
                cupidimage_pdf_render_content_stream(
                    (unsigned char *)content, strlen(content), (unsigned char *)type3_document,
                    (size_t)length, 3, &fonts, &image, &geometry, error, sizeof(error));
            cupidimage_pdf_font_table_free(&fonts);
            if (!failure)
                total = allocation_count;
            checks++;
            if (live_count || (!failure && !ok))
                failures++;
            while (live_count)
                fault_free(live[0]);
        }
    }
    for (int variant = 0; variant < 4; variant++) {
        int uncolored = variant & 1, deferred = variant >= 2;
        const char *program = deferred
                                  ? (uncolored ? "/Blend gs 1.5 0 0 1.5 0 0 cm /Mask Do"
                                               : "/Blend gs 1 0 0 rg 1.5 0 0 1.5 0 0 cm /Mask Do")
                                  : (uncolored ? "1.5 0 0 1.5 0 0 cm /Mask Do"
                                               : "1 0 0 rg 1.5 0 0 1.5 0 0 cm /Mask Do");
        char pattern_document[2048];
        int length =
            snprintf(pattern_document, sizeof(pattern_document),
                     "3 0 obj << /Resources << /Pattern << /P 5 0 R >> /ColorSpace << /PC "
                     "[/Pattern /DeviceRGB] >> >> >> endobj\n"
                     "5 0 obj << /Type /Pattern /PatternType 1 /PaintType %d /TilingType 1 /BBox "
                     "[0 0 1.5 1.5] /XStep 3 /YStep 3 "
                     "/Resources << /ExtGState << /Blend << /BM /Multiply >> >> "
                     "/XObject << /Mask 6 0 R >> >> /Length %zu >> "
                     "stream\n%s\nendstream endobj\n"
                     "6 0 obj << /Subtype /Image /Width 1 /Height 1 /ImageMask true /Filter "
                     "/ASCIIHexDecode /Length 3 >> stream\n00>\nendstream endobj\n",
                     uncolored ? 2 : 1, strlen(program), program);
        if (length < 0 || (size_t)length >= sizeof(pattern_document))
            return 1;
        const char *content =
            uncolored ? "/PC cs 1 0 0 /P scn 0 0 18 12 re f 0 1 0 /P scn 0 0 9 6 re f"
                      : "/Pattern cs /P scn q 1 g 0 0 18 12 re f Q /P scn 0 0 18 12 re f";
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            unsigned char pixels[24 * 16 * 4];
            memset(pixels, 255, sizeof(pixels));
            cupidimage_image image = {0};
            image.width = 24;
            image.height = 16;
            image.rgba = pixels;
            pdf_page_geometry geometry = {0, 0, 18, 12, 0, 24, 16, 1};
            char error[128] = {0};
            allocation_count = 0;
            fail_at = failure;
            int ok = cupidimage_pdf_render_content_stream(
                (unsigned char *)content, strlen(content), (unsigned char *)pattern_document,
                (size_t)length, 3, NULL, &image, &geometry, error, sizeof(error));
            if (!failure)
                total = allocation_count;
            checks++;
            if (live_count || (!failure && !ok))
                failures++;
            while (live_count)
                fault_free(live[0]);
        }
    }
    for (int owns_view = 0; owns_view < 2; owns_view++)
        for (size_t failure = 0; failure <= 2; failure++) {
            unsigned char *owned = malloc(16);
            if (!owned)
                return 1;
            memcpy(owned, "xx<< /N 1 >>", 12);
            pdf_object_view view = {owned + 2, 10, owns_view ? owned : NULL, NULL};
            pdf_pattern pattern = {0};
            pdf_pattern_store store = {0};
            pdf_render_state state = {0};
            state.patterns = &store;
            state.document = (const unsigned char *)"document";
            state.document_size = 8;
            allocation_count = 0;
            fail_at = failure;
            int ok = pattern_retain(&state, &pattern, &view, NULL, 0);
            checks++;
            if (ok != (failure == 0) || (!ok && store.bytes_used))
                failures++;
            memset(owned, 0, 16);
            if (ok && (pattern.definition.data == view.data ||
                       memcmp(pattern.definition.data, "<< /N 1 >>", 10)))
                failures++;
            free(owned);
            fault_free(pattern.initial);
            fault_free(pattern.definition.owned);
            if (live_count) {
                failures++;
                while (live_count)
                    fault_free(live[0]);
            }
        }
    /* Cached colors must be rebuilt when an inherited pattern enters another blending space. */
    for (int shading = 0; shading < 2; shading++) {
        const char *program = ".8 0 0 .6 k 0 0 3 3 re f";
        const char *form = "0 0 18 12 re f";
        char pattern[512], document[2048];
        if (shading)
            snprintf(pattern, sizeof(pattern),
                     "5 0 obj << /PatternType 2 /Shading << /ShadingType 2 /ColorSpace /DeviceCMYK "
                     "/Coords [0 0 18 0] /Function << /FunctionType 2 /Domain [0 1] "
                     "/C0 [.8 0 0 .6] /C1 [.8 0 0 .6] /N 1 >> >> >> endobj\n");
        else
            snprintf(
                pattern, sizeof(pattern),
                "5 0 obj << /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 3 3] "
                "/XStep 3 /YStep 3 /Resources << >> /Length %zu >> stream\n%s\nendstream endobj\n",
                strlen(program), program);
        int length = snprintf(document, sizeof(document),
                              "3 0 obj << /Resources << /Pattern << /P 5 0 R >> "
                              "/XObject << /F 6 0 R >> >> >> endobj\n%s"
                              "6 0 obj << /Type /XObject /Subtype /Form /BBox [0 0 18 12] "
                              "/Group << /S /Transparency /I true /CS /DeviceGray >> "
                              "/Length %zu >> stream\n%s\nendstream endobj\n",
                              pattern, strlen(form), form);
        if (length < 0 || (size_t)length >= sizeof(document))
            return 1;
        const char *content = "/Pattern cs /P scn 0 0 18 12 re f /F Do 0 0 18 12 re f";
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            unsigned char pixels[24 * 16 * 4];
            memset(pixels, 255, sizeof(pixels));
            cupidimage_image image = {0};
            image.width = 24;
            image.height = 16;
            image.rgba = pixels;
            pdf_page_geometry geometry = {0, 0, 18, 12, 0, 24, 16, 1};
            char error[128] = {0};
            allocation_count = 0;
            fail_at = failure;
            int ok = cupidimage_pdf_render_content_stream(
                (const unsigned char *)content, strlen(content), (const unsigned char *)document,
                (size_t)length, 3, NULL, &image, &geometry, error, sizeof(error));
            if (!failure)
                total = allocation_count;
            checks++;
            if (live_count || (!failure && !ok))
                failures++;
            if (!ok)
                for (size_t i = 0; i < sizeof(pixels); i++)
                    if (pixels[i] != 255) {
                        failures++;
                        break;
                    }
            while (live_count)
                fault_free(live[0]);
        }
    }
    {
        pdf_render_state state = {0};
        pdf_pattern_store full = {0};
        full.count = 65536;
        state.patterns = &full;
        checks++;
        if (cupidimage_pdf_select_pattern(&state, "P", 0))
            failures++;
        full.count = 0;
        state.pattern_depth = 8;
        checks++;
        if (cupidimage_pdf_select_pattern(&state, "P", 0))
            failures++;
    }
    for (int variant = 0; variant < 4; variant++) {
        const char *states[] = {"", "/ca .5", "/ca .5 /BM /Multiply",
                                "/ca .5 /BM /Multiply /SMask << /S /Luminosity /G 8 0 R >>"};
        const char *mask = ".5 g 0 0 18 12 re f";
        char pattern_document[1024];
        int length = snprintf(
            pattern_document, sizeof(pattern_document),
            "3 0 obj << /Resources << /Pattern << /P 5 0 R >> >> >> endobj\n"
            "5 0 obj << /PatternType 2 /ExtGState << %s >> /Shading 6 0 R >> endobj\n"
            "6 0 obj << /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 18 0] /Function 7 0 R "
            ">> endobj\n"
            "7 0 obj << /FunctionType 4 /Domain [0 1] /Range [0 1] /Length 2 >> "
            "stream\n{}\nendstream endobj\n"
            "8 0 obj << /Type /XObject /Subtype /Form /BBox [0 0 18 12] "
            "/Group << /S /Transparency /I true /CS /DeviceGray >> /Length %zu >> "
            "stream\n%s\nendstream endobj\n",
            states[variant], strlen(mask), mask);
        if (length < 0 || (size_t)length >= sizeof(pattern_document))
            return 1;
        const char *content = "/Pattern cs /P scn q /P scn Q 0 0 18 12 re f";
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            unsigned char pixels[24 * 16 * 4];
            memset(pixels, 255, sizeof(pixels));
            cupidimage_image image = {0};
            image.width = 24;
            image.height = 16;
            image.rgba = pixels;
            pdf_page_geometry geometry = {0, 0, 18, 12, 0, 24, 16, 1};
            char error[128] = {0};
            allocation_count = 0;
            fail_at = failure;
            int ok = cupidimage_pdf_render_content_stream(
                (unsigned char *)content, strlen(content), (unsigned char *)pattern_document,
                strlen(pattern_document), 3, NULL, &image, &geometry, error, sizeof(error));
            if (!failure)
                total = allocation_count;
            checks++;
            if (live_count || (!failure && !ok))
                failures++;
            while (live_count)
                fault_free(live[0]);
        }
        pdf_render_state state = {0};
        pdf_pattern_store full = {0};
        cupidimage_image image = {0};
        image.width = image.height = 1;
        state.img = &image;
        state.patterns = &full;
        full.bytes_used = PATTERN_BYTES - 1;
        pdf_object_view view = {(const unsigned char *)"<< >>", 5, NULL, NULL};
        checks++;
        if (pattern_select_shading(&state, &view, 0, 0))
            failures++;
    }
    {
        const char *entries[] = {"/W [1 [500 600]] /W2 [1 2 -800 200 700]",
                                 "/DW 600 /DW2 [900 -1100]",
                                 "/W [65535 [500]]",
                                 "/W [65536 [1]]",
                                 "/W [3 2 500]",
                                 "/W2 [1 [-800 200]]",
                                 "/DW2 [900]",
                                 "/W [65535 [500 600]]",
                                 "/W 8 0 R"};
        for (unsigned test = 0; test < sizeof(entries) / sizeof(entries[0]); test++) {
            char document[2048];
            int length =
                snprintf(document, sizeof(document),
                         "6 0 obj << /Subtype /CIDFontType2 %s >> endobj\n8 0 obj 8 0 R endobj",
                         entries[test]);
            const char *source =
                "<< /Subtype /Type0 /Encoding /Identity-V /DescendantFonts [6 0 R] >>";
            pdf_object_view view = {(const unsigned char *)source, strlen(source), NULL, NULL};
            size_t total = 0;
            for (size_t failure = 0; failure <= total; failure++) {
                pdf_font_map font = {0};
                size_t budget = 0;
                allocation_count = 0;
                fail_at = failure;
                int ok = cupidimage_pdf_parse_cid_font((unsigned char *)document, (size_t)length,
                                                       &view, &font, &budget);
                if (!failure)
                    total = allocation_count;
                checks++;
                if (!failure && ok != (test < 3)) {
                    fprintf(stderr, "FAIL CID metadata case %u\n", test);
                    failures++;
                }
                cupidimage_pdf_cid_font_free(font.cid);
                if (live_count) {
                    failures++;
                    while (live_count)
                        fault_free(live[0]);
                }
            }
        }
        fail_at = 0;
    }
    for (unsigned kind = 0; kind < 7; kind++) {
        const unsigned char *font_data = kind == 2 ? test_type1_font
                                         : kind    ? test_cff_font
                                                   : test_truetype_font;
        size_t font_size = kind == 2 ? sizeof(test_type1_font)
                           : kind    ? sizeof(test_cff_font)
                                     : sizeof(test_truetype_font);
        char attributes[128] = "";
        if (kind == 2)
            snprintf(attributes, sizeof(attributes), "/Length1 %u /Length2 %u /Length3 0",
                     test_type1_font_clear, test_type1_font_encrypted);
        else if (kind)
            strcpy(attributes, "/Subtype /Type1C");
        const char *resources = kind == 5   ? "/ExtGState << /Use << /Font [5 0 R 18] >> >>"
                                : kind == 6 ? "/ExtGState << /Use << /Font 8 0 R >> >>"
                                            : "/Font << /F 5 0 R >>";
        char document[16384];
        int length = snprintf(document, sizeof(document),
                              "3 0 obj << /Resources << %s >> >> endobj\n5 0 obj "
                              "<< /Subtype /%s /Encoding /WinAnsiEncoding /FontDescriptor 6 "
                              "0 R >> endobj\n6 0 obj << /Flags 32 /%s 7 0 R >> endobj\n7 0 "
                              "obj << %s /Filter /ASCIIHexDecode /Length %zu >> stream\n",
                              resources, kind ? "Type1" : "TrueType",
                              kind == 2 ? "FontFile"
                              : kind    ? "FontFile3"
                                        : "FontFile2",
                              attributes, font_size * 2 + 1);
        for (size_t i = 0; i < font_size; i++)
            length += snprintf(document + length, sizeof(document) - (size_t)length, "%02x",
                               font_data[i]);
        length +=
            snprintf(document + length, sizeof(document) - (size_t)length, ">\nendstream endobj\n");
        if (kind == 6)
            length += snprintf(document + length, sizeof(document) - (size_t)length,
                               "8 0 obj [5 0 R 9 0 R] endobj\n9 0 obj 18 endobj\n");
        if (kind == 3 || kind == 4)
            length = snprintf(document, sizeof(document),
                              "3 0 obj << /Resources << /Font << /F 5 0 R >> >> >> endobj\n"
                              "5 0 obj << /Type /Font /Subtype /Type1 /BaseFont /%s >> endobj\n",
                              kind == 3 ? "Helvetica" : "Symbol");
        for (int mode = 0; mode < 8; mode++) {
            size_t total = 0;
            for (size_t failure = 0; failure <= total; failure++) {
                pdf_font_table fonts;
                cupidimage_pdf_font_table_init(&fonts);
                allocation_count = 0;
                fail_at = failure;
                int ok = cupidimage_pdf_collect_page_fonts((unsigned char *)document,
                                                           (size_t)length, 3, &fonts, NULL, 0);
                uint8_t pixels[48 * 40 * 4];
                cupidimage_image image = {0};
                image.width = 48;
                image.height = 40;
                image.rgba = pixels;
                pdf_page_geometry geometry = {0, 0, 36, 30, 0, 48, 40, 1};
                char content[128], error[128] = {0};
                int content_length =
                    snprintf(content, sizeof(content), "BT %s %d Tr 2 4 Td (ABC) Tj ET",
                             kind >= 5 ? "/Use gs" : "/F 18 Tf", mode);
                if (ok)
                    ok = cupidimage_pdf_render_content_stream(
                        (unsigned char *)content, (size_t)content_length, (unsigned char *)document,
                        (size_t)length, 3, &fonts, &image, &geometry, error, sizeof(error));
                if (!failure)
                    total = allocation_count;
                checks++;
                if (!failure && !ok) {
                    fprintf(stderr, "FAIL embedded font mode %d: %s\n", mode, error);
                    failures++;
                }
                cupidimage_pdf_font_table_free(&fonts);
                if (live_count) {
                    failures++;
                    fprintf(stderr, "FAIL embedded font allocation %zu: %zu live\n", failure,
                            live_count);
                    while (live_count)
                        fault_free(live[0]);
                }
            }
        }
        fail_at = 0;
    }
    {
        const char *programs[] = {
            "1 begincodespacerange <00> <FF> endcodespacerange 2 begincidchar <41> 1 <42> 2 "
            "endcidchar",
            "/Base usecmap 1 begincidchar <41> 2 endcidchar",
            "1 begincodespacerange <00> <FF> endcodespacerange 1 begincidrange <40> <43> 1 "
            "endcidrange 1 begincidchar <41> 5 endcidchar",
            "1 begincodespacerange <00> <FF> endcodespacerange 1 begincidchar <41> 65536 "
            "endcidchar"};
        for (unsigned sample = 0; sample < 4; sample++) {
            const char *base = "1 begincodespacerange <00> <FF> endcodespacerange 1 begincidchar "
                               "<41> 1 endcidchar";
            char document[4096];
            int size = snprintf(
                document, sizeof(document),
                "10 0 obj << /CMapName /Child %s /Length %zu >> stream\n%s\nendstream endobj\n11 0 "
                "obj << /CMapName /Base /Length %zu >> stream\n%s\nendstream endobj\n",
                sample == 1 ? "/UseCMap 11 0 R" : "", strlen(programs[sample]), programs[sample],
                strlen(base), base);
            size_t total = 0;
            for (size_t failure = 0; failure <= total; failure++) {
                pdf_object_view view = {0};
                pdf_cmap *map = NULL;
                allocation_count = 0;
                fail_at = failure;
                int ok =
                    cupidimage_pdf_get_object_content((unsigned char *)document, (size_t)size, 10,
                                                      &view) &&
                    cupidimage_pdf_cmap_load((unsigned char *)document, (size_t)size, &view, &map);
                if (!failure)
                    total = allocation_count;
                checks++;
                if (!failure && ok != (sample < 3)) {
                    fprintf(stderr, "FAIL CMap sample %u\n", sample);
                    failures++;
                }
                cupidimage_pdf_cmap_free(map);
                cupidimage_pdf_object_view_release(&view);
                if (live_count) {
                    failures++;
                    while (live_count)
                        fault_free(live[0]);
                }
            }
        }
        fail_at = 0;
    }
    {
        const char *names[] = {"/90ms-RKSJ-H", "/UniJIS-UTF16-V", "/GBK2K-V", "/Missing"};
        for (unsigned sample = 0; sample < 4; sample++) {
            size_t total = 0;
            for (size_t failure = 0; failure <= total; failure++) {
                pdf_object_view view = {0};
                view.data = (const unsigned char *)names[sample];
                view.len = strlen(names[sample]);
                pdf_cmap *map = NULL;
                allocation_count = 0;
                fail_at = failure;
                int ok = cupidimage_pdf_cmap_load(NULL, 0, &view, &map);
                if (!failure)
                    total = allocation_count;
                checks++;
                if (!failure && ok != (sample < 3))
                    failures++;
                cupidimage_pdf_cmap_free(map);
                if (live_count) {
                    failures++;
                    while (live_count)
                        fault_free(live[0]);
                }
            }
        }
        fail_at = 0;
    }
    {
        const unsigned char *programs[] = {test_cff_cid_font, test_cff_composite_font};
        const size_t lengths[] = {sizeof(test_cff_cid_font), sizeof(test_cff_composite_font)};
        for (unsigned sample = 0; sample < 2; sample++) {
            size_t total = 0;
            for (size_t failure = 0; failure <= total; failure++) {
                allocation_count = 0;
                fail_at = failure;
                pdf_cff_font font;
                int ok = cupidimage_pdf_cff_init(&font, programs[sample], lengths[sample]);
                if (ok) {
                    for (unsigned glyph = 0; glyph < font.chars.count; glyph++) {
                        pdf_cff_outline outline;
                        if (!cupidimage_pdf_cff_glyph_outline(&font, glyph, &outline)) {
                            ok = 0;
                            break;
                        }
                        cupidimage_pdf_type2_free(&outline);
                    }
                }
                cupidimage_pdf_cff_free(&font);
                if (!failure)
                    total = allocation_count;
                checks++;
                if (!failure && !ok)
                    failures++;
                if (live_count) {
                    failures++;
                    while (live_count)
                        fault_free(live[0]);
                }
            }
        }
        fail_at = 0;
    }
    {
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            allocation_count = 0;
            fail_at = failure;
            pdf_type1_font font;
            int ok = cupidimage_pdf_type1_init(
                &font, test_type1_composite, sizeof(test_type1_composite),
                test_type1_composite_clear, test_type1_composite_encrypted);
            if (ok) {
                for (unsigned i = 0; i < font.glyph_count; i++) {
                    pdf_cff_outline outline;
                    if (!cupidimage_pdf_type1_outline(&font, i, &outline)) {
                        ok = 0;
                        break;
                    }
                    cupidimage_pdf_type2_free(&outline);
                }
            }
            cupidimage_pdf_type1_free(&font);
            if (!failure)
                total = allocation_count;
            checks++;
            if (!failure && !ok)
                failures++;
            if (live_count) {
                failures++;
                while (live_count)
                    fault_free(live[0]);
            }
        }
        fail_at = 0;
    }
    {
        unsigned char pixels[16 * 16 * 4];
        memset(pixels, 255, sizeof(pixels));
        cupidimage_image image = {.width = 16, .height = 16, .rgba = pixels};
        pdf_form_budget form = {0};
        pdf_clip_budget clip = {0, 10000, 100};
        pdf_render_state state = {0};
        state.img = &image;
        state.form_budget = &form;
        state.clip_budget = &clip;
        state.gs.fill_alpha = 1;
        const double red[3] = {1, 0, 0};
        for (int sample = 0; sample < 4; sample++) {
            pdf_transparency_group group = {0};
            group.lazy_backdrop = sample != 3;
            int ok = cupidimage_pdf_group_surface(&state, &group, 0, sample == 3 ? red : NULL);
            if (ok && sample == 1)
                cupidimage_pdf_group_paint(&group, 5 * 16 + 7, red, 0, 1, 0);
            if (ok && sample == 2) {
                cupidimage_pdf_group_paint(&group, 5 * 16 + 7, red, .5, 1, 0);
                cupidimage_pdf_group_paint(&group, 8 * 16 + 9, red, .5, 1, 0);
            }
            size_t before = clip.tests_left;
            if (ok)
                ok = cupidimage_pdf_group_finish(&state, &group);
            const size_t expected[] = {0, 1, 12, 256};
            checks++;
            if (!ok || before - clip.tests_left != expected[sample])
                failures++;
            cupidimage_pdf_group_free(&state, &group);
            if (form.transparency_bytes_live || live_count)
                failures++;
        }
    }
    printf("PDF clip allocation failures: %zu checks, %zu failures\n", checks, failures);
    return failures != 0;
}
