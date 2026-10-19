#include "cupidimage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 8) return 2;
    cupidimage_image image = {0};
    char error[256] = {0};
    int accepted = atoi(argv[2]);
    int ok = cupidimage_load_jpeg_file(argv[1], &image, error, sizeof(error));
    if (ok != accepted) {
        fprintf(stderr, "expected decode %d, got %d: %s\n", accepted, ok, error);
        cupidimage_free(&image);
        return 1;
    }
    if (ok) {
        if (image.width != (uint32_t)atoi(argv[3]) ||
            image.height != (uint32_t)atoi(argv[4])) {
            fprintf(stderr, "unexpected dimensions %u x %u\n", image.width, image.height);
            cupidimage_free(&image);
            return 1;
        }
        for (size_t i = 0; i < (size_t)image.width * image.height; ++i) {
            for (int c = 0; c < 3; ++c) {
                int expected = atoi(argv[5 + c]);
                if (expected >= 0 && image.rgba[i * 4 + (size_t)c] != expected) {
                    fprintf(stderr, "pixel %zu channel %d: expected %d, got %u\n",
                            i, c, expected, image.rgba[i * 4 + (size_t)c]);
                    cupidimage_free(&image);
                    return 1;
                }
            }
            if (image.rgba[i * 4 + 3] != 255) return 1;
        }
    } else if (image.rgba || !error[0]) {
        fprintf(stderr, "failure must leave no pixels and explain the error\n");
        cupidimage_free(&image);
        return 1;
    }
    cupidimage_free(&image);
    return 0;
}
