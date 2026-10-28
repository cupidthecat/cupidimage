#include "cupidimage.h"
#include <stdlib.h>

int main(int argc, char **argv) {
    cupidimage_image image = {0};
    char error[256] = {0};
    if (argc != 4)
        return 2;
    if (!cupidimage_load_webp_file(argv[1], &image, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    int ok =
        image.width == strtoul(argv[2], NULL, 10) && image.height == strtoul(argv[3], NULL, 10);
    size_t pixels = (size_t)image.width * image.height;
    if (ok)
        ok = fwrite(image.rgba, 4, pixels, stdout) == pixels;
    cupidimage_free(&image);
    return ok ? 0 : 1;
}
