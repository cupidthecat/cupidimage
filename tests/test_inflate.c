#include "cupidimage_internal.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    char *end = NULL;
    unsigned long capacity = strtoul(argv[2], &end, 10);
    if (!end || *end || capacity > 1048576) return 2;
    unsigned char *input = NULL;
    size_t size = 0, used = 999;
    char error[128];
    if (!cupidimage_read_file_bytes(argv[1], &input, &size, error, sizeof(error))) return 2;
    unsigned char *output = malloc(capacity ? capacity : 1);
    if (!output) { free(input); return 2; }
    int ok = cupidimage_inflate_zlib(input, size, output, capacity, &used);
    free(input);
    if (!ok && used != 0) { free(output); return 3; }
    if (ok && used && fwrite(output, 1, used, stdout) != used) { free(output); return 2; }
    free(output);
    return ok ? 0 : 1;
}
