#include "fault_alloc.h"
#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/formats/pdf/images/jpx/budget.c"
#include "../src/formats/pdf/images/jpx/decode.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#include <assert.h>

static int allocator_checks(void) {
    unsigned checks = 0;
#define CHECK(value)                                                                               \
    do {                                                                                           \
        checks++;                                                                                  \
        assert(value);                                                                             \
    } while (0)
    size_t work = 200000;
    pdf_jpx_budget outer, inner;
    cupidimage_jpx_budget_begin(&outer, 4096, &work);
    unsigned char *a = cupidimage_jpx_opj_malloc(17);
    CHECK(a && !((uintptr_t)a & 31));
    memset(a, 42, 17);
    a = cupidimage_jpx_opj_aligned_32_realloc(a, 80);
    CHECK(a && !((uintptr_t)a & 31));
    for (unsigned i = 0; i < 17; i++)
        CHECK(a[i] == 42);
    cupidimage_jpx_budget_begin(&inner, 4096, &work);
    unsigned char *b = cupidimage_jpx_opj_calloc(7, 9);
    CHECK(b && !((uintptr_t)b & 31));
    for (unsigned i = 0; i < 63; i++)
        CHECK(!b[i]);
    cupidimage_jpx_opj_free(a);
    cupidimage_jpx_opj_free(b);
    CHECK(cupidimage_jpx_budget_end(&inner));
    CHECK(cupidimage_jpx_budget_end(&outer));
    CHECK(!live_count && jpx_current_budget == NULL);
    for (unsigned kind = 0; kind < 5; kind++) {
        work = kind == 1 ? 0 : 100000;
        cupidimage_jpx_budget_begin(&outer, kind == 0 ? 1 : 4096, &work);
        if (kind == 2) {
            CHECK(!cupidimage_jpx_opj_calloc(SIZE_MAX, 2));
        } else if (kind == 3) {
            CHECK(cupidimage_jpx_opj_malloc(1)); /* Scope cleanup must reject a leaked owner. */
        } else {
            if (kind == 4)
                outer.calls_left = 0;
            CHECK(!cupidimage_jpx_opj_malloc(1));
        }
        CHECK(!cupidimage_jpx_budget_end(&outer));
        CHECK(!live_count && jpx_current_budget == NULL);
    }
    work = 200000;
    cupidimage_jpx_budget_begin(&outer, 4096, &work);
    CHECK(cupidimage_jpx_codec_block_work(0, 0, 64, 64, 8, 0));
    CHECK(!cupidimage_jpx_codec_block_work(0, 0, 65, 64, 8, 0));
    CHECK(!cupidimage_jpx_codec_block_work(1, 0, 0, 64, 8, 0));
    CHECK(!cupidimage_jpx_codec_block_work(0, 0, 64, 64, 32, 0));
    CHECK(!cupidimage_jpx_codec_work(UINT64_MAX));
    CHECK(!cupidimage_jpx_budget_end(&outer));
    CHECK(!live_count);
    printf("JPEG 2000 allocation scopes: %u checks passed\n", checks);
#undef CHECK
    return 0;
}

int main(int argc, char **argv) {
    (void)fault_realloc;
    (void)fault_calloc;
    if (argc == 2 && !strcmp(argv[1], "--allocator"))
        return allocator_checks();
    if (argc < 2)
        return 2;
    int faults = 0, reject = 0;
    size_t requested_work = 256u * 1024u * 1024u;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--faults"))
            faults = 1;
        else if (!strcmp(argv[i], "--reject"))
            reject = 1;
        else if (!strncmp(argv[i], "--work=", 7))
            requested_work = (size_t)strtoull(argv[i] + 7, NULL, 10);
        else
            return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END))
        return 2;
    long length = ftell(file);
    if (length < 0 || length > 64 * 1024 * 1024 || fseek(file, 0, SEEK_SET))
        return 2;
    unsigned char *data = malloc((size_t)length + 1);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length)
        return 2;
    fclose(file);
    size_t total = 0, checks = 0;
    int ok = 1;
    for (size_t fail = 0; fail <= total; fail++) {
        fail_at = fail;
        allocation_count = 0;
        pdf_jpx_samples image;
        char error[256] = {0};
        size_t work = requested_work;
        int result = cupidimage_pdf_jpx_codestream(data, (size_t)length, &image, &work, error,
                                                   sizeof(error));
        if (!fail) {
            ok = result == !reject;
            if (faults)
                total = allocation_count;
            else if (result) {
                printf("%u %u %u %u %u %zu\n", image.width, image.height, image.components,
                       image.x0, image.y0, work);
                for (unsigned c = 0; c < image.components; c++) {
                    const pdf_jpx_plane *plane = image.planes + c;
                    printf("plane %u %u %u %u %u %u %u %u\n", plane->width, plane->height,
                           plane->dx, plane->dy, plane->x0, plane->y0, plane->precision,
                           plane->is_signed);
                    for (size_t i = 0; i < (size_t)plane->width * plane->height; i++)
                        printf("%u ", plane->samples[i]);
                    putchar('\n');
                }
            } else if (!reject)
                fprintf(stderr, "%s\n", error);
        }
        cupidimage_pdf_jpx_samples_free(&image);
        if (live_count || (fail && result) || jpx_current_budget) {
            fprintf(stderr, "JPEG 2000 ownership failure at allocation %zu\n", fail);
            ok = 0;
        }
        checks++;
    }
    free(data);
    if (faults && ok)
        printf("%zu allocation checks\n", checks);
    return ok ? 0 : 3;
}
