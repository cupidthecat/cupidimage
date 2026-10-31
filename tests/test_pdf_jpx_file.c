#include "fault_alloc.h"
#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/formats/pdf/images/jpx/boxes.c"
#include "../src/formats/pdf/images/jpx/budget.c"
#include "../src/formats/pdf/images/jpx/channels.c"
#include "../src/formats/pdf/images/jpx/decode.c"
#include "../src/formats/pdf/images/jpx/file.c"
#include "../src/formats/pdf/images/jpx/header.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#include <inttypes.h>

int main(int argc, char **argv) {
    (void)fault_realloc;
    (void)fault_calloc;
    if (argc < 2)
        return 2;
    int faults = 0, reject = 0, summary = 0;
    size_t requested_work = 256u * 1024u * 1024u;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--faults"))
            faults = 1;
        else if (!strcmp(argv[i], "--reject"))
            reject = 1;
        else if (!strcmp(argv[i], "--summary"))
            summary = 1;
        else if (!strncmp(argv[i], "--work=", 7))
            requested_work = (size_t)strtoull(argv[i] + 7, NULL, 10);
        else
            return 2;
    }
    FILE *input = fopen(argv[1], "rb");
    if (!input || fseek(input, 0, SEEK_END))
        return 2;
    long length = ftell(input);
    if (length < 0 || length > 64 * 1024 * 1024 || fseek(input, 0, SEEK_SET))
        return 2;
    unsigned char *data = malloc((size_t)length + 1);
    if (!data || fread(data, 1, (size_t)length, input) != (size_t)length)
        return 2;
    fclose(input);
    size_t total = 0, checks = 0;
    int ok = 1;
    for (size_t fail = 0; fail <= total; fail++) {
        fail_at = fail;
        allocation_count = 0;
        pdf_jpx_file file;
        char error[256] = {0};
        size_t work = requested_work;
        int result = cupidimage_pdf_jpx_file_decode(data, (size_t)length, &file, &work, error,
                                                    sizeof(error));
        if (!fail) {
            ok = result == !reject;
            if (faults)
                total = allocation_count;
            else if (result) {
                printf("%u %u %u %u %u %d %zu\n", file.image.width, file.image.height,
                       file.channels, file.ordinary, file.opacity_channels, file.chroma_key, work);
                for (unsigned c = 0; c < file.ordinary; c++)
                    printf("color %u %u %u\n", file.color_order[c], file.alpha[c],
                           file.alpha_type[c]);
                if (file.chroma_key) {
                    printf("keys");
                    for (unsigned c = 0; c < file.ordinary; c++)
                        printf(" %" PRIu64, file.keys[c]);
                    putchar('\n');
                }
                for (unsigned c = 0; c < file.channels && !summary; c++) {
                    uint64_t initial;
                    unsigned precision;
                    int is_signed;
                    if (!cupidimage_pdf_jpx_channel_sample(&file, c, 0, 0, &initial, &precision,
                                                           &is_signed)) {
                        ok = 0;
                        break;
                    }
                    printf("channel %u %d\n", precision, is_signed);
                    for (unsigned y = 0; y < file.image.height; y++)
                        for (unsigned x = 0; x < file.image.width; x++) {
                            uint64_t value;
                            unsigned precision;
                            int is_signed;
                            if (!cupidimage_pdf_jpx_channel_sample(&file, c, x, y, &value,
                                                                   &precision, &is_signed)) {
                                ok = 0;
                                break;
                            }
                            printf("%" PRIu64 " ", value);
                        }
                    putchar('\n');
                }
                for (unsigned c = 0; c < file.header.color_count; c++) {
                    const pdf_jpx_color *color = file.header.colors + c;
                    printf("space %u %d %u %u %zu\n", color->method, color->precedence,
                           color->approximation, color->enumeration, color->parameters.length);
                }
            } else if (!reject)
                fprintf(stderr, "%s\n", error);
        }
        cupidimage_pdf_jpx_file_free(&file);
        if (live_count || (fail && result) || jpx_current_budget) {
            fprintf(stderr, "JPEG 2000 file ownership failure at allocation %zu\n", fail);
            ok = 0;
        }
        checks++;
    }
    free(data);
    if (faults && ok)
        printf("%zu allocation checks\n", checks);
    return ok ? 0 : 3;
}
