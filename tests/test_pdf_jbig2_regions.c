#include "fault_alloc.h"
#include "formats/pdf/images/jbig2/internal.h"
#include <assert.h>
#define malloc fault_malloc
#define calloc fault_calloc
#define free fault_free
#include "../src/formats/pdf/images/jbig2/memory.c"
#undef malloc
#undef calloc
#undef free

/* Owned 17x9 all-black regions, encoded by the independent test MQ encoder. */
static const unsigned char encoded[6][5] = {
    {0x04, 0x53, 0xff, 0xac}, {0x08, 0xd3, 0xff, 0xac},       {0x11, 0xff, 0x7f, 0xff, 0xac},
    {0x04, 0x53, 0xff, 0xac}, {0xfc, 0x13, 0xcb, 0xff, 0xac}, {0xfc, 0x13, 0xcb, 0xff, 0xac}};
static const unsigned lengths[6] = {4, 4, 5, 4, 5, 5};
static unsigned checks;

static int run(unsigned mode, size_t budget, unsigned invalid, size_t length) {
    size_t work = budget;
    pdf_jb2_context context;
    assert(cupidimage_pdf_jb2_context_init(&context, &work, PDF_JB2_MEMORY_LIMIT));
    pdf_jb2_arithmetic decoder;
    pdf_jb2_bitmap *output = NULL;
    size_t count = mode < 4 ? cupidimage_pdf_jb2_generic_contexts(mode) : mode == 4 ? 8192 : 1024;
    unsigned char *states = cupidimage_pdf_jb2_alloc(&context, count, 1);
    int ok =
        states && cupidimage_pdf_jb2_arithmetic_init(&decoder, &context, encoded[mode], length);
    if (mode < 4) {
        pdf_jb2_generic p;
        assert(cupidimage_pdf_jb2_generic_defaults(&p, mode, 0));
        p.width = 17;
        p.height = 9;
        p.typical = 1;
        if (invalid == 1)
            p.at_x[0] = p.at_y[0] = 0;
        if (invalid == 2)
            p.at_x[0] = 128;
        if (invalid == 3)
            p.template_id = 4;
        if (invalid == 4)
            p.extended = 2;
        if (invalid == 5)
            p.typical = 2;
        if (invalid == 6)
            count--;
        if (invalid == 7)
            p.at_count++;
        ok =
            ok && cupidimage_pdf_jb2_generic_decode(&context, &p, &decoder, states, count, &output);
    } else {
        pdf_jb2_bitmap *reference = cupidimage_pdf_jb2_bitmap_new(&context, 17, 9, 1);
        pdf_jb2_refinement p = {17, 9, mode - 4, 1, {-1, -1}, {-1, -1}, 0, 0, reference};
        if (invalid == 1) {
            p.template_id = 0;
            p.at_x[0] = p.at_y[0] = 0;
        }
        if (invalid == 2) {
            p.template_id = 0;
            p.at_x[1] = 128;
        }
        if (invalid == 3)
            p.template_id = 2;
        if (invalid == 4)
            p.reference = NULL;
        if (invalid == 5)
            p.typical = 2;
        if (invalid == 6)
            count--;
        if (invalid == 7) {
            p.template_id = 0;
            p.at_y[0] = 1;
        }
        ok = ok && reference &&
             cupidimage_pdf_jb2_refinement_decode(&context, &p, &decoder, states, count, &output);
    }
    if (ok) {
        assert(output && !context.failed);
        for (unsigned y = 0; y < 9; y++) {
            assert(output->data[y * 3] == 255 && output->data[y * 3 + 1] == 255);
            assert(output->data[y * 3 + 2] == 128);
        }
    } else
        assert(context.failed && !output);
    assert(work <= budget);
    cupidimage_pdf_jb2_clear(&context);
    assert(!live_count);
    checks++;
    return ok;
}

int main(void) {
    for (unsigned mode = 0; mode < 6; mode++) {
        size_t total = 0;
        for (size_t failure = 0; failure <= total + 1; failure++) {
            fail_at = failure;
            allocation_count = 0;
            int ok = run(mode, 1000000, 0, lengths[mode]);
            if (!failure)
                total = allocation_count;
            assert(ok == (!failure || failure > total));
        }
        fail_at = 0;
        for (unsigned invalid = 1; invalid <= 7; invalid++)
            assert(!run(mode, 1000000, invalid, lengths[mode]));
        for (size_t length = 0; length < lengths[mode] - 2; length++)
            assert(!run(mode, 1000000, 0, length));
        for (size_t work = 0; work < 12000; work += 7)
            run(mode, work, 0, lengths[mode]);
    }
    (void)fault_realloc;
    printf("JBIG2 regions: %u allocation, budget, template and truncation checks\n", checks);
    return 0;
}
