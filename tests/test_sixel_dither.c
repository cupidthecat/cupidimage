#define _GNU_SOURCE
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fail_at = -1, allocations;

static void *controlled_calloc(size_t count, size_t size) {
    allocations++;
    if (allocations == fail_at)
        return NULL;
    return calloc(count, size);
}

#define calloc controlled_calloc
#ifndef SIXEL_SOURCE
#define SIXEL_SOURCE "../src/output/sixel/cupidimage_sixel.c"
#endif
#include SIXEL_SOURCE
#undef calloc

static FILE *reference;
static int record, cases;
static uint32_t random_value = 123456789;

static uint32_t next_random(void) {
    random_value ^= random_value << 13;
    random_value ^= random_value >> 17;
    random_value ^= random_value << 5;
    return random_value;
}

static int check(unsigned pattern, unsigned mode, unsigned alpha, unsigned colors, int failure,
                 int emit) {
    cupidimage_image image = {.width = pattern == 4   ? 1
                                       : pattern == 5 ? 17
                                                      : 13,
                              .height = pattern == 4   ? 17
                                        : pattern == 5 ? 1
                                                       : 11};
    image.rgba = malloc((size_t)image.width * image.height * 4);
    assert(image.rgba);
    random_value = 123456789;
    for (uint32_t y = 0; y < image.height; y++)
        for (uint32_t x = 0; x < image.width; x++) {
            uint32_t value = next_random();
            size_t index = ((size_t)y * image.width + x) * 4;
            image.rgba[index] = (uint8_t)(value >> 16);
            image.rgba[index + 1] = (uint8_t)(value >> 8);
            image.rgba[index + 2] = (uint8_t)value;
            image.rgba[index + 3] = pattern == 2   ? 0
                                    : pattern == 1 ? (uint8_t)(((x + y) % 3) * 127)
                                                   : 255;
            if (pattern == 3) {
                image.rgba[index] = ((x + y) & 1) * 255;
                image.rgba[index + 1] = 0;
                image.rgba[index + 2] = 0;
            }
        }
    char *data = NULL;
    size_t length = 0;
    FILE *output = open_memstream(&data, &length);
    assert(output);
    cupidimage_sixel_options options = {.max_colors = colors,
                                        .dither_mode = (uint8_t)mode,
                                        .use_transparency = (uint8_t)alpha,
                                        .pixel_aspect_ratio = 1.0f};
    char error[256] = {0};
    allocations = 0;
    fail_at = failure;
    int result =
        cupidimage_render_sixel_with_options(&image, output, 0, 0, &options, error, sizeof(error));
    int allocated = allocations;
    fail_at = -1;
    assert(!fclose(output));
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < length; i++) {
        hash ^= (unsigned char)data[i];
        hash *= UINT64_C(1099511628211);
    }
    char line[256];
    snprintf(line, sizeof(line),
             "pattern=%u mode=%u alpha=%u colors=%u fail=%d rc=%d bytes=%zu hash=%016" PRIx64 "\n",
             pattern, mode, alpha, colors, failure, result, length, hash);
    if (emit) {
        if (record)
            fputs(line, stdout);
        else {
            char expected[256];
            assert(fgets(expected, sizeof(expected), reference));
            if (strcmp(line, expected)) {
                fprintf(stderr, "Expected %sActual   %s", expected, line);
                abort();
            }
        }
        cases++;
    }
    free(image.rgba);
    free(data);
    return allocated;
}

int main(int argc, char **argv) {
    record = argc == 2 && !strcmp(argv[1], "--record");
    if (!record) {
        reference = fopen(argc == 2 ? argv[1] : "tests/fixtures/sixel_dither_reference.txt", "r");
        assert(reference);
    }
    unsigned modes[] = {0, 1, 2, 3, 255}, colors[] = {2, 16, 256};
    for (unsigned pattern = 0; pattern < 6; pattern++)
        for (size_t m = 0; m < 5; m++)
            for (unsigned alpha = 0; alpha < 2; alpha++)
                for (size_t c = 0; c < 3; c++)
                    check(pattern, modes[m], alpha, colors[c], -1, 1);
    for (unsigned mode = 1; mode <= 2; mode++)
        for (unsigned alpha = 0; alpha < 2; alpha++) {
            int total = check(1, mode, alpha, 16, -1, 0);
            for (int failure = total - 3; failure <= total; failure++)
                check(1, mode, alpha, 16, failure, 1);
        }
    if (!record) {
        assert(fgetc(reference) == EOF);
        fclose(reference);
        printf("Sixel dither: %d byte-identical reference and allocation-fallback cases passed\n",
               cases);
    }
    return 0;
}
