#include "cupidimage.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s image-file\n", argv[0]);
        return 1;
    }
    cupidimage_image image = {0};
    char error[256] = {0};
    if (!cupidimage_load_image_file(argv[1], &image, error, sizeof(error))) {
        fprintf(stderr, "load: %s\n", error);
        return 1;
    }
    int ok = cupidimage_render_ansi(&image, stdout, 120, 60);
    cupidimage_free(&image);
    return ok ? 0 : 1;
}
