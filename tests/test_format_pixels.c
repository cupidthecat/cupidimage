#include "cupidimage.h"
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    cupidimage_image image = {0};
    char error[256] = {0};
    if (!cupidimage_load_image_file(argv[1], &image, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    int ok = 0;
    if (!strcmp(argv[2], "pixels")) {
        uint32_t header[] = {image.width, image.height, image.hotspot_x, image.hotspot_y};
        size_t count = (size_t)image.width * image.height;
        ok = fwrite(header, sizeof(header), 1, stdout) == 1 &&
             fwrite(image.rgba, 4, count, stdout) == count;
    } else if (!strcmp(argv[2], "ansi")) {
        ok = cupidimage_render_ansi(&image, stdout, 0, 0);
    } else if (!strcmp(argv[2], "kitty")) {
        cupidimage_kitty_options options = {42, 7, 1, 0};
        ok = cupidimage_render_kitty_with_options(&image, stdout, 0, 0, &options, error,
                                                  sizeof(error));
    } else if (!strcmp(argv[2], "sixel")) {
        cupidimage_sixel_options options = {0};
        options.max_colors = 256;
        options.background_color = 0xFFFFFF;
        options.pixel_aspect_ratio = 1.0f;
        ok = cupidimage_render_sixel_with_options(&image, stdout, 0, 0, &options, error,
                                                  sizeof(error));
    }
    cupidimage_free(&image);
    if (!ok)
        fprintf(stderr, "%s\n", error);
    return ok ? 0 : 1;
}
