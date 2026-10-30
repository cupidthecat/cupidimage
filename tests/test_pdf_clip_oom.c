#include "fault_alloc.h"

#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/cupidimage_pdf.c"
#undef malloc
#undef calloc
#undef realloc
#undef free

int main(void) {
    static const char *fixtures[] = {
        "0 0 9 6 re W n q 3 0 6 12 re W* n 1 0 0 rg 0 0 18 12 re f Q q q",
        "q 0 0 9 12 re W n q 0 0 18 6 re W n Q Q 0 0 18 12 re W n",
        "0 0 9 12 re W n q (unterminated", "0 0 9 12 re W n /F0 Do 0 0 1 rg 0 6 18 6 re f"};
    const char *form_content = "q 0 0 18 6 re W n 1 0 0 rg 0 0 18 12 re f q";
    char document[1024];
    int document_size =
        snprintf(document, sizeof(document),
                 "3 0 obj << /Resources << /XObject << /F0 5 0 R >> >> >> endobj\n"
                 "5 0 obj << /Type /XObject /Subtype /Form /BBox [0 0 18 12] /Length %zu >>\n"
                 "stream\n%s\nendstream\nendobj\n",
                 strlen(form_content), form_content);
    size_t checks = 0, failures = 0;
    for (size_t fixture = 0; fixture < sizeof(fixtures) / sizeof(fixtures[0]); fixture++) {
        size_t total = 0;
        for (size_t failure = 0; failure <= total; failure++) {
            uint8_t pixels[24 * 16 * 4];
            cupidimage_image image = {0};
            image.width = 24;
            image.height = 16;
            image.rgba = pixels;
            pdf_page_geometry geometry = {0, 0, 18, 12, 0, 24, 16};
            char error[128] = {0};
            allocation_count = 0;
            fail_at = failure;
            int ok = pdf_render_content_stream(
                (const unsigned char *)fixtures[fixture], strlen(fixtures[fixture]),
                (const unsigned char *)document, (size_t)document_size, 3, NULL, &image, &geometry,
                error, sizeof(error));
            if (failure == 0)
                total = allocation_count;
            checks++;
            if (live_count || (failure == 0 && ok != (fixture != 2))) {
                fprintf(stderr,
                        "FAIL fixture %zu allocation %zu: %zu live allocations, result %d\n",
                        fixture, failure, live_count, ok);
                failures++;
            }
            while (live_count)
                fault_free(live[0]);
        }
    }
    printf("PDF clip allocation failures: %zu checks, %zu failures\n", checks, failures);
    return failures != 0;
}
