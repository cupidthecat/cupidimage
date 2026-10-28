#include "formats/pdf/pdf_content_syntax.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*reader)(const unsigned char *, size_t, size_t *, char **, size_t *);
static unsigned checks, failures;

static void check(reader read, const char *input, size_t size, const char *expected, size_t length,
                  size_t consumed, int valid) {
    size_t pos = 0, bytes = 0;
    char *output = NULL;
    int ok = read((const unsigned char *)input, size, &pos, &output, &bytes);
    checks++;
    if (ok != valid ||
        (ok && (bytes != length || pos != consumed ||
                (length && memcmp(output, expected, length)) || output[bytes])) ||
        (!ok && pos)) {
        fprintf(stderr, "FAIL content syntax %u: status %d, bytes %zu, consumed %zu\n", checks, ok,
                bytes, pos);
        failures++;
    }
    free(output);
}

#define READ(fn, input, want)                                                                      \
    check(cupidimage_pdf_content_##fn, input, sizeof(input) - 1, want, sizeof(want) - 1,           \
          sizeof(input) - 1, 1)
#define BAD(fn, input) check(cupidimage_pdf_content_##fn, input, sizeof(input) - 1, NULL, 0, 0, 0)

int main(void) {
    READ(literal, "()", "");
    READ(literal, "(a\rb\r\nc\nd)", "a\nb\nc\nd");
    READ(literal, "(a\\\rb\\\r\nc\\\nd)", "abcd");
    READ(literal, "(\\n\\r\\t\\b\\f\\(\\)\\\\)", "\n\r\t\b\f()\\");
    READ(literal, "(\\a\\8\\9)", "a89");
    READ(literal, "(a(b(c)d)e)", "a(b(c)d)e");
    READ(literal, "(\\0\\00\\000\\777\\377\\400\\1x)", "\0\0\0\377\377\0\1x");
    READ(literal, "(a\0b)", "a\0b");
    BAD(literal, "(unclosed");
    BAD(literal, "(\\");
    BAD(literal, "(nested()");
    READ(hex, "<>", "");
    READ(hex, "<4 1\0\t\n\f\r42>", "AB");
    READ(hex, "<4>", "\100");
    READ(hex, "<410>", "A\0");
    BAD(hex, "<41");
    BAD(hex, "<4");
    BAD(hex, "<\v41>");
    BAD(hex, "<4G>");
    BAD(hex, "<<>>");
    READ(name, "/", "");
    READ(name, "/F#30", "F0");
    READ(name, "/A#2f#23#20#28#5d", "A/# (]");
    BAD(name, "/#00");
    BAD(name, "/#");
    BAD(name, "/#1");
    BAD(name, "/#GG");
    READ(array, "[(a]b) <5D> << /Key [1 2] >> % ] ignored\n 3]",
         "(a]b) <5D> << /Key [1 2] >> % ] ignored\n 3");
    READ(dictionary, "<< /Key [(a>>) <3E3E>] >>", "<< /Key [(a>>) <3E3E>] >>");
    BAD(array, "[<41]");
    BAD(array, "[<< /A 1 ] >>");
    BAD(array, "[(a]");
    BAD(dictionary, "<< /A [1 >>");
    BAD(dictionary, "<< /A <12 >>");
    BAD(dictionary, "<< /A /#00 >>");
    for (unsigned byte = 0; byte < 256; byte++) {
        char encoded[8], expected = (char)byte;
        snprintf(encoded, sizeof(encoded), "<%02X>", byte);
        check(cupidimage_pdf_content_hex, encoded, 4, &expected, 1, 4, 1);
        snprintf(encoded, sizeof(encoded), "(\\%03o)", byte);
        check(cupidimage_pdf_content_literal, encoded, 6, &expected, 1, 6, 1);
        snprintf(encoded, sizeof(encoded), "/#%02X", byte);
        check(cupidimage_pdf_content_name, encoded, 4, &expected, 1, 4, byte != 0);
    }
    const char *numbers[] = {"0", "+1", "-1.25", ".5", "1.", "1e3", "1..2", "--1", ".", "+", "12A"};
    double wanted[] = {0, 1, -1.25, .5, 1};
    for (unsigned i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++) {
        size_t pos = 0;
        double value = 0;
        int ok = cupidimage_pdf_content_number((const unsigned char *)numbers[i],
                                               strlen(numbers[i]), &pos, &value);
        checks++;
        if (ok != (i < 5) || (ok && (pos != strlen(numbers[i]) || value != wanted[i])) ||
            (!ok && pos))
            failures++;
    }
    char nesting[132];
    memset(nesting, '(', 65);
    memset(nesting + 65, ')', 65);
    check(cupidimage_pdf_content_literal, nesting, 130, NULL, 0, 0, 0);
    memset(nesting, '[', 65);
    memset(nesting + 65, ']', 65);
    check(cupidimage_pdf_content_array, nesting, 130, NULL, 0, 0, 0);
    char long_number[80];
    memset(long_number, '1', sizeof(long_number));
    size_t pos = 0;
    double value;
    checks++;
    if (cupidimage_pdf_content_number((unsigned char *)long_number, sizeof(long_number), &pos,
                                      &value) ||
        pos)
        failures++;
    size_t limit = 1024u * 1024u;
    char *large = malloc(limit + 3);
    if (!large)
        return 1;
    large[0] = '(';
    memset(large + 1, 'A', limit);
    large[limit + 1] = ')';
    check(cupidimage_pdf_content_literal, large, limit + 2, large + 1, limit, limit + 2, 1);
    large[limit + 1] = 'A';
    large[limit + 2] = ')';
    check(cupidimage_pdf_content_literal, large, limit + 3, NULL, 0, 0, 0);
    large[0] = '[';
    memset(large + 1, ' ', limit - 2);
    large[limit - 1] = ']';
    check(cupidimage_pdf_content_array, large, limit, large + 1, limit - 2, limit, 1);
    large[limit - 1] = ' ';
    large[limit] = ']';
    check(cupidimage_pdf_content_array, large, limit + 1, NULL, 0, 0, 0);
    free(large);
    printf("PDF content syntax: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
