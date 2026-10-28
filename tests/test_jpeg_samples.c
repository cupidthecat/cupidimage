#include "core/internal.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 6)
        return 2;
    unsigned char *data = NULL, *samples = NULL;
    size_t size = 0;
    if (!cupidimage_read_file_bytes(argv[1], &data, &size, NULL, 0))
        return 2;
    int width = atoi(argv[2]), height = atoi(argv[3]), components = atoi(argv[4]);
    int ok = cupidimage_jpeg_decode_samples(data, size, width, height, components, atoi(argv[5]),
                                            &samples);
    free(data);
    if (!ok)
        return 3;
    size_t count = (size_t)width * height * components;
    ok = fwrite(samples, 1, count, stdout) == count;
    free(samples);
    return ok ? 0 : 2;
}
