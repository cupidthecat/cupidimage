#include "compression/ccitt.h"
#include "formats/pdf/pdf_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;

static void check(const char *parameters, const char *bits, const unsigned char *expected,
                  size_t expected_size, int valid) {
    unsigned char encoded[4096] = {0};
    size_t n = strlen(bits);
    if (n > sizeof(encoded) * 8)
        abort();
    for (size_t i = 0; i < n; i++) {
        if (bits[i] != '0' && bits[i] != '1')
            abort();
        encoded[i / 8] |= (unsigned char)((bits[i] - '0') << (7 - i % 8));
    }
    char dictionary[1024];
    snprintf(dictionary, sizeof(dictionary), "<< /Filter /CCITTFaxDecode /DecodeParms << %s >> >>",
             parameters);
    pdf_object_view view = {0};
    view.data = (const unsigned char *)dictionary;
    view.len = strlen(dictionary);
    unsigned char *decoded = NULL;
    size_t size = 0;
    int ok = cupidimage_pdf_decode_stream(&view, encoded, (n + 7) / 8, &decoded, &size);
    checks++;
    if (ok != valid ||
        (ok && (size != expected_size || (size && memcmp(decoded, expected, size))))) {
        fprintf(stderr, "fax check %u failed: ok=%d size=%zu %s\n", checks, ok, size, parameters);
        failures++;
    }
    free(decoded);
}

#define EOL "000000000001"
#define WHITE8 "10011"
#define BLACK8                                                                                     \
    "00110101"                                                                                     \
    "000101"
#define BOUNDED "/Columns 8 /Rows 1 /EndOfBlock false"

int main(int argc, char **argv) {
    if (argc == 7) {
        FILE *file = fopen(argv[1], "rb");
        unsigned char input[65536];
        if (!file)
            return 2;
        size_t length = fread(input, 1, sizeof(input), file);
        int failed = ferror(file) || !feof(file);
        fclose(file);
        if (failed)
            return 2;
        cupidimage_ccitt_options options = {
            atoi(argv[4]), atoi(argv[2]), atoi(argv[3]), 0, 0, atoi(argv[5]), atoi(argv[6]), 0};
        unsigned char *output = NULL;
        size_t size = 0;
        int ok = cupidimage_ccitt_decode(input, length, &options, &output, &size);
        if (ok)
            ok = fwrite(output, 1, size, stdout) == size;
        free(output);
        return ok ? 0 : 3;
    }
    if (argc != 1)
        return 2;
    const unsigned char white[] = {255}, black[] = {0}, pair[] = {255, 0},
                        recovery[] = {0, 0, 255, 255}, inverse[] = {0, 255};
    check(BOUNDED, WHITE8, white, 1, 1);
    check(BOUNDED, BLACK8, black, 1, 1);
    check(BOUNDED " /BlackIs1 true", WHITE8, black, 1, 1);
    check(BOUNDED " /BlackIs1 true", BLACK8, white, 1, 1);
    check(BOUNDED " /EndOfLine true", EOL WHITE8, white, 1, 1);
    check(BOUNDED, EOL WHITE8, white, 1, 1);
    check(BOUNDED " /EndOfLine true", WHITE8, NULL, 0, 0);
    check("/Columns 8 /Rows 2 /EndOfBlock false", WHITE8 BLACK8, pair, 2, 1);
    check("/Columns 8 /Rows 2 /EndOfBlock false /BlackIs1 true", WHITE8 BLACK8, inverse, 2, 1);
    check("/Columns 8 /Rows 2 /EndOfBlock false /EncodedByteAlign true", WHITE8 "000" BLACK8, pair,
          2, 1);
    check("/Columns 8 /Rows 9", WHITE8 EOL EOL EOL EOL EOL EOL, white, 1, 1);
    check("/Columns 8 /Rows 1", WHITE8, NULL, 0, 0);
    check("/K -1 /Columns 8", "1" EOL EOL, white, 1, 1);
    check("/K -1 /Columns 8 /Rows 9", "1" EOL EOL, white, 1, 1);
    check("/K -1 " BOUNDED, "001" BLACK8, black, 1, 1);
    check("/K -1 /Columns 8 /Rows 2 /EndOfBlock false", "001" BLACK8 "11",
          (const unsigned char[]){0, 0}, 2, 1);
    check("/K 2 /Columns 8 /Rows 2 /EndOfBlock false /EndOfLine true",
          EOL "1" BLACK8 EOL "0"
              "11",
          (const unsigned char[]){0, 0}, 2, 1);
    check("/K 27 /Columns 8", EOL "1" WHITE8 EOL "1" EOL "1" EOL "1" EOL "1" EOL "1" EOL "1", white,
          1, 1);
    check("/Columns 3 /Rows 1 /EndOfBlock false", "1000", white, 1, 1);
    check("/Columns 3 /Rows 1 /EndOfBlock false /BlackIs1 true",
          "00110101"
          "10",
          (const unsigned char[]){224}, 1, 1);
    check(BOUNDED, "00000000000000000000", NULL, 0, 0);
    check(BOUNDED, "00110101", NULL, 0, 0);
    check("/Columns 0", WHITE8, NULL, 0, 0);
    check("/Columns 1048577", WHITE8, NULL, 0, 0);
    check(BOUNDED " /BlackIs1 trueX", WHITE8, NULL, 0, 0);
    check(BOUNDED " /BlackIs1 1", WHITE8, NULL, 0, 0);
    check(BOUNDED " /K -1.5", WHITE8, NULL, 0, 0);
    check(BOUNDED " /DamagedRowsBeforeError 1", WHITE8, white, 1, 1);
    check("/K -1 " BOUNDED " /DamagedRowsBeforeError 1", "1", white, 1, 1);
    check("/Columns 8 /Rows 4 /EndOfBlock false /EndOfLine true /DamagedRowsBeforeError 2",
          EOL BLACK8 EOL "0000000000000" EOL "0000000000000" EOL WHITE8, recovery, 4, 1);
    check("/Columns 8 /Rows 4 /EndOfBlock false /EndOfLine true /DamagedRowsBeforeError 1",
          EOL BLACK8 EOL "0000000000000" EOL "0000000000000" EOL WHITE8, NULL, 0, 0);
    check(BOUNDED " /EndOfLine true /DamagedRowsBeforeError 1", EOL "0000000000000", NULL, 0, 0);
    check("/K -1 /Columns 8 /EndOfBlock false", "1" EOL EOL, white, 1, 1);
    check("/Columns 8 /EndOfBlock false", WHITE8 EOL EOL EOL EOL EOL EOL, white, 1, 1);
    printf("PDF fax filters: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
