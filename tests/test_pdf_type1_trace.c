#include "formats/pdf/fonts/type1.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            failures++;                                                                            \
            fprintf(stderr, "Type 1 trace failure at %u\n", __LINE__);                             \
        }                                                                                          \
    } while (0)

int main(void) {
    const unsigned char code[] = {139, 248, 236, 13, 149, 159, 1, 139, 139, 21, 239, 139, 5, 14};
    pdf_type1_glyph glyph = {0};
    glyph.code = (pdf_cff_slice){code, sizeof(code)};
    pdf_type1_font font = {0};
    font.glyphs = &glyph;
    font.glyph_count = 1;
    for (size_t budget = 0; budget < 30; budget++) {
        size_t work = budget;
        pdf_cff_outline out;
        pdf_type1_hint_trace trace;
        int ok = cupidimage_pdf_type1_outline_budget(&font, 0, &out, &trace, &work);
        CHECK(ok == (budget >= 13));
        CHECK(work == (budget >= 13 ? budget - 13 : 0));
        if (ok) {
            CHECK(out.width == 600 && out.count == 3 && trace.count == 1);
            CHECK(trace.events[0].values[0] == 10 && trace.events[0].values[1] == 20);
        } else
            CHECK(!out.segments && !out.count && !trace.events && !trace.count);
        cupidimage_pdf_type2_free(&out);
        cupidimage_pdf_type1_trace_free(&trace);
    }
    size_t work = 26;
    for (unsigned i = 0; i < 3; i++) {
        pdf_cff_outline out;
        CHECK(cupidimage_pdf_type1_outline_budget(&font, 0, &out, NULL, &work) == (i < 2));
        cupidimage_pdf_type2_free(&out);
    }
    CHECK(work == 0);
    work = SIZE_MAX;
    pdf_cff_outline out;
    pdf_type1_hint_trace trace;
    CHECK(cupidimage_pdf_type1_outline_budget(&font, 0, &out, &trace, &work));
    CHECK(work == SIZE_MAX - 13);
    cupidimage_pdf_type2_free(&out);
    cupidimage_pdf_type1_trace_free(&trace);
    CHECK(!cupidimage_pdf_type1_outline_budget(&font, 1, &out, &trace, &work));
    CHECK(!out.segments && !trace.events && work == SIZE_MAX - 13);
    unsigned char sub[8193], large[64] = {139, 248, 236, 13};
    for (unsigned i = 0; i < 4096; i++) {
        sub[2 * i] = 12;
        sub[2 * i + 1] = 0;
    }
    sub[8192] = 11;
    unsigned n = 4;
    for (unsigned i = 0; i < 17; i++) {
        large[n++] = 139;
        large[n++] = 10;
    }
    large[n++] = 14;
    pdf_cff_slice subr = {sub, sizeof(sub)};
    font.subrs = &subr;
    font.subr_count = 1;
    glyph.code = (pdf_cff_slice){large, n};
    work = 70000;
    CHECK(!cupidimage_pdf_type1_outline_budget(&font, 0, &out, &trace, &work));
    CHECK(work == 70000 - 65536 && !out.segments && !trace.events);
    const unsigned char seac[] = {159, 248, 236, 13, 139, 149, 169, 204, 247, 86, 12, 6};
    glyph.code = (pdf_cff_slice){seac, sizeof(seac)};
    CHECK(cupidimage_pdf_type1_outline_hints(&font, 0, &out, &trace));
    CHECK(out.composite && out.base == 65 && out.accent == 194 && out.accent_x == 30 &&
          out.accent_y == 30 && out.width == 600 && !trace.count);
    cupidimage_pdf_type2_free(&out);
    cupidimage_pdf_type1_trace_free(&trace);
    printf("Type 1 trace budgets and ownership: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
