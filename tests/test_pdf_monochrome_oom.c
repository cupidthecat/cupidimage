#include "fault_alloc.h"
#include <assert.h>
#define malloc fault_malloc
#define calloc fault_calloc
#define free fault_free
#include "../src/formats/pdf/raster/monochrome.c"
#undef malloc
#undef calloc
#undef free

int main(void) {
    pdf_coverage_point outlines[][7] = {
        {{70, 0, 1, 0}, {90, 0, 1, 0}, {90, 256, 1, 0}, {70, 256, 1, 1}},
        {{-128, 32, 1, 0}, {512, 640, 0, 0}, {1024, 32, 1, 0}, {512, -128, 0, 1}},
        {{0, 0, 1, 0},
         {0, 512, 2, 0},
         {512, 512, 2, 0},
         {512, 0, 1, 0},
         {320, -128, 2, 0},
         {160, -128, 2, 0},
         {0, 0, 1, 1}}};
    unsigned counts[] = {4, 4, 7};
    unsigned checks = 0;
    for (unsigned shape = 0; shape < 3; shape++)
        for (unsigned mode = 0; mode < 8; mode++)
            for (int high = 0; high <= 1; high++) {
                size_t total = 0;
                unsigned char expected[256];
                for (size_t failure = 0; failure <= total + 1; failure++) {
                    fail_at = failure;
                    allocation_count = 0;
                    size_t work = 1000000;
                    unsigned char *output = NULL;
                    int ok = cupidimage_pdf_monochrome(outlines[shape], counts[shape], 16, 16, mode,
                                                       high, &work, &output);
                    if (!failure) {
                        assert(ok);
                        total = allocation_count;
                        memcpy(expected, output, sizeof(expected));
                    }
                    if (ok) {
                        assert(!memcmp(output, expected, sizeof(expected)));
                        fault_free(output);
                    } else
                        assert(!output);
                    assert(ok == (!failure || failure > total));
                    assert(!live_count);
                    checks++;
                }
            }
    (void)fault_realloc;
    printf("Monochrome raster: %u allocation/ownership checks\n", checks);
    return 0;
}
