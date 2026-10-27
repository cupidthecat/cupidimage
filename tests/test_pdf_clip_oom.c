#include "fault_alloc.h"

#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/formats/pdf/pdf_filters.c"
#include "../src/formats/pdf/pdf_document.c"
#include "../src/formats/pdf/pdf_fonts.c"
#include "../src/formats/pdf/pdf_image_colors.c"
#include "../src/formats/pdf/pdf_images.c"
#include "../src/formats/pdf/pdf_transparency.c"
#include "../src/formats/pdf/pdf_render.c"
#include "../src/formats/pdf/cupidimage_pdf.c"
#undef malloc
#undef calloc
#undef realloc
#undef free

int main(void) {
    static const char *fixtures[] = {
        "0 0 9 6 re W n q 3 0 6 12 re W* n 1 0 0 rg 0 0 18 12 re f Q q q",
        "q 0 0 9 12 re W n q 0 0 18 6 re W n Q Q 0 0 18 12 re W n",
        "0 0 9 12 re W n q (unterminated",
        "0 0 9 12 re W n /F0 Do 0 0 1 rg 0 6 18 6 re f",
        "0 0 9 12 re W n 18 0 0 12 0 0 cm /Im0 Do",
        "18 0 0 12 0 0 cm /Im1 Do",
        "/G gs 3 w 0 6 m 18 6 l 0 6 m 18 6 l S",
        "/G gs 0 0 18 12 re B",
        "/FontForm Do"
    };
    const char *form_content = "q 0 0 18 6 re W n 1 0 0 rg 0 0 18 12 re f q";
    const char *font_content = "BT /F1 6 Tf 0 6 Td (Z) Tj ET";
    const char *cmap = "1 beginbfchar <5A> <0041> endbfchar";
    char document[3072];
    int document_size = snprintf(document, sizeof(document),
        "3 0 obj << /Resources << /XObject << /F0 5 0 R /Im0 6 0 R /Im1 8 0 R /FontForm 11 0 R >> /ExtGState << /G << /ca .5 /CA .5 /BM /Multiply >> >> >> >> endobj\n"
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
        "13 0 obj << /Length %zu >> stream\n%s\nendstream endobj\n",
        strlen(form_content), form_content, strlen(font_content), font_content, strlen(cmap), cmap);
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
            int ok = cupidimage_pdf_render_content_stream((const unsigned char *)fixtures[fixture],
                strlen(fixtures[fixture]), (const unsigned char *)document, (size_t)document_size,
                3, NULL, &image, &geometry, error, sizeof(error));
            if (failure == 0) total = allocation_count;
            checks++;
            if (live_count || (failure == 0 && ok != (fixture != 2))) {
                fprintf(stderr, "FAIL fixture %zu allocation %zu: %zu live allocations, result %d\n",
                        fixture, failure, live_count, ok);
                failures++;
            }
            while (live_count) fault_free(live[0]);
        }
    }
    printf("PDF clip allocation failures: %zu checks, %zu failures\n", checks, failures);
    return failures != 0;
}
