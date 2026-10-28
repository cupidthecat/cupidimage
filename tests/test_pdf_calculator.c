#include "formats/pdf/pdf_calculator.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;

static void check(const char *source, int valid, double expected) {
    pdf_calculator_program *p =
        cupidimage_pdf_calculator_compile((const unsigned char *)source, strlen(source));
    double result = 0;
    size_t budget = 100000;
    int ok = p && cupidimage_pdf_calculator_evaluate(p, NULL, 0, &result, 1, &budget);
    checks++;
    if (ok != valid || (ok && fabs(result - expected) > 1e-10 * fmax(1, fabs(expected)))) {
        fprintf(stderr, "calculator: %s: ok=%d result=%.17g expected=%.17g\n", source, ok, result,
                expected);
        failures++;
    }
    free(p);
}

int main(void) {
    static const struct {
        const char *source;
        double expected;
    } good[] = {{"{ -3 abs }", 3},
                {"{ 3 2 add }", 5},
                {"{ 1 1 atan }", 45},
                {"{ -1 0 atan }", 270},
                {"{ -1.4 ceiling }", -1},
                {"{ 60 cos }", .5},
                {"{ -1.9 cvi }", -1},
                {"{ 3 cvr }", 3},
                {"{ 3 2 div }", 1.5},
                {"{ 2 3 exp }", 8},
                {"{ -1.4 floor }", -2},
                {"{ -7 3 idiv }", -2},
                {"{ 1 ln }", 0},
                {"{ 100 log }", 2},
                {"{ -7 3 mod }", -1},
                {"{ 3 2 mul }", 6},
                {"{ 3 neg }", -3},
                {"{ -1.5 round }", -1},
                {"{ -.5 round }", 0},
                {"{ -1.6 round }", -2},
                {"{ 4503599627370497 round }", 4503599627370497.0},
                {"{ 1.5 round }", 2},
                {"{ 30 sin }", .5},
                {"{ 9 sqrt }", 3},
                {"{ 3 2 sub }", 1},
                {"{ -1.9 truncate }", -1},
                {"{ 6 3 and }", 2},
                {"{ 6 3 or }", 7},
                {"{ 6 3 xor }", 5},
                {"{ -1 1 bitshift }", -2},
                {"{ -1 -1 bitshift }", 2147483647},
                {"{ -1 32 bitshift }", 0},
                {"{ 1 -2147483648 bitshift }", 0},
                {"{ 0 not }", -1},
                {"{ true not { 1 } { 0 } ifelse }", 0},
                {"{ true false and { 1 } { 0 } ifelse }", 0},
                {"{ true false or { 1 } { 0 } ifelse }", 1},
                {"{ true true xor { 1 } { 0 } ifelse }", 0},
                {"{ 1 1.0 eq { 1 } { 0 } ifelse }", 1},
                {"{ true 1 ne { 1 } { 0 } ifelse }", 1},
                {"{ 2 2 ge { 1 } { 0 } ifelse }", 1},
                {"{ 3 2 gt { 1 } { 0 } ifelse }", 1},
                {"{ 2 2 le { 1 } { 0 } ifelse }", 1},
                {"{ 1 2 lt { 1 } { 0 } ifelse }", 1},
                {"{ 4 true { 2 mul } if }", 8},
                {"{ 4 false { pop } if }", 4},
                {"{ true { false { 1 } { 2 } ifelse } { 3 } ifelse }", 2},
                {"{ false { 1 0 div } { 7 } ifelse }", 7},
                {"{ 3 dup mul }", 9},
                {"{ 2 3 exch sub }", 1},
                {"{ 2 3 2 copy add add add }", 10},
                {"{ 2 0 copy }", 2},
                {"{ 2 3 1 index mul exch pop }", 6},
                {"{ 2 3 pop }", 2},
                {"{ 1 2 3 3 1 roll pop pop }", 3},
                {"{ 1 2 3 3 -1 roll pop pop }", 2},
                {"{ 7 0 123 roll }", 7},
                {"{ 2147483647 1 add }", 2147483648.0},
                {"{ -2147483648 neg }", 2147483648.0},
                {"{ -2147483648 abs }", 2147483648.0},
                {"{ -2147483648 -1 mod }", 0},
                {"{ 2147483648 }", 2147483648.0},
                {"{% comment\r\n +.5 -0.25 add % trailing\n} % end", .25},
                {"{ 1. ceiling 1 eq { 5 } { 0 } ifelse }", 5}};

    for (size_t i = 0; i < sizeof(good) / sizeof(*good); i++)
        check(good[i].source, 1, good[i].expected);
    static const char *bad[] = {"",
                                "1",
                                "{",
                                "{}",
                                "{1} extra",
                                "{1} {2}",
                                "{ 1e2 }",
                                "{16#FF}",
                                "{ . }",
                                "{ + }",
                                "{ 1..2 }",
                                "{ (1) }",
                                "{ [1] }",
                                "{ /x }",
                                "{ x }",
                                "{ {1} }",
                                "{ true {1} ifelse }",
                                "{ true {1} {2} if }",
                                "{ if }",
                                "{ 1 2 }",
                                "{ true }",
                                "{ pop }",
                                "{ 1 add }",
                                "{ exch }",
                                "{ dup }",
                                "{ true 1 add }",
                                "{ 1 true lt }",
                                "{ 1. not }",
                                "{ 1. 2 and }",
                                "{ true 1 or }",
                                "{ 1. 2 bitshift }",
                                "{ 1 2. bitshift }",
                                "{ 0 0 atan }",
                                "{ 1 0 div }",
                                "{ 1 0 idiv }",
                                "{ 1 0 mod }",
                                "{ 1. 2 idiv }",
                                "{ 1 2. mod }",
                                "{ -1 sqrt }",
                                "{ 0 ln }",
                                "{ -1 log }",
                                "{ -1 .5 exp }",
                                "{ -2147483648 -1 idiv }",
                                "{ 2147483648 cvi }",
                                "{ -2147483649 cvi }",
                                "{ 1 {2} if }",
                                "{ 1 2 copy }",
                                "{ -1 copy }",
                                "{ 1 0. copy }",
                                "{ 1 1 index }",
                                "{ 1 -1 index }",
                                "{ 1 2 1 roll }",
                                "{ 1 -1 1 roll }",
                                "{ 1 1. 1 roll }",
                                "{ 1 1 1. roll }",
                                "{ 2147483647 1 add not }",
                                "{ 1 cvr not }",
                                "{ 1. floor not }",
                                "{ false { unknown } if 1 }"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++)
        check(bad[i], 0, 0);
    char source[20000];
    size_t used = 0;
    used += (size_t)sprintf(source + used, "{");
    for (int i = 0; i < 256; i++)
        used += (size_t)sprintf(source + used, " 1");
    for (int i = 0; i < 255; i++)
        used += (size_t)sprintf(source + used, " pop");
    sprintf(source + used, " }");
    check(source, 1, 1);
    strcpy(source, "{ 1");
    for (int i = 0; i < 256; i++)
        strcat(source, " dup");
    strcat(source, " }");
    check(source, 0, 0);
    strcpy(source, "{ 1");
    for (int i = 0; i < 4096; i++)
        strcat(source, " pop");
    strcat(source, " }");
    check(source, 0, 0);
    strcpy(source, "{");
    for (int i = 0; i < 16; i++)
        strcat(source, " true {");
    strcat(source, " 1");
    for (int i = 0; i < 16; i++)
        strcat(source, " } if");
    strcat(source, " }");
    check(source, 0, 0);
    const char *tiny = "{ 1 dup add }";
    pdf_calculator_program *p =
        cupidimage_pdf_calculator_compile((const unsigned char *)tiny, strlen(tiny));
    size_t budget = 2;
    double out;
    checks++;
    if (!p || cupidimage_pdf_calculator_evaluate(p, NULL, 0, &out, 1, &budget))
        failures++;
    free(p);
    printf("PDF calculator: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
