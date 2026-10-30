#include "cupidimage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 7)
        return 2;
    FILE *input = fopen(argv[6], "rb");
    if (!input || fseek(input, 0, SEEK_END))
        return 2;
    long size = ftell(input);
    if (size < 1 || fseek(input, 0, SEEK_SET))
        return 2;
    uint8_t *pixels = malloc((size_t)size);
    uint8_t *copy = malloc((size_t)size);
    if (!pixels || !copy || fread(pixels, 1, (size_t)size, input) != (size_t)size)
        return 2;
    fclose(input);
    memcpy(copy, pixels, (size_t)size);
    cupidimage_image image = {0};
    image.width = (uint32_t)strtoul(argv[1], NULL, 10);
    image.height = (uint32_t)strtoul(argv[2], NULL, 10);
    image.rgba = pixels;
    cupidimage_sixel_options options = {0};
    options.use_transparency = (uint8_t)atoi(argv[3]);
    options.dither_mode = (uint8_t)atoi(argv[4]);
    options.max_colors = (uint32_t)atoi(argv[5]);
    options.background_color = 0xFFFFFF;
    char error[256] = {0};
    int ok = atoi(argv[3]) < 0 ? cupidimage_render_sixel(&image, stdout, 0, 0, error, sizeof(error))
                               : cupidimage_render_sixel_with_options(
                                     &image, stdout, 0, 0, &options, error, sizeof(error));
    int changed = memcmp(copy, pixels, (size_t)size) != 0;
    free(copy);
    free(pixels);
    if (changed)
        return 1;
    if (!ok)
        fprintf(stderr, "%s\n", error);
    return ok ? 0 : 3;
}
