#include "fault_alloc.h"
#include "formats/pdf/images/jbig2/segments.h"
#include <assert.h>
#define malloc fault_malloc
#define calloc fault_calloc
#define free fault_free
#include "../src/formats/pdf/images/jbig2/memory.c"
#undef malloc
#undef calloc
#undef free

static unsigned checks;

static void put(unsigned char *data, size_t *offset, uint32_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; i++)
        data[(*offset)++] = (unsigned char)(value >> ((bytes - i - 1) * 8));
}

static size_t header(unsigned char *data, uint32_t number, unsigned type, unsigned page,
                     const uint32_t *references, unsigned count, uint32_t length, unsigned wide) {
    size_t offset = 0;
    put(data, &offset, number, 4);
    put(data, &offset, type | (wide ? 64 : 0), 1);
    if (count <= 4)
        put(data, &offset, (count << 5) | ((1u << (count + 1)) - 1), 1);
    else {
        put(data, &offset, 0xe0000000u | count, 4);
        for (unsigned i = 0; i < (count + 8) / 8; i++) {
            unsigned used = count + 1 - i * 8;
            put(data, &offset, used >= 8 ? 255 : (1u << used) - 1, 1);
        }
    }
    for (unsigned i = 0; i < count; i++)
        put(data, &offset, references[i], number <= 256 ? 1 : number <= 65536 ? 2 : 4);
    put(data, &offset, page, wide ? 4 : 1);
    put(data, &offset, length, 4);
    return offset;
}

static int parse(const unsigned char *data, size_t length, unsigned global, size_t work,
                 unsigned count, int index) {
    pdf_jb2_context context;
    assert(cupidimage_pdf_jb2_context_init(&context, &work, PDF_JB2_MEMORY_LIMIT));
    pdf_jb2_segments list = {.context = &context};
    int ok = cupidimage_pdf_jb2_segments_read(&list, data, length, global);
    if (ok && index)
        ok = cupidimage_pdf_jb2_segments_index(&list);
    if (ok) {
        assert(list.count == count);
        for (size_t i = 0; i < list.count; i++) {
            pdf_jb2_segment *s = list.segments + i;
            assert(cupidimage_pdf_jb2_segment_retains(s, 0));
            for (unsigned j = 1; j <= s->reference_count; j++)
                assert(cupidimage_pdf_jb2_segment_retains(s, j));
            assert(!cupidimage_pdf_jb2_segment_retains(s, s->reference_count + 1));
        }
    } else
        assert(context.failed);
    cupidimage_pdf_jb2_clear(&context);
    assert(!live_count);
    checks++;
    return ok;
}

int main(void) {
    unsigned char data[8192];
    uint32_t references[32];
    for (unsigned i = 0; i < 32; i++)
        references[i] = i;
    const uint32_t numbers[] = {32, 256, 257, 65536, 65537, UINT32_MAX};
    for (unsigned n = 0; n < 6; n++)
        for (unsigned count = 0; count < 32; count++)
            for (unsigned wide = 0; wide < 2; wide++) {
                size_t length = header(data, numbers[n], 0, 1, references, count, 0, wide);
                assert(parse(data, length, 0, 1000000, 1, 0));
                for (size_t truncated = 1; truncated < length; truncated++)
                    assert(!parse(data, truncated, 0, 1000000, 1, 0));
            }
    size_t length = header(data, 100, 0, 1, references, 32, 0, 0);
    for (unsigned i = 0; i < 32; i++)
        length += header(data + length, i, 0, 0, NULL, 0, 0, 0);
    assert(parse(data, length, 0, 1000000, 33, 1));
    size_t allocations = 0;
    for (size_t failure = 0; failure <= allocations + 1; failure++) {
        fail_at = failure;
        allocation_count = 0;
        int ok = parse(data, length, 0, 1000000, 33, 1);
        if (!failure)
            allocations = allocation_count;
        assert(ok == (!failure || failure > allocations));
    }
    fail_at = 0;
    for (size_t work = 0; work < 20000; work += 13)
        parse(data, length, 0, work, 33, 1);
    for (unsigned bad = 0; bad < 11; bad++) {
        size_t end = header(data, 1, 0, 0, NULL, 0, 0, 0);
        uint32_t ref = bad == 2 ? 2 : bad == 3 ? 3 : bad == 4 ? 0 : 1;
        end += header(data + end, bad == 0 ? 1 : 2, bad == 5 ? 36 : 0, bad == 6 ? 2 : 1, &ref, 1, 0,
                      0);
        if (bad == 1)
            data[5] = 0xa0;
        if (bad == 7)
            data[5] = 3;
        if (bad == 8)
            data[4] = 55;
        if (bad == 9)
            data[6] = 1;
        if (bad == 10)
            data[10] = 255;
        assert(!parse(data, end, bad == 9, 1000000, 2, 1));
    }
    for (unsigned mmr = 0; mmr < 2; mmr++) {
        size_t start = header(data, 1, 38, 1, NULL, 0, UINT32_MAX, 0);
        size_t end = start;
        put(data, &end, 10, 4);
        put(data, &end, 20, 4);
        put(data, &end, 0, 4);
        put(data, &end, 0, 4);
        put(data, &end, 0, 1);
        put(data, &end, mmr, 1);
        if (!mmr)
            for (unsigned i = 0; i < 8; i++)
                put(data, &end, 0, 1);
        put(data, &end, 0x12345678, 4);
        put(data, &end, mmr ? 0 : 0xffac, 2);
        put(data, &end, 10, 4);
        assert(parse(data, end, 0, 1000000, 1, 1));
        for (size_t truncated = start; truncated < end; truncated++)
            assert(!parse(data, truncated, 0, 1000000, 1, 0));
        data[end - 1] = 21;
        assert(!parse(data, end, 0, 1000000, 1, 0));
        data[end - 1] = 10;
        data[4] = 39;
        assert(!parse(data, end, 0, 1000000, 1, 0));
    }
    (void)fault_realloc;
    printf("JBIG2 segments: %u header, graph, bounds and ownership checks\n", checks);
    return 0;
}
