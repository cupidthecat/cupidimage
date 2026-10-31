#include "fault_alloc.h"
#include "formats/pdf/fonts/truetype_data.h"
#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/formats/pdf/fonts/cff.c"
#include "../src/formats/pdf/fonts/truetype.c"
#include "../src/formats/pdf/fonts/truetype_subset.c"
#undef malloc
#undef calloc
#undef realloc
#undef free

static int run(const unsigned char *data, size_t length, int cff, int recover, unsigned glyph,
               int output) {
    int ok;
    if (cff) {
        pdf_cff_font font;
        ok = recover ? cupidimage_pdf_cff_init_subset(&font, data, length)
                     : cupidimage_pdf_cff_init(&font, data, length);
        if (ok && output) {
            printf("cff %u %u\n", font.chars.count, font.private_count);
            for (unsigned i = 0; i < font.chars.count; i++)
                printf("%u %u\n", i, font.fdselect ? font.fdselect[i] : 0);
        }
        cupidimage_pdf_cff_free(&font);
    } else {
        pdf_tt_font font;
        uint32_t *ends = NULL;
        ok = recover ? cupidimage_pdf_tt_init_subset(&font, data, length, &ends)
                     : cupidimage_pdf_tt_init(&font, data, length);
        pdf_tt_outline outline = {0};
        if (ok)
            ok = cupidimage_pdf_tt_outline(&font, glyph, &outline);
        if (ok && output) {
            printf("tt %u %u %d\n", font.units, font.glyphs, ends != NULL);
            printf("glyph %u %u %u\n", glyph, outline.advance, outline.count);
            for (unsigned i = 0; i < outline.count; i++)
                printf("%.17g %.17g %u %u\n", outline.points[i].x, outline.points[i].y,
                       outline.points[i].on, outline.points[i].end);
        }
        cupidimage_pdf_tt_outline_free(&outline);
        fault_free(ends);
    }
    return ok;
}

int main(int argc, char **argv) {
    if (argc < 4 || argc > 5)
        return 2;
    if (strcmp(argv[2], "tt-strict") && strcmp(argv[2], "tt-recover") &&
        strcmp(argv[2], "cff-strict") && strcmp(argv[2], "cff-recover"))
        return 2;
    int cff = !strncmp(argv[2], "cff-", 4), recover = strstr(argv[2], "recover") != NULL;
    int faults = argc == 5 && !strcmp(argv[4], "--faults");
    char *end;
    unsigned long glyph = strtoul(argv[3], &end, 10);
    if (!*argv[3] || *end || glyph > 65535)
        return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 2;
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return 2;
    }
    long length = ftell(file);
    if (length < 0 || length > 32 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return 2;
    }
    unsigned char *data = malloc((size_t)length + 1);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return 2;
    }
    fclose(file);
    size_t total = 0, checks = 0;
    int ok = 1;
    for (size_t fail = 0; fail <= total; fail++) {
        fail_at = fail;
        allocation_count = 0;
        int result = run(data, (size_t)length, cff, recover, (unsigned)glyph, !faults && !fail);
        if (!fail) {
            ok = result;
            if (faults)
                total = allocation_count;
        }
        if (live_count || (fail && result)) {
            fprintf(stderr, "subset-font ownership failure at allocation %zu\n", fail);
            ok = 0;
        }
        checks++;
    }
    free(data);
    if (faults && ok)
        printf("%zu allocation checks\n", checks);
    return ok ? 0 : 3;
}
