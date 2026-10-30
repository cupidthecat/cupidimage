#include "formats/pdf/fonts/standard.h"
#include "formats/pdf/fonts/type2.h"
#include "formats/pdf/pdf_internal.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3)
        return 2;
    const pdf_standard_font *font = cupidimage_pdf_standard_font(argv[1]);
    if (!font)
        return 2;
    if (argc == 3 && !strcmp(argv[2], "--metadata")) {
        static const struct {
            const char *encoding;
            int valid;
        } cases[] = {{"/Encoding /WinAnsiEncoding", 1},
                     {"/Encoding null", 1},
                     {"/Encoding << /Differences [65 /A] >>", 1},
                     {"/Encoding 42", 0},
                     {"/Encoding []", 0},
                     {"/Encoding (x)", 0},
                     {"/Encoding /UnknownEncoding", 0},
                     {"/Encoding << /BaseEncoding 42 >>", 0},
                     {"/Encoding << /Differences [/A] >>", 0},
                     {"/Encoding << /Differences [256 /A] >>", 0},
                     {"/Encoding << /Differences [65.5 /A] >>", 0},
                     {"/Encoding << /Differences [65 /#00] >>", 0},
                     {"/Encoding << /Differences [255 /A /B] >>", 0}};

        for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
            char text[512];
            int length = snprintf(text, sizeof(text),
                                  "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica %s >>",
                                  cases[i].encoding);
            pdf_object_view view = {(const unsigned char *)text, (size_t)length, NULL, NULL};
            pdf_font_map parsed = {0};
            size_t budget = 0;
            int ok =
                cupidimage_pdf_parse_standard_font(view.data, view.len, &view, &parsed, &budget);
            int valid = ok == cases[i].valid && ((parsed.program != NULL) == cases[i].valid);
            cupidimage_pdf_font_program_free(parsed.program);
            if (!valid)
                return 4;
        }
        const unsigned char text[] = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>";
        pdf_object_view view = {text, sizeof(text) - 1, NULL, NULL};
        pdf_font_map parsed = {0};
        size_t budget = 64u * 1024u * 1024u;
        int ok =
            cupidimage_pdf_parse_standard_font(text, sizeof(text) - 1, &view, &parsed, &budget);
        if (ok || parsed.program || budget != 64u * 1024u * 1024u) {
            cupidimage_pdf_font_program_free(parsed.program);
            return 4;
        }
        puts("14 standard-font metadata and budget checks passed");
        return 0;
    }
    if (argc == 3 && (!strcmp(argv[2], "--outlines") || !strcmp(argv[2], "--dump"))) {
        const pdf_standard_outline *source = cupidimage_pdf_standard_outline(argv[1]);
        pdf_cff_font cff;
        if (!source || !cupidimage_pdf_cff_init(&cff, source->data, source->length))
            return 3;
        unsigned count = 0, missing = 0;
        for (unsigned i = 0; i < cff.chars.count; i++) {
            pdf_cff_outline outline;
            if (!cupidimage_pdf_type2_outline(&cff, i, &outline)) {
                cupidimage_pdf_cff_free(&cff);
                return 3;
            }
            if (!strcmp(argv[2], "--dump")) {
                printf("glyph %u %.17g %u %d %u %u %.17g %.17g\n", i, outline.width, outline.count,
                       outline.composite, outline.base, outline.accent, outline.accent_x,
                       outline.accent_y);
                for (unsigned j = 0; j < outline.count; j++) {
                    const pdf_cff_segment *segment = outline.segments + j;
                    printf("segment %u", segment->kind);
                    unsigned n = segment->kind == PDF_CFF_CURVE   ? 6
                                 : segment->kind == PDF_CFF_CLOSE ? 0
                                                                  : 2;
                    for (unsigned k = 0; k < n; k++)
                        printf(" %.17g", segment->points[k]);
                    putchar('\n');
                }
            }
            cupidimage_pdf_type2_free(&outline);
            count++;
        }
        for (size_t i = 1; i < font->count; i++)
            if (!cupidimage_pdf_cff_name(&cff, font->metrics[i].name))
                missing++;
        if (strcmp(argv[2], "--dump"))
            printf("%u %u\n", count, missing);
        cupidimage_pdf_cff_free(&cff);
        return 0;
    }
    if (cupidimage_pdf_standard_glyph(font, "not-a-glyph") != 0 ||
        cupidimage_pdf_standard_glyph(font, ".notdef") != 0)
        return 2;
    for (size_t i = 0; i < font->count; i++) {
        if (cupidimage_pdf_standard_glyph(font, font->metrics[i].name) != i)
            return 2;
        printf("glyph %s %d\n", font->metrics[i].name, font->metrics[i].width);
    }
    for (unsigned i = 0; i < 256; i++) {
        unsigned g = font->encoding[i];
        if (g >= font->count)
            return 2;
        printf("code %u %s\n", i, font->metrics[g].name);
    }
    return 0;
}
