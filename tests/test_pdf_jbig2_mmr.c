#include "fault_alloc.h"
#include "formats/pdf/images/jbig2/internal.h"
#include <assert.h>
#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/compression/ccitt.c"
#include "../src/formats/pdf/images/jbig2/bitmap.c"
#include "../src/formats/pdf/images/jbig2/memory.c"
#include "../src/formats/pdf/images/jbig2/mmr.c"
#undef malloc
#undef calloc
#undef realloc
#undef free

static size_t encode(const char *bits, unsigned char output[128]) {
    memset(output, 0, 128);
    size_t length = strlen(bits);
    assert(length <= 1024);
    for (size_t i = 0; i < length; i++)
        output[i / 8] |= (unsigned char)((bits[i] - '0') << (7 - i % 8));
    return (length + 7) / 8;
}

int main(void) {
    unsigned checks = 0;
    unsigned char data[128];
    size_t length = encode("11"
                           "000000000001000000000001",
                           data);
    cupidimage_ccitt_options fax = {-1, 8, 2, 0, 0, 1, 1, 0};
    for (size_t budget = 0; budget < 256; budget++) {
        size_t work = budget, used = 0, bits = 99;
        unsigned char *output = NULL;
        int ok =
            cupidimage_ccitt_decode_bounded(data, length, &fax, &output, &used, 2, &work, 1, &bits);
        assert(work <= budget);
        if (ok) {
            assert(used == 2 && !output[0] && !output[1] && bits == 26);
            fault_free(output);
        } else
            assert(!output && !used && !bits);
        assert(!live_count);
        checks++;
    }
    size_t total = 0;
    for (size_t failure = 0; failure <= total + 1; failure++) {
        fail_at = failure;
        allocation_count = 0;
        size_t work = 100000, consumed = 0;
        pdf_jb2_context context;
        pdf_jb2_bitmap *bitmap = NULL;
        assert(cupidimage_pdf_jb2_context_init(&context, &work, 100000));
        int ok = cupidimage_pdf_jb2_mmr(&context, data, length, 8, 2, 1, &consumed, &bitmap);
        if (!failure)
            total = allocation_count;
        assert(ok == (!failure || failure > total));
        if (ok)
            assert(bitmap && consumed == 4 && !bitmap->data[0] && !bitmap->data[1]);
        else
            assert(!bitmap && !consumed && context.failed);
        cupidimage_pdf_jb2_clear(&context);
        assert(!live_count);
        checks++;
    }
    fail_at = 0;
    for (unsigned which = 0; which < 8; which++) {
        size_t work = 100000, consumed = 0;
        pdf_jb2_context context;
        pdf_jb2_bitmap *bitmap = NULL;
        assert(cupidimage_pdf_jb2_context_init(&context, &work, 100000));
        int end = 1;
        unsigned width = 8, height = 2;
        const char *text = "11"
                           "000000000001000000000001";
        int valid = 1;
        if (which == 0) {
            text = "11";
            end = 0;
        }
        if (which == 1) {
            text = "11";
            valid = 0;
        }
        if (which == 2) {
            text = "1"
                   "000000000001000000000001";
            valid = 0;
        }
        if (which == 3) {
            text = "111"
                   "000000000001000000000001";
            valid = 0;
        }
        if (which == 4) {
            text = "000000000001"
                   "11"
                   "000000000001000000000001";
            valid = 0;
        }
        if (which == 5) {
            text = "0000001";
            valid = 0;
        }
        if (which == 6) {
            text = "000000000001000000000001";
            width = height = 0;
        }
        if (which == 7) {
            text = "";
            width = height = 0;
            end = 0;
        }
        length = encode(text, data);
        int ok =
            cupidimage_pdf_jb2_mmr(&context, data, length, width, height, end, &consumed, &bitmap);
        assert(ok == valid);
        if (ok)
            assert(bitmap && consumed == length);
        else
            assert(!bitmap && !consumed);
        cupidimage_pdf_jb2_clear(&context);
        assert(!live_count);
        checks++;
    }
    puts("JBIG2 MMR: strict prefixes, termination, output/work caps and ownership passed");
    printf("JBIG2 MMR: %u checks\n", checks);
    return 0;
}
