#include "formats/pdf/images/jbig2/decode.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned char *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0 || size > 64 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    unsigned char *data = malloc(size ? (size_t)size : 1);
    if (!data || fread(data, 1, (size_t)size, file) != (size_t)size) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *length = (size_t)size;
    return data;
}

int main(int argc, char **argv) {
    if (argc != 4 && argc != 5)
        return 2;
    size_t length = 0, globals_length = 0;
    unsigned char *data = read_file(argv[1], &length);
    unsigned char *globals = argc == 5 ? read_file(argv[4], &globals_length) : NULL;
    if (!data || (argc == 5 && !globals)) {
        free(data);
        free(globals);
        return 2;
    }
    unsigned char *output = NULL;
    size_t output_length = 0, work = 500000000;
    int ok = cupidimage_pdf_jbig2_decode(
        data, length, globals, globals_length, (uint32_t)strtoul(argv[2], NULL, 10),
        (uint32_t)strtoul(argv[3], NULL, 10), &work, &output, &output_length);
    if (ok)
        ok = fwrite(output, 1, output_length, stdout) == output_length;
    free(output);
    free(data);
    free(globals);
    return ok ? 0 : 1;
}
