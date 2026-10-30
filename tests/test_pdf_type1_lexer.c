#include "formats/pdf/fonts/type1/lexer.h"
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;

static void check(const char *input, unsigned type, double number, const char *text) {
    size_t length = strlen(input), pos = 0;
    pdf_type1_token token =
        cupidimage_pdf_type1_lex((pdf_cff_slice){(const unsigned char *)input, length}, &pos, 0);
    checks++;
    if (token.type != type || (type != PDF_T1_ERROR && pos != length) ||
        (type == PDF_T1_NUMBER && token.number != number) || (text && strcmp(text, token.text))) {
        fprintf(stderr, "FAIL Type 1 lexer %u: %s => type %u, %.17g, pos %zu/%zu\n", checks, input,
                token.type, token.number, pos, length);
        failures++;
    }
}

int main(void) {
    const char *numbers[] = {"0",          "-0",          "+17",         "-.002",  "34.5",
                             "123.6e10",   "1.0E-5",      "1E6",         "-1.",    ".5",
                             "2147483648", "16#FFFFFFFF", "16#80000000", "8#1777", "2#1000",
                             "36#Z",       "16#fF",       "00016#01"};
    const double values[] = {0,   0,  17,           -.002, 34.5,          123.6e10, 1e-5, 1e6,
                             -1,  .5, 2147483648.0, -1,    -2147483648.0, 1023,     8,    35,
                             255, 1};
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++)
        check(numbers[i], PDF_T1_NUMBER, values[i], NULL);
    const char *names[] = {".",     "+",    "-",   "1e",       "1e+",    "1e-",    "1e2x",
                           "1.2.3", "nan",  "inf", "infinity", "0x1p2",  "0x12",   "1#0",
                           "37#Z",  "16#G", "16#", "#12",      "-16#FF", "16#-FF", "16#100000000Q",
                           "23A"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        check(names[i], PDF_T1_WORD, 0, names[i]);
    check("1e9999", PDF_T1_ERROR, 0, NULL);
    check("16#100000000", PDF_T1_ERROR, 0, NULL);
    check("2#111111111111111111111111111111111", PDF_T1_ERROR, 0, NULL);
    check("/", PDF_T1_NAME, 0, "");
    check("//", PDF_T1_IMMEDIATE, 0, "");
    check("//lenIV", PDF_T1_IMMEDIATE, 0, "lenIV");
    check("/A#20B", PDF_T1_NAME, 0, "A#20B");
    check("%comment\f123", PDF_T1_NUMBER, 123, NULL);
    check("<<", PDF_T1_OTHER, 0, "<<");
    check(">>", PDF_T1_OTHER, 0, ">>");
    check(">", PDF_T1_ERROR, 0, NULL);
    check(")", PDF_T1_ERROR, 0, NULL);
    check("<>", PDF_T1_OTHER, 0, NULL);
    check("<0 1 A b C>", PDF_T1_OTHER, 0, NULL);
    check("<GG", PDF_T1_ERROR, 0, NULL);
    check("<12", PDF_T1_ERROR, 0, NULL);
    check("<~z~>", PDF_T1_OTHER, 0, NULL);
    check("<~!!>!!~>", PDF_T1_OTHER, 0, NULL);
    check("<~s8W-!~>", PDF_T1_OTHER, 0, NULL);
    check("<~uuuuu", PDF_T1_ERROR, 0, NULL);
    check("<~!z", PDF_T1_ERROR, 0, NULL);
    check("<~!~>", PDF_T1_ERROR, 0, NULL);
    check("<~!!", PDF_T1_ERROR, 0, NULL);
    check("{<~!!>!!~> /lenIV 4 def}", PDF_T1_PROC, 0, NULL);
    check("{<< /A 1 >> {2} (a\\)b)}", PDF_T1_PROC, 0, NULL);
    check("{ % comment\f}", PDF_T1_PROC, 0, NULL);
    char input[200];
    for (unsigned base = 2; base <= 36; base++) {
        for (unsigned value = 0; value < 256; value++) {
            char digits[40];
            unsigned n = 0, v = value;
            do {
                digits[n++] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[v % base];
                v /= base;
            } while (v);
            unsigned used = (unsigned)snprintf(input, sizeof(input), "%u#", base);
            while (n)
                input[used++] = digits[--n];
            input[used] = 0;
            check(input, PDF_T1_NUMBER, value, NULL);
        }
    }
    for (unsigned i = 1; i < 10000; i++) {
        snprintf(input, sizeof(input), "%u.125", i);
        check(input, PDF_T1_NUMBER, i + .125, NULL);
    }
    for (unsigned i = 0; i < 128; i++)
        input[i] = 'a';
    input[128] = 0;
    check(input, PDF_T1_ERROR, 0, NULL);
    input[127] = 0;
    check(input, PDF_T1_WORD, 0, input);
    size_t pos = 2;
    pdf_type1_token token = cupidimage_pdf_type1_lex((pdf_cff_slice){NULL, 0}, &pos, 0);
    checks++;
    failures += token.type != PDF_T1_ERROR || pos != 2;
    const char *locales[] = {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "fr_FR"};
    unsigned localized = 0;
    for (unsigned i = 0; i < sizeof(locales) / sizeof(locales[0]); i++) {
        if (!setlocale(LC_NUMERIC, locales[i]))
            continue;
        for (unsigned j = 0; j < sizeof(values) / sizeof(values[0]); j++)
            check(numbers[j], PDF_T1_NUMBER, values[j], NULL);
        check("1,25", PDF_T1_WORD, 0, "1,25");
        localized++;
        break;
    }
    setlocale(LC_NUMERIC, "C");
    printf("Type 1 locale checks: %s\n",
           localized ? "comma-decimal locale passed" : "locale unavailable; skipped");
    uint32_t seed = 0x1251;
    unsigned char bytes[128];
    for (unsigned sample = 0; sample < 10000; sample++) {
        for (unsigned i = 0; i < sizeof(bytes); i++) {
            seed = seed * 1664525u + 1013904223u;
            bytes[i] = (unsigned char)(seed >> 24);
        }
        pos = 0;
        while (pos < sizeof(bytes)) {
            size_t before = pos;
            token = cupidimage_pdf_type1_lex((pdf_cff_slice){bytes, sizeof(bytes)}, &pos, 0);
            checks++;
            if (pos <= before || pos > sizeof(bytes)) {
                failures++;
                break;
            }
            if (token.type == PDF_T1_ERROR || token.type == PDF_T1_EOF)
                break;
        }
    }
    printf("Type 1 lexical syntax: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
