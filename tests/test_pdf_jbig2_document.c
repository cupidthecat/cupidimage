#include "fault_alloc.h"
#include "formats/pdf/images/jbig2/decode.h"
#include <assert.h>
#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/formats/pdf/images/jbig2/arithmetic.c"
#include "../src/formats/pdf/images/jbig2/bitmap.c"
#include "../src/formats/pdf/images/jbig2/bits.c"
#include "../src/formats/pdf/images/jbig2/coding.c"
#include "../src/formats/pdf/images/jbig2/decode.c"
#include "../src/formats/pdf/images/jbig2/dictionary.c"
#include "../src/formats/pdf/images/jbig2/document_dictionary.c"
#include "../src/formats/pdf/images/jbig2/document_regions.c"
#include "../src/formats/pdf/images/jbig2/document_text.c"
#include "../src/formats/pdf/images/jbig2/document_values.c"
#include "../src/formats/pdf/images/jbig2/generic.c"
#include "../src/formats/pdf/images/jbig2/halftone.c"
#include "../src/formats/pdf/images/jbig2/huffman.c"
#include "../src/formats/pdf/images/jbig2/huffman_table.c"
#include "../src/formats/pdf/images/jbig2/memory.c"
#include "../src/formats/pdf/images/jbig2/mmr.c"
#include "../src/formats/pdf/images/jbig2/patterns.c"
#include "../src/formats/pdf/images/jbig2/refinement.c"
#include "../src/formats/pdf/images/jbig2/segments.c"
#include "../src/formats/pdf/images/jbig2/symbol_codes.c"
#include "../src/formats/pdf/images/jbig2/text.c"
#undef malloc
#undef calloc
#undef realloc
#undef free
#include "fixtures/pdf-jbig2.inc"

static unsigned checks;

static int decode(const unsigned char *data, size_t length, const unsigned char *globals,
                  size_t globals_length, unsigned fixture, size_t budget, int exact) {
    unsigned char *output = NULL;
    size_t bytes = 0, work = budget;
    int ok = cupidimage_pdf_jbig2_decode(data, length, globals, globals_length,
                                         jb2_fixtures[fixture].width, jb2_fixtures[fixture].height,
                                         &work, &output, &bytes);
    if (ok) {
        size_t expected = ((jb2_fixtures[fixture].width + 7) / 8) * jb2_fixtures[fixture].height;
        assert(output && bytes == expected);
        if (exact)
            assert(!memcmp(output, jb2_fixtures[fixture].samples, bytes));
    } else
        assert(!output && !bytes);
    fault_free(output);
    assert(!live_count && work <= budget);
    checks++;
    return ok;
}

int main(void) {
    for (unsigned fixture = 0; fixture < 4; fixture++) {
        const unsigned char *data = jb2_fixtures[fixture].data;
        const unsigned char *globals = jb2_fixtures[fixture].globals;
        size_t length = jb2_fixtures[fixture].length,
               globals_length = jb2_fixtures[fixture].globals_length;
        size_t allocations = 0;
        for (size_t failure = 0; failure <= allocations + 1; failure++) {
            allocation_count = 0;
            fail_at = failure;
            int ok = decode(data, length, globals, globals_length, fixture, 1000000, 1);
            if (!failure)
                allocations = allocation_count;
            assert(ok == (!failure || failure > allocations));
        }
        fail_at = 0;
        for (size_t n = 0; n < length; n++)
            decode(data, n, globals, globals_length, fixture, 1000000, 0);
        for (size_t n = 0; n < globals_length; n++)
            decode(data, length, globals, n, fixture, 1000000, 0);
        for (size_t work = 0; work < 100000; work += 137)
            decode(data, length, globals, globals_length, fixture, work, 1);
        unsigned random = fixture + 14492;
        unsigned char mutated[4096];
        assert(length <= sizeof(mutated));
        for (unsigned n = 0; n < 2048; n++) {
            memcpy(mutated, data, length);
            random = random * 1664525u + 1013904223u;
            size_t index = random % length;
            random = random * 1664525u + 1013904223u;
            mutated[index] ^= (unsigned char)(1u << (random % 8));
            decode(mutated, length, globals, globals_length, fixture, 200000, 0);
        }
    }
    (void)fault_realloc;
    printf("JBIG2 documents: %u allocation, budget, truncation and bounded mutation checks\n",
           checks);
    return 0;
}
