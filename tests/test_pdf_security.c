#include "cupidimage.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3)
        return 2;
    unsigned char password_bytes[4097];
    size_t length = fread(password_bytes, 1, sizeof(password_bytes), stdin);
    if (length > 4096 || ferror(stdin))
        return 2;
    cupidimage_pdf_password password = {password_bytes, length, argc == 3 ? atoi(argv[2]) : 0};
    cupidimage_pdf_options options = {0};
    cupidimage_image image = {0};
    char error[256] = {0};
    int count = 0;
    int ok = cupidimage_get_pdf_page_count_file_with_password(argv[1], &count, &password, error,
                                                              sizeof(error)) &&
             cupidimage_load_pdf_page_file_with_password(argv[1], &image, 0, &options, &password,
                                                         error, sizeof(error));
    memset(password_bytes, 0, sizeof(password_bytes));
    if (!ok) {
        fprintf(stderr, "%s\n", error);
        cupidimage_free(&image);
        return 3;
    }
    uint32_t header[] = {image.width, image.height, (uint32_t)count};
    ok = fwrite(header, sizeof(header), 1, stdout) == 1 &&
         fwrite(image.rgba, 4, (size_t)image.width * image.height, stdout) ==
             (size_t)image.width * image.height;
    cupidimage_free(&image);
    return ok ? 0 : 1;
}
