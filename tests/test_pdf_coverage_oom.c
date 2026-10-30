#include "fault_alloc.h"
#include <assert.h>
#define malloc fault_malloc
#define calloc fault_calloc
#define free fault_free
#include "../src/formats/pdf/raster/coverage.c"
#undef malloc
#undef calloc
#undef free

int main(void) {
    (void)fault_realloc;
    pdf_coverage_point p[] = {
        {-500, -500, 1, 0}, {3000, 0, 2, 0}, {0, 3000, 2, 0}, {1000, 1000, 1, 1}};
    unsigned checks = 0;
    for (size_t failure = 0; failure <= 6; failure++)
        for (size_t budget = 0; budget <= 4000; budget += 17) {
            allocation_count = 0;
            fail_at = failure;
            unsigned char *out = (void *)1;
            size_t work = budget;
            int ok = cupidimage_pdf_coverage(p, 4, 16, 16, 0, &work, &out);
            assert(work <= budget);
            if (ok) {
                assert(out);
                fault_free(out);
            } else
                assert(!out);
            assert(!live_count);
            checks++;
        }
    for (int empty = 0; empty < 2; empty++)
        for (size_t failure = 0; failure <= 6; failure++) {
            allocation_count = 0;
            fail_at = failure;
            size_t work = 100000000;
            unsigned char *out = NULL;
            int ok =
                cupidimage_pdf_coverage(empty ? NULL : p, empty ? 0 : 4, 16, 16, 0, &work, &out);
            unsigned allocations = empty ? 2 : 6;
            assert(ok == (failure == 0 || failure > allocations));
            if (ok) {
                assert(allocation_count == allocations);
                fault_free(out);
            } else
                assert(!out);
            assert(!live_count);
            checks++;
        }
    printf("PDF coverage allocation/cancellation: %u cleanup checks passed\n", checks);
    return 0;
}
