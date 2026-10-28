#include "core/internal.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4)
        return 2;
    char *end = NULL;
    unsigned long capacity = strtoul(argv[2], &end, 10);
    if (!end || *end || capacity > 1048576)
        return 2;
    unsigned char *input = NULL;
    size_t size = 0, used = 999;
    char error[128];
    if (!cupidimage_read_file_bytes(argv[1], &input, &size, error, sizeof(error)))
        return 2;
    unsigned char *output = malloc(capacity ? capacity : 1);
    if (!output) {
        free(input);
        return 2;
    }
    size_t consumed = 999;
    int ok = argc == 4
                 ? cupidimage_inflate_zlib_prefix(input, size, output, capacity, &used, &consumed)
                 : cupidimage_inflate_zlib(input, size, output, capacity, &used);
    if (argc == 4) {
        if (!ok && consumed) {
            free(input);
            free(output);
            return 3;
        }
        if (ok)
            fprintf(stderr, "%zu", consumed);
    }
    free(input);
    if (!ok && used != 0) {
        free(output);
        return 3;
    }
    if (ok && used && fwrite(output, 1, used, stdout) != used) {
        free(output);
        return 2;
    }
    free(output);
    return ok ? 0 : 1;
}
