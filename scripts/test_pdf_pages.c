#include "cupidimage.h"
#include "tests/pdf_probe_options.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--capabilities"))
        return pdf_probe_capabilities() ? 0 : 3;
    if (argc < 3)
        return 2;
    cupidimage_pdf_options options = {0};
    if (argc >= 4 && !strncmp(argv[3], "--", 2)) {
        if (!pdf_probe_options(argc, argv, 3, &options))
            return 2;
    } else {
        if (argc > 7)
            return 2;
        options.text_antialias = argc >= 4 ? atoi(argv[3]) : 0;
        options.truetype_hinting = argc >= 5 ? atoi(argv[4]) : 0;
        options.cff_hinting = argc >= 6 ? atoi(argv[5]) : 0;
        options.type1_hinting = argc >= 7 ? atoi(argv[6]) : 0;
    }
    cupidimage_image image = {0};
    char error[256] = {0};
    int count = 0;
    if (!cupidimage_get_pdf_page_count_file(argv[1], &count, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 3;
    }
    if (strcmp(argv[2], "count") == 0) {
        printf("%d\n", count);
        return 0;
    }
    int loaded =
        argc >= 4
            ? cupidimage_load_pdf_page_file_with_options(argv[1], &image, atoi(argv[2]), &options,
                                                         error, sizeof(error))
            : cupidimage_load_pdf_page_file(argv[1], &image, atoi(argv[2]), error, sizeof(error));
    if (!loaded) {
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
