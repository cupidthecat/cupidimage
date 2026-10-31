#include "fault_alloc.h"
#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/formats/pdf/fonts/type1.c"
#include "../src/formats/pdf/fonts/type1/hints.c"
#include "../src/formats/pdf/fonts/type1/lexer.c"
#include "../src/formats/pdf/fonts/type1/syntax.c"
#undef malloc
#undef calloc
#undef realloc
#undef free

int main(int argc, char **argv) {
    if (argc != 5)
        return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END)) {
        if (file)
            fclose(file);
        return 2;
    }
    long size = ftell(file);
    if (size < 0 || size > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return 2;
    }
    unsigned char *data = malloc(size ? (size_t)size : 1);
    if (!data || fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return 2;
    }
    fclose(file);
    size_t clear = (size_t)strtoul(argv[2], NULL, 10),
           encrypted = (size_t)strtoul(argv[3], NULL, 10);
    int expected = atoi(argv[4]) != 0;
    size_t total = 0, checks = 0;
    for (size_t failure = 0; failure <= total + 1; failure++) {
        fail_at = failure;
        allocation_count = 0;
        pdf_type1_font font;
        int ok = cupidimage_pdf_type1_init(&font, data, (size_t)size, clear, encrypted);
        if (!failure)
            total = allocation_count;
        if (ok != (expected && (!failure || failure > total))) {
            fprintf(stderr, "Type 1 parser allocation %zu: unexpected result %d\n", failure, ok);
            free(data);
            return 1;
        }
        cupidimage_pdf_type1_free(&font);
        if (live_count) {
            fprintf(stderr, "Type 1 parser allocation %zu: %zu live blocks\n", failure, live_count);
            free(data);
            return 1;
        }
        checks++;
    }
    fail_at = 0;
    free(data);
    printf("%zu\n", checks);
    return 0;
}
