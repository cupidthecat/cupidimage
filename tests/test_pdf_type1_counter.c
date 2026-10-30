#include "fault_alloc.h"
#include <math.h>
#include <stdint.h>
#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/formats/pdf/fonts/type1/counter.c"
#include "../src/formats/pdf/fonts/type1/trace.c"
#include "../src/formats/pdf/fonts/type1_charstring.c"
#include "../src/formats/pdf/fonts/type2.c"

static unsigned checks, failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            failures++;                                                                            \
            fprintf(stderr, "Type 1 counter failure at %u\n", __LINE__);                           \
        }                                                                                          \
    } while (0)

static const double values[] = {1, 0, 20, 100, -20, 1, 10, 30, 200, -30};

static void reset(void) {
    CHECK(!live_count);
    allocation_count = fail_at = 0;
}

static int program(pdf_type1_counter *counter, pdf_type1_hint_trace *trace, size_t *work) {
    /* The trailing data is transferred first; the final call supplies the prefix. */
    return cupidimage_pdf_type1_counter_push(counter, values + 3, 7, 0, trace, 0, work) &&
           cupidimage_pdf_type1_counter_push(counter, values, 3, 1, trace, 0, work);
}

int main(void) {
    (void)fault_calloc;
    pdf_type1_counter counter = {0};
    pdf_type1_hint_trace trace = {0};
    size_t work = 100000;
    CHECK(program(&counter, &trace, &work));
    CHECK(counter.finished && !counter.active && trace.count == 4 &&
          trace.diagnostics == PDF_T1_HINT_COUNTER_DATA && work == 99980);
    CHECK(trace.events[0].values[0] == 0 && trace.events[0].values[1] == 20 &&
          trace.events[1].values[0] == 100 && trace.events[1].values[1] == -20 &&
          trace.events[2].vertical == 1 && trace.events[3].values[3] == 1);
    size_t allocations = allocation_count;
    CHECK(!cupidimage_pdf_type1_counter_push(&counter, values, 1, 0, &trace, 0, &work));
    cupidimage_pdf_type1_counter_free(&counter);
    cupidimage_pdf_type1_trace_free(&trace);
    reset();
    for (size_t failure = 1; failure <= allocations; failure++) {
        fail_at = failure;
        work = 100000;
        CHECK(!program(&counter, &trace, &work));
        cupidimage_pdf_type1_counter_free(&counter);
        cupidimage_pdf_type1_trace_free(&trace);
        reset();
    }
    for (size_t budget = 0; budget <= 24; budget++) {
        work = budget;
        int ok = program(&counter, &trace, &work);
        CHECK(ok == (budget >= 20));
        CHECK(work == (budget >= 20 ? budget - 20 : 0));
        cupidimage_pdf_type1_counter_free(&counter);
        cupidimage_pdf_type1_trace_free(&trace);
        reset();
    }
    for (unsigned position = 0; position < 10; position++)
        for (unsigned bad = 0; bad < 3; bad++) {
            double data[10];
            memcpy(data, values, sizeof(data));
            data[position] = bad == 0 ? NAN : bad == 1 ? INFINITY : 1e31;
            work = 100000;
            CHECK(!cupidimage_pdf_type1_counter_push(&counter, data, 10, 1, &trace, 0, &work));
            cupidimage_pdf_type1_counter_free(&counter);
            cupidimage_pdf_type1_trace_free(&trace);
            reset();
        }
    double zero[22] = {0};
    work = SIZE_MAX;
    for (unsigned i = 0; i < 2978; i++)
        CHECK(cupidimage_pdf_type1_counter_push(&counter, zero, 22, 0, NULL, 0, &work));
    CHECK(counter.count == 65516);
    CHECK(cupidimage_pdf_type1_counter_push(&counter, zero, 20, 0, NULL, 0, &work));
    CHECK(counter.count == 65536);
    CHECK(!cupidimage_pdf_type1_counter_push(&counter, zero, 1, 0, NULL, 0, &work));
    CHECK(!cupidimage_pdf_type1_counter_push(&counter, zero, 0, 1, NULL, 0, &work));
    cupidimage_pdf_type1_counter_free(&counter);
    reset();

    uint32_t seed = 0x425143u;
    for (unsigned attempt = 0; attempt < 2000; attempt++) {
        double data[22];
        for (unsigned i = 0; i < 22; i++) {
            seed = seed * 1664525u + 1013904223u;
            data[i] = ((int)((seed >> 16) % 33) - 16) / 2.0;
        }
        work = 200;
        int ok = cupidimage_pdf_type1_counter_push(&counter, data, seed % 23, 1, &trace, 0, &work);
        CHECK(work <= 200 && (!ok || (counter.finished && !counter.active)));
        cupidimage_pdf_type1_counter_free(&counter);
        cupidimage_pdf_type1_trace_free(&trace);
        reset();
    }

    /* A full charstring owns and discards both counter storage and partial traces. */
    const unsigned char code[] = {139, 248, 236, 13,  140, 139, 159, 239, 119, 139, 145,
                                  152, 12,  16,  139, 139, 21,  239, 139, 5,   14};
    pdf_type1_glyph glyph = {0};
    glyph.code = (pdf_cff_slice){code, sizeof(code)};
    pdf_type1_font font = {0};
    font.glyphs = &glyph;
    font.glyph_count = 1;
    pdf_cff_outline outline;
    work = 100000;
    CHECK(cupidimage_pdf_type1_outline_budget(&font, 0, &outline, &trace, &work));
    CHECK(outline.count == 3 && outline.width == 600 && trace.count == 2);
    allocations = allocation_count;
    cupidimage_pdf_type2_free(&outline);
    cupidimage_pdf_type1_trace_free(&trace);
    reset();
    for (size_t failure = 1; failure <= allocations; failure++) {
        fail_at = failure;
        work = 100000;
        CHECK(!cupidimage_pdf_type1_outline_budget(&font, 0, &outline, &trace, &work));
        CHECK(!outline.count && !outline.segments && !trace.count && !trace.events);
        reset();
    }
    printf("Type 1 counter budgets and ownership: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
