#include "formats/pdf/pdf_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;

static void check(const char *filter, const unsigned char *input, size_t length,
                  const unsigned char *expected, size_t expected_length, int valid) {
    pdf_object_view view = {0};
    view.data = (const unsigned char *)filter;
    view.len = strlen(filter);
    unsigned char *output = NULL;
    size_t size = 0;
    int ok = cupidimage_pdf_decode_stream(&view, input, length, &output, &size);
    checks++;
    if (ok != valid ||
        (ok && (size != expected_length || (size && memcmp(output, expected, size))))) {
        fprintf(stderr, "FAIL filter check %u: %s, status %d, length %zu\n", checks, filter, ok,
                size);
        failures++;
    }
    free(output);
}

int main(void) {
    const char *ascii85 = "<< /Filter /ASCII85Decode >>";
    check(ascii85, (const unsigned char *)"z~>", 3, (const unsigned char *)"\0\0\0\0", 4, 1);
    check(ascii85, (const unsigned char *)"!!~>", 4, (const unsigned char *)"\0", 1, 1);
    check(ascii85, (const unsigned char *)"!!!~>", 5, (const unsigned char *)"\0\0", 2, 1);
    check(ascii85, (const unsigned char *)"!!!!~>", 6, (const unsigned char *)"\0\0\0", 3, 1);
    check(ascii85, (const unsigned char *)"s8W-!~>", 7, (const unsigned char *)"\xff\xff\xff\xff",
          4, 1);
    check(ascii85, (const unsigned char *)"s8W-\"~>", 7, NULL, 0, 0);
    check(ascii85, (const unsigned char *)"~>", 2, NULL, 0, 1);
    check(ascii85, (const unsigned char *)"y~>", 3, NULL, 0, 0);
    check(ascii85, (const unsigned char *)"<~z~>", 5, NULL, 0, 0);
    unsigned char run[128];
    memset(run, 'X', sizeof(run));
    const char *rle = "<< /Filter /RunLengthDecode >>";
    check(rle, (const unsigned char *)"\x81X\x80", 3, run, 128, 1);
    check(rle, (const unsigned char *)"\xffX\x80", 3, run, 2, 1);
    check(rle, (const unsigned char *)"\x80", 1, NULL, 0, 1);
    unsigned char literal[130];
    literal[0] = 127;
    memset(literal + 1, 'X', 128);
    literal[129] = 128;
    check(rle, literal, sizeof(literal), run, sizeof(run), 1);
    check("<< /Filter /AHx >>", (const unsigned char *)"1>", 2, (const unsigned char *)"\x10", 1,
          1);
    check("<< /Filter [/AHx /RL] >>", (const unsigned char *)"ff5880>", 7, run, 2, 1);
    size_t length = 2 * (64u * 1024u * 1024u / 128u + 1u) + 1;
    unsigned char *bomb = (unsigned char *)malloc(length);
    if (!bomb)
        return 1;
    for (size_t i = 0; i < length - 1; i += 2) {
        bomb[i] = 129;
        bomb[i + 1] = 'X';
    }
    bomb[length - 1] = 128;
    check(rle, bomb, length, NULL, 0, 0);
    free(bomb);
    printf("PDF byte filters: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
