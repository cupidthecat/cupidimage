#include "fault_alloc.h"
#include <stdint.h>
#define calloc fault_calloc
#define free fault_free
#include "../src/formats/pdf/transparency/storage.c"
#undef calloc
#undef free

static unsigned checks;
#define CHECK(value)                                                                               \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(value)) {                                                                            \
            fprintf(stderr, "group storage check %u\n", __LINE__);                                 \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main(void) {
    (void)fault_realloc;
    for (unsigned channels = 3; channels <= 4; channels++) {
        size_t live_bytes = 123;
        pdf_group_storage store;
        const size_t pixels = GROUP_BLOCK_PIXELS * 2 + 3;
        CHECK(cupidimage_pdf_group_storage_init(&store, pixels, channels, &live_bytes, 4000000));
        CHECK(live_bytes == 123 + 3 * sizeof(double *));
        size_t calls = allocation_count;
        for (size_t pixel = 0; pixel < pixels; pixel++) {
            const double *sample = cupidimage_pdf_group_storage_read(&store, pixel);
            for (unsigned c = 0; c < channels + 2; c++)
                CHECK(sample[c] == 0);
        }
        CHECK(calls == allocation_count);
        for (size_t pixel = 0; pixel < pixels; pixel++) {
            double *sample = cupidimage_pdf_group_storage_write(&store, pixel);
            CHECK(sample != NULL);
            for (unsigned c = 0; c < channels + 2; c++)
                sample[c] = (double)((pixel * 7 + c) % 1024) / 1024;
        }
        CHECK(live_bytes == 123 + 3 * sizeof(double *) + pixels * (channels + 2) * sizeof(double));
        calls = allocation_count;
        for (size_t pixel = 0; pixel < pixels; pixel++) {
            const double *sample = cupidimage_pdf_group_storage_read(&store, pixel);
            for (unsigned c = 0; c < channels + 2; c++)
                CHECK(sample[c] == (double)((pixel * 7 + c) % 1024) / 1024);
        }
        CHECK(!cupidimage_pdf_group_storage_write(&store, pixels));
        CHECK(!cupidimage_pdf_group_storage_write(&store, SIZE_MAX));
        CHECK(cupidimage_pdf_group_storage_read(&store, SIZE_MAX)[0] == 0);
        CHECK(calls == allocation_count);
        cupidimage_pdf_group_storage_detach(&store);
        CHECK(live_bytes == 123);
        CHECK(!cupidimage_pdf_group_storage_write(&store, 0));
        CHECK(cupidimage_pdf_group_storage_read(&store, 1)[2] == 9.0 / 1024);
        cupidimage_pdf_group_storage_free(&store);
        CHECK(live_bytes == 123 && !live_count);
    }
    size_t live_bytes = 0;
    pdf_group_storage first, second;
    size_t block_bytes = GROUP_BLOCK_PIXELS * 5 * sizeof(double);
    size_t limit = block_bytes + 2 * sizeof(double *);
    CHECK(cupidimage_pdf_group_storage_init(&first, GROUP_BLOCK_PIXELS, 3, &live_bytes, limit));
    CHECK(cupidimage_pdf_group_storage_init(&second, GROUP_BLOCK_PIXELS, 3, &live_bytes, limit));
    CHECK(cupidimage_pdf_group_storage_write(&first, 0));
    CHECK(live_bytes == limit);
    CHECK(!cupidimage_pdf_group_storage_write(&second, 0));
    CHECK(cupidimage_pdf_group_storage_read(&second, 0)[0] == 0);
    cupidimage_pdf_group_storage_free(&first);
    CHECK(cupidimage_pdf_group_storage_write(&second, 0));
    cupidimage_pdf_group_storage_free(&second);
    CHECK(!live_bytes && !live_count);
    CHECK(!cupidimage_pdf_group_storage_init(&first, SIZE_MAX, 3, &live_bytes, 1024));
    CHECK(!cupidimage_pdf_group_storage_init(&first, 1, 2, &live_bytes, 1024));
    CHECK(!cupidimage_pdf_group_storage_init(&first, 1, 5, &live_bytes, 1024));
    CHECK(!cupidimage_pdf_group_storage_init(&first, 1, 3, NULL, 1024));
    live_bytes = 1025;
    CHECK(!cupidimage_pdf_group_storage_init(&first, 1, 3, &live_bytes, 1024));
    live_bytes = 0;
    CHECK(cupidimage_pdf_group_storage_init(&first, 0, 3, &live_bytes, 0));
    CHECK(!cupidimage_pdf_group_storage_write(&first, 0));
    cupidimage_pdf_group_storage_free(&first);
    CHECK(!live_bytes && !live_count);
    size_t total = 0;
    for (size_t failure = 0; failure <= total; failure++) {
        allocation_count = 0;
        fail_at = failure;
        int ok = cupidimage_pdf_group_storage_init(&first, GROUP_BLOCK_PIXELS * 2 + 1, 4,
                                                   &live_bytes, 4000000);
        for (size_t i = 0; ok && i < 3; i++)
            ok = cupidimage_pdf_group_storage_write(&first, i * GROUP_BLOCK_PIXELS) != NULL;
        if (!failure)
            total = allocation_count;
        CHECK(ok == !failure);
        cupidimage_pdf_group_storage_free(&first);
        CHECK(!live_bytes && !live_count);
    }
    fail_at = 0;
    printf("Group sample storage: %u checks, %zu allocation failures\n", checks, total);
    return 0;
}
