#include "fault_alloc.h"

#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/compression/ccitt.c"
#include "../src/formats/pdf/cupidimage_pdf.c"
#include "../src/formats/pdf/pdf_calculator.c"
#include "../src/formats/pdf/pdf_document.c"
#include "../src/formats/pdf/pdf_filters.c"
#include "../src/formats/pdf/pdf_font_type3.c"
#include "../src/formats/pdf/pdf_fonts.c"
#include "../src/formats/pdf/pdf_functions.c"
#include "../src/formats/pdf/pdf_image_colors.c"
#include "../src/formats/pdf/pdf_images.c"
#include "../src/formats/pdf/pdf_inline.c"
#include "../src/formats/pdf/pdf_patterns.c"
#include "../src/formats/pdf/pdf_render.c"
#include "../src/formats/pdf/pdf_resources.c"
#include "../src/formats/pdf/pdf_shading.c"
#include "../src/formats/pdf/pdf_stroke.c"
#include "../src/formats/pdf/pdf_text.c"
#include "../src/formats/pdf/pdf_transparency.c"
#undef malloc
#undef calloc
#undef realloc
#undef free

int main(void) {
    static const struct { const char *content; int valid;
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
                    {"/BadCalculator sh", 0}};
    const char *form_content = "q 0 0 18 6 re W n 1 0 0 rg 0 0 18 12 re f q";
    const char *font_content = "BT /F1 6 Tf 0 6 Td (Z) Tj ET";
    const char *cmap = "1 beginbfchar <5A> <0041> endbfchar";
    char document[3072];
    int document_size = snprintf(document, sizeof(document),
        "3 0 obj << /Resources << /XObject << /F0 5 0 R /Im0 6 0 R /Im1 8 0 R /FontForm 11 0 R >> /ExtGState << /G << /ca .5 /CA .5 /BM /Multiply >> >> /Shading << /Sampled 14 0 R /Broken 17 0 R /Calculator 18 0 R /BadCalculator 20 0 R >> >> >> endobj\n"
        "5 0 obj << /Type /XObject /Subtype /Form /BBox [0 0 18 12] /Length %zu >>\n"
        "stream\n%s\nendstream\nendobj\n"
        "6 0 obj << /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceRGB "
        "/BitsPerComponent 8 /SMask 7 0 R /Filter /ASCIIHexDecode /Length 7 >> stream\n524742>\nendstream endobj\n"
        "7 0 obj << /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceGray "
        "/BitsPerComponent 8 /Matte [1 1 1] /Filter /ASCIIHexDecode /Length 3 >> stream\n4D>\nendstream endobj\n"
        "8 0 obj << /Subtype /Image /Width 1 /Height 1 /ColorSpace [/Indexed /DeviceRGB 0 9 0 R] "
        "/BitsPerComponent 8 /SMask 10 0 R /Filter /ASCIIHexDecode /Length 3 >> stream\n00>\nendstream endobj\n"
        "9 0 obj << /Filter /ASCIIHexDecode /Length 7 >> stream\n524742>\nendstream endobj\n"
        "10 0 obj << /Subtype /Image /Width 1 /Height 1 /ColorSpace /DeviceGray "
        "/BitsPerComponent 8 /Length 1 >> stream\nM\nendstream endobj\n"
        "11 0 obj << /Subtype /Form /BBox [0 0 18 12] /Resources << /Font << /F1 12 0 R >> >> /Length %zu >> stream\n%s\nendstream endobj\n"
        "12 0 obj << /Type /Font /ToUnicode 13 0 R >> endobj\n"
        "13 0 obj << /Length %zu >> stream\n%s\nendstream endobj\n"
        "14 0 obj << /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 18 0] /Function 15 0 R >> endobj\n"
        "15 0 obj << /FunctionType 0 /Domain [0 1] /Range [0 1] /Size [2] /BitsPerSample 8 /Filter /ASCIIHexDecode /Length 5 >> stream\n00ff>\nendstream endobj\n"
        "16 0 obj << /FunctionType 2 /Domain [0 1] /C0 [0 1] /C1 [1] /N 1 >> endobj\n"
        "17 0 obj << /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 18 0] /Function [15 0 R 16 0 R 15 0 R] >> endobj\n"
        "18 0 obj << /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 18 0] /Function 19 0 R >> endobj\n"
        "19 0 obj << /FunctionType 4 /Domain [0 1] /Range [0 1] /Length 2 >> stream\n{}\nendstream endobj\n"
        "20 0 obj << /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 0 18 0] /Function [19 0 R 16 0 R 19 0 R] >> endobj\n",
        strlen(form_content), form_content, strlen(font_content), font_content, strlen(cmap), cmap);
    if (document_size < 0 || (size_t)document_size >= sizeof(document)) return 1;
    size_t checks = 0, failures = 0;
    for (size_t fixture = 0; fixture < sizeof(fixtures) / sizeof(fixtures[0]); fixture++) {
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            uint8_t pixels[24 * 16 * 4];
            cupidimage_image image = {0};
            image.width = 24; image.height = 16; image.rgba = pixels;
            pdf_page_geometry geometry = {0, 0, 18, 12, 0, 24, 16};
            char error[128] = {0};
            allocation_count = 0;
            fail_at = failure;
            int ok = cupidimage_pdf_render_content_stream((const unsigned char *)fixtures[fixture].content,
                strlen(fixtures[fixture].content), (const unsigned char *)document, (size_t)document_size,
                3, NULL, &image, &geometry, error, sizeof(error));
            if (failure == 0) total = allocation_count;
            checks++;
            if (live_count || (failure == 0 && ok != fixtures[fixture].valid)) {
                fprintf(stderr, "FAIL fixture %zu allocation %zu: %zu live allocations, result %d\n",
                        fixture, failure, live_count, ok);
                failures++;
            }
            while (live_count) fault_free(live[0]);
        }
    }
    {
        const char *shading_doc =
            "3 0 obj << /Resources << /Shading << /Sh 4 0 R >> >> >> endobj\n"
            "4 0 obj << /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 1.5 0] "
            "/Function << /FunctionType 2 /Domain [0 1] /N 1 >> >> endobj\n";
        uint8_t pixels[16]; memset(pixels, 255, sizeof(pixels));
        cupidimage_image image = {0}; image.width=2; image.height=2; image.rgba=pixels;
        pdf_form_budget budget = {0,3,3,0};
        pdf_render_state state = {0}; state.img=&image; state.document=(const unsigned char *)shading_doc;
        state.document_size=strlen(shading_doc); state.page_object=3; state.page_top_pts=1.5f;
        state.gs.ctm[0]=state.gs.ctm[3]=1; state.gs.fill_alpha=1; state.form_budget=&budget;
        fail_at=0; allocation_count=0;
        checks++;
        if (cupidimage_pdf_draw_shading(&state,"Sh") || budget.shade_evaluations_left != 3) failures++;
        budget.shade_evaluations_left=4; checks++;
        if (!cupidimage_pdf_draw_shading(&state,"Sh") || budget.shade_evaluations_left || pixels[0]!=64 || pixels[4]!=191) failures++;
        checks++;
        if (cupidimage_pdf_draw_shading(&state,"Sh")) failures++;
        if (live_count) { failures++; while(live_count) fault_free(live[0]); }
    }
    /* Every fax allocation, including output growth, must unwind cleanly. */
    for (int malformed=0; malformed<2; malformed++) {
        size_t total=0;
        for (size_t failure=0; failure<=total; failure++) {
            const unsigned char encoded[]={0x9c,0xe7,0x38}; /* Four white 8-pixel rows. */
            cupidimage_ccitt_options options={0,8,4,0,0,0,0,0};
            unsigned char *decoded=NULL; size_t size=0;
            allocation_count=0; fail_at=failure;
            int ok=cupidimage_ccitt_decode(encoded,malformed?1:sizeof(encoded),&options,&decoded,&size);
            if (!failure) total=allocation_count;
            checks++;
            if ((!failure && !malformed && (!ok || size!=4)) ||
                (failure && ok) || (!ok && (decoded || size))) failures++;
            fault_free(decoded);
            if (live_count) { failures++; while(live_count) fault_free(live[0]); }
        }
    }
    {
        const char *glyph="100 0 0 0 100 100 d1 100 0 0 100 0 0 cm /Im Do";
        char type3_document[2048];
        int length=snprintf(type3_document,sizeof(type3_document),
            "3 0 obj << /Resources << /Font << /F 5 0 R >> >> >> endobj\n"
            "5 0 obj << /Type /Font /Subtype /Type3 /FontBBox [0 0 100 100] /FontMatrix [.01 0 0 .01 0 0] "
            "/CharProcs << /A 6 0 R >> /Encoding << /Differences [65 /A] >> /FirstChar 65 /LastChar 65 "
            "/Widths [100] /Resources << /XObject << /Im 8 0 R >> >> >> endobj\n"
            "6 0 obj << /Length %zu >> stream\n%s\nendstream endobj\n"
            "8 0 obj << /Subtype /Image /Width 1 /Height 1 /ImageMask true /Filter /ASCIIHexDecode /Length 3 >> stream\n00>\nendstream endobj\n",
            strlen(glyph),glyph);
        if(length<0 || (size_t)length>=sizeof(type3_document))return 1;
        size_t total=0;
        for(size_t failure=0;failure<=total;failure++) {
            unsigned char pixels[24*16*4];cupidimage_image image={0};
            image.width=24;image.height=16;image.rgba=pixels;
            pdf_page_geometry geometry={0,0,18,12,0,24,16};char error[128]={0};
            pdf_font_table fonts;cupidimage_pdf_font_table_init(&fonts);
            allocation_count=0;fail_at=failure;
            const char *content="BT /F 10 Tf (AA) Tj ET";
            int ok=cupidimage_pdf_collect_page_fonts((unsigned char *)type3_document,(size_t)length,3,&fonts,error,sizeof(error)) &&
                cupidimage_pdf_render_content_stream((unsigned char *)content,strlen(content),(unsigned char *)type3_document,(size_t)length,
                    3,&fonts,&image,&geometry,error,sizeof(error));
            cupidimage_pdf_font_table_free(&fonts);
            if(!failure)total=allocation_count;
            checks++;
            if(live_count || (!failure && !ok))failures++;
            while(live_count)fault_free(live[0]);
        }
    }
    for (int uncolored = 0; uncolored < 2; uncolored++) {
        const char *program =
            uncolored ? "1.5 0 0 1.5 0 0 cm /Mask Do" : "1 0 0 rg 1.5 0 0 1.5 0 0 cm /Mask Do";
        char pattern_document[2048];
        int length =
            snprintf(pattern_document, sizeof(pattern_document),
                     "3 0 obj << /Resources << /Pattern << /P 5 0 R >> /ColorSpace << /PC "
                     "[/Pattern /DeviceRGB] >> >> >> endobj\n"
                     "5 0 obj << /Type /Pattern /PatternType 1 /PaintType %d /TilingType 1 /BBox "
                     "[0 0 1.5 1.5] /XStep 3 /YStep 3 "
                     "/Resources << /XObject << /Mask 6 0 R >> >> /Length %zu >> "
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
            pdf_page_geometry geometry = {0, 0, 18, 12, 0, 24, 16};
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
    {
        const char *pattern_document =
            "3 0 obj << /Resources << /Pattern << /P 5 0 R >> >> >> endobj\n"
            "5 0 obj << /PatternType 2 /Shading 6 0 R >> endobj\n"
            "6 0 obj << /ShadingType 2 /ColorSpace /DeviceGray /Coords [0 0 18 0] /Function 7 0 R "
            ">> endobj\n"
            "7 0 obj << /FunctionType 4 /Domain [0 1] /Range [0 1] /Length 2 >> "
            "stream\n{}\nendstream endobj\n";
        const char *content = "/Pattern cs /P scn q /P scn Q 0 0 18 12 re f";
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            unsigned char pixels[24 * 16 * 4];
            memset(pixels, 255, sizeof(pixels));
            cupidimage_image image = {0};
            image.width = 24;
            image.height = 16;
            image.rgba = pixels;
            pdf_page_geometry geometry = {0, 0, 18, 12, 0, 24, 16};
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
        pdf_object_view view = {(const unsigned char *)"<< >>", 5, NULL};
        checks++;
        if (pattern_select_shading(&state, &view, 0, 0))
            failures++;
    }
    printf("PDF clip allocation failures: %zu checks, %zu failures\n", checks, failures);
    return failures != 0;
}
