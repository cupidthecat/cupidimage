#include "cupidimage.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    cupidimage_image image = {0};
    char error[256] = {0};
    int count = 0;
    if (!cupidimage_get_pdf_page_count_file(argv[1], &count, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 3;
    }
    if (!cupidimage_load_pdf_page_file(argv[1], &image, atoi(argv[2]), error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        cupidimage_free(&image);
        return 3;
    }
    uint32_t header[3] = {image.width, image.height, (uint32_t)count};
    int ok = fwrite(header, sizeof(header), 1, stdout) == 1 &&
             fwrite(image.rgba, 4, (size_t)image.width * image.height, stdout) ==
                 (size_t)image.width * image.height;
    cupidimage_free(&image);
    return ok ? 0 : 1;
}
