#include "fault_alloc.h"
#include "formats/pdf/images/jbig2/symbols.h"
#include <assert.h>
#include <limits.h>
#define malloc fault_malloc
#define calloc fault_calloc
#define free fault_free
#include "../src/formats/pdf/images/jbig2/memory.c"
#undef malloc
#undef calloc
#undef free

static unsigned checks;
static const unsigned char arithmetic_input[30] = {
    0x84, 0xc7, 0x3b, 0xfc, 0xe1, 0xa1, 0x43, 0x04, 0x02, 0x20, 0x00, 0x00, 0x41, 0x0d, 0xbb,
    0x86, 0xf4, 0x31, 0x7f, 0xff, 0x88, 0xff, 0x37, 0x47, 0x1a, 0xdb, 0x6a, 0xdf, 0xff, 0xac};
static const unsigned char arithmetic_expected[32] = {
    0x00, 0x02, 0x00, 0x51, 0x00, 0x00, 0x00, 0xc0, 0x03, 0x52, 0x87, 0x2a, 0xaa, 0xaa, 0xaa, 0xaa,
    0x82, 0xc0, 0x20, 0x00, 0xfc, 0xd7, 0x9e, 0xf6, 0xbf, 0x7f, 0xed, 0x90, 0x4f, 0x46, 0xa3, 0xbf};

static void arithmetic_tests(void) {
    for (size_t budget = 0; budget < 1500; budget++) {
        size_t work = budget;
        pdf_jb2_context context;
        assert(cupidimage_pdf_jb2_context_init(&context, &work, PDF_JB2_MEMORY_LIMIT));
        pdf_jb2_arithmetic decoder;
        int ok = cupidimage_pdf_jb2_arithmetic_init(&decoder, &context, arithmetic_input, 30);
        unsigned char state = 0;
        for (unsigned i = 0; ok && i < 256; i++) {
            unsigned bit;
            ok = cupidimage_pdf_jb2_arithmetic_bit(&decoder, &state, &bit);
            if (ok)
                assert(bit == ((arithmetic_expected[i / 8] >> (7 - i % 8)) & 1u));
        }
        assert(ok == !context.failed && work <= budget);
        cupidimage_pdf_jb2_clear(&context);
        checks++;
    }
    for (size_t length = 0; length < 30; length++) {
        size_t work = 10000;
        pdf_jb2_context context;
        assert(cupidimage_pdf_jb2_context_init(&context, &work, 1024));
        pdf_jb2_arithmetic decoder;
        int ok = cupidimage_pdf_jb2_arithmetic_init(&decoder, &context, arithmetic_input, length);
        unsigned char state = 0;
        for (unsigned i = 0; ok && i < 256; i++) {
            unsigned bit;
            ok = cupidimage_pdf_jb2_arithmetic_bit(&decoder, &state, &bit);
        }
        assert(!ok && context.failed);
        cupidimage_pdf_jb2_clear(&context);
        checks++;
    }
}

static void huffman_tests(void) {
    for (unsigned number = 1; number <= 15; number++) {
        size_t work = 1000000;
        pdf_jb2_context context;
        assert(cupidimage_pdf_jb2_context_init(&context, &work, PDF_JB2_MEMORY_LIMIT));
        pdf_jb2_huffman table;
        assert(cupidimage_pdf_jb2_huffman_standard(&context, number, &table));
        uint64_t code = 0;
        unsigned previous_length = 0;
        for (unsigned length = 1; length <= 32; length++) {
            code <<= length - previous_length;
            previous_length = length;
            for (unsigned row = 0; row < table.count; row++) {
                const pdf_jb2_huff_row *entry = table.rows + row;
                if (entry->prefix != length)
                    continue;
                uint64_t maximum = (UINT64_C(1) << entry->range) - 1;
                const uint64_t extra[] = {0, maximum / 2, maximum};
                for (unsigned sample = 0; sample < 3; sample++) {
                    unsigned total = length + entry->range, bytes = (total + 7) / 8;
                    uint64_t encoded = ((code << entry->range) | extra[sample])
                                       << (bytes * 8 - total);
                    unsigned char input[8];
                    for (unsigned i = 0; i < bytes; i++)
                        input[i] = (unsigned char)(encoded >> ((bytes - i - 1) * 8));
                    size_t value_work = 1000;
                    pdf_jb2_context value_context;
                    assert(cupidimage_pdf_jb2_context_init(&value_context, &value_work, 1024));
                    pdf_jb2_bits bits = {&value_context, input, bytes, 0};
                    int64_t value;
                    int oob;
                    int64_t expected = entry->flags == 1 ? entry->low - (int64_t)extra[sample]
                                                         : entry->low + (int64_t)extra[sample];
                    int valid =
                        entry->flags == 2 || (expected >= INT32_MIN && expected <= INT32_MAX);
                    assert(cupidimage_pdf_jb2_huffman_value(&bits, &table, &value, &oob) == valid);
                    assert(bits.bit == total && oob == (entry->flags == 2));
                    if (valid && !oob)
                        assert(value == expected);
                    if (!valid)
                        assert(value_context.failed && !value);
                    cupidimage_pdf_jb2_clear(&value_context);
                    checks++;
                }
                code++;
            }
        }
        cupidimage_pdf_jb2_clear(&context);
        assert(!live_count);
    }
    for (unsigned bad = 0; bad < 5; bad++) {
        pdf_jb2_huff_row rows[] = {{1, 0, 0, 0}, {1, 0, 0, 1}, {1, 0, 0, 2}};
        unsigned count = 3;
        if (bad == 1) {
            rows[0].prefix = 256;
            count = 1;
        }
        if (bad == 2) {
            rows[0].range = 256;
            count = 1;
        }
        if (bad == 3) {
            rows[0] = (pdf_jb2_huff_row){1, 32, 1, INT64_MIN};
            count = 1;
        }
        if (bad == 4) {
            rows[0].prefix = 0;
            count = 1;
        }
        size_t work = 100000;
        pdf_jb2_context context;
        assert(cupidimage_pdf_jb2_context_init(&context, &work, PDF_JB2_MEMORY_LIMIT));
        pdf_jb2_huffman table;
        assert(!cupidimage_pdf_jb2_huffman_build(&context, rows, count, &table));
        assert(context.failed && !table.nodes);
        cupidimage_pdf_jb2_clear(&context);
        assert(!live_count);
        checks++;
    }
}

static void long_huffman_tests(void) {
    for (unsigned lower = 0; lower < 2; lower++) {
        size_t work = 100000;
        pdf_jb2_context context;
        assert(cupidimage_pdf_jb2_context_init(&context, &work, 100000));
        pdf_jb2_huff_row row = {255, 255, lower, lower ? INT32_MAX : INT32_MIN};
        pdf_jb2_huffman table;
        assert(cupidimage_pdf_jb2_huffman_build(&context, &row, 1, &table));
        unsigned char input[64] = {0};
        for (unsigned bit = 478; bit < 510; bit++)
            input[bit / 8] |= (unsigned char)(128u >> (bit % 8));
        pdf_jb2_bits bits = {&context, input, sizeof(input), 0};
        int64_t value;
        int oob;
        assert(cupidimage_pdf_jb2_huffman_value(&bits, &table, &value, &oob));
        assert(!oob && bits.bit == 510 && value == (lower ? INT32_MIN : INT32_MAX));
        cupidimage_pdf_jb2_clear(&context);
        assert(!live_count);
        checks++;
    }
}

static void put32(unsigned char *data, int64_t value) {
    uint32_t word = (uint32_t)value;
    for (unsigned i = 0; i < 4; i++)
        data[i] = (unsigned char)(word >> (24 - 8 * i));
}

static void custom_huffman_tests(void) {
    static const unsigned char example[] = {0x42, 0,    0,    0,    0,    0,   1,
                                            1,    0x10, 0x49, 0x23, 0x81, 0x80};
    for (size_t length = 0; length <= sizeof(example); length++) {
        size_t work = 1000000;
        pdf_jb2_context context;
        assert(cupidimage_pdf_jb2_context_init(&context, &work, PDF_JB2_MEMORY_LIMIT));
        pdf_jb2_huffman table;
        int ok = cupidimage_pdf_jb2_huffman_custom(&context, example, length, &table);
        assert(ok == (length == sizeof(example)));
        if (ok) {
            assert(table.count == 5 && table.rows[3].prefix == 0);
            assert(table.rows[3].flags == 1 && table.rows[3].low == -1);
            assert(table.rows[4].low == 65808 && table.rows[4].prefix == 3);
        } else
            assert(context.failed && !table.nodes);
        cupidimage_pdf_jb2_clear(&context);
        assert(!live_count);
        checks++;
    }
    for (unsigned number = 1; number <= 15; number++) {
        size_t work = 1000000;
        pdf_jb2_context context;
        assert(cupidimage_pdf_jb2_context_init(&context, &work, PDF_JB2_MEMORY_LIMIT));
        pdf_jb2_huffman standard, custom;
        assert(cupidimage_pdf_jb2_huffman_standard(&context, number, &standard));
        unsigned char data[256] = {0x7e};
        unsigned used = 9, normal = 0, lower = 0, upper = 0, oob = 0;
        int64_t high = 0;
        for (unsigned i = 0; i < standard.count; i++) {
            const pdf_jb2_huff_row *row = standard.rows + i;
            if (row->flags == 1)
                lower = row->prefix;
            else if (row->flags == 2)
                oob = row->prefix;
            else if (row->range == 32)
                upper = row->prefix;
            else {
                data[used++] = (unsigned char)row->prefix;
                data[used++] = (unsigned char)row->range;
                high = row->low + (INT64_C(1) << row->range);
                normal++;
            }
        }
        put32(data + 1, standard.rows[0].low);
        put32(data + 5, high);
        data[used++] = (unsigned char)lower;
        data[used++] = (unsigned char)upper;
        if (oob) {
            data[0] |= 1;
            data[used++] = (unsigned char)oob;
        }
        assert(cupidimage_pdf_jb2_huffman_custom(&context, data, used, &custom));
        assert(custom.count == normal + 2 + !!oob);
        for (unsigned i = 0; i < normal; i++) {
            assert(custom.rows[i].low == standard.rows[i].low);
            assert(custom.rows[i].prefix == standard.rows[i].prefix);
            assert(custom.rows[i].range == standard.rows[i].range);
        }
        assert(custom.rows[normal].prefix == lower);
        assert(custom.rows[normal + 1].prefix == upper);
        cupidimage_pdf_jb2_clear(&context);
        assert(!live_count);
        checks++;
    }
    for (unsigned sample = 0; sample < 8; sample++) {
        unsigned char data[16] = {0x7e};
        put32(data + 1, INT32_MIN);
        put32(data + 5, INT32_MAX);
        data[9] = 255;
        data[10] = 255;
        size_t length = 13;
        if (sample == 1)
            data[0] |= 128;
        if (sample == 2)
            put32(data + 5, INT32_MIN);
        if (sample == 3) {
            put32(data + 1, 1);
            put32(data + 5, 0);
        }
        if (sample == 4)
            length++;
        if (sample == 5)
            data[9] = 0;
        if (sample == 6) {
            data[9] = 1;
            data[11] = 1;
            data[12] = 1;
        }
        if (sample == 7) {
            data[0] |= 1;
            data[13] = 255;
            length++;
        }
        size_t work = 1000000;
        pdf_jb2_context context;
        assert(cupidimage_pdf_jb2_context_init(&context, &work, PDF_JB2_MEMORY_LIMIT));
        pdf_jb2_huffman table;
        assert(cupidimage_pdf_jb2_huffman_custom(&context, data, length, &table) ==
               (sample == 0 || sample == 7));
        cupidimage_pdf_jb2_clear(&context);
        assert(!live_count);
        checks++;
    }
    size_t allocations = 0;
    for (size_t failure = 0; failure <= allocations + 1; failure++) {
        fail_at = failure;
        allocation_count = 0;
        size_t work = 1000000;
        pdf_jb2_context context;
        assert(cupidimage_pdf_jb2_context_init(&context, &work, PDF_JB2_MEMORY_LIMIT));
        pdf_jb2_huffman table;
        int ok = cupidimage_pdf_jb2_huffman_custom(&context, example, sizeof(example), &table);
        if (!failure)
            allocations = allocation_count;
        assert(ok == (!failure || failure > allocations));
        cupidimage_pdf_jb2_clear(&context);
        assert(!live_count);
        checks++;
    }
    fail_at = 0;
}

static void push_bits(unsigned char *data, size_t *offset, unsigned value, unsigned count) {
    for (unsigned i = count; i; i--, (*offset)++)
        if ((value >> (i - 1)) & 1u)
            data[*offset / 8] |= (unsigned char)(1u << (7 - *offset % 8));
}

static void symbol_code_tests(void) {
    for (unsigned sample = 0; sample < 9; sample++) {
        unsigned char data[64] = {0};
        size_t bit = 0;
        for (unsigned code = 0; code < 35; code++) {
            unsigned prefix = sample == 0   ? (code == 2 || code == 32 ? 1 : 0)
                              : sample == 1 ? (code == 1                  ? 1
                                               : code == 33 || code == 34 ? 2
                                                                          : 0)
                              : sample == 2 ? (code == 0 ? 1 : 0)
                              : sample == 3 ? 0
                              : sample == 4 ? (code == 32 ? 1 : 0)
                              : sample == 5 ? (code == 33 ? 1 : 0)
                              : sample == 6 ? (code < 3 ? 1 : 0)
                                            : (code == 34 ? 1 : 0);
            push_bits(data, &bit, prefix, 4);
        }
        unsigned count = 4;
        if (!sample) {
            push_bits(data, &bit, 0, 1);
            push_bits(data, &bit, 1, 1);
            push_bits(data, &bit, 0, 2);
        } else if (sample == 1) {
            count = 150;
            push_bits(data, &bit, 0, 2);
            push_bits(data, &bit, 3, 2);
            push_bits(data, &bit, 127, 7);
            push_bits(data, &bit, 2, 2);
            push_bits(data, &bit, 7, 3);
        } else if (sample == 2)
            push_bits(data, &bit, 0, 4);
        else if (sample == 3)
            count = 0;
        else if (sample == 5) {
            push_bits(data, &bit, 0, 1);
            push_bits(data, &bit, 7, 3);
        } else if (sample == 8) {
            count = 138;
            push_bits(data, &bit, 0, 1);
            push_bits(data, &bit, 127, 7);
        }
        size_t length = (bit + 7) / 8, work = 1000000;
        pdf_jb2_context context;
        assert(cupidimage_pdf_jb2_context_init(&context, &work, PDF_JB2_MEMORY_LIMIT));
        pdf_jb2_bits bits = {&context, data, length, 0};
        pdf_jb2_huffman table;
        int ok = cupidimage_pdf_jb2_symbol_codes(&bits, count, &table);
        assert(ok == (sample < 4 || sample == 8));
        if (ok) {
            assert(bits.bit == length * 8 && table.count == count);
            for (unsigned i = 0; i < count; i++) {
                unsigned prefix = !sample ? 2 : sample == 1 && i < 2 ? 1 : 0;
                assert(table.rows[i].prefix == prefix && table.rows[i].low == i);
            }
        } else
            assert(context.failed);
        cupidimage_pdf_jb2_clear(&context);
        assert(!live_count);
        checks++;
    }
}

static void bitmap_tests(void) {
    for (unsigned operation = 0; operation < 5; operation++)
        for (int y = -4; y <= 4; y++)
            for (int x = -4; x <= 4; x++) {
                size_t work = 10000;
                pdf_jb2_context context;
                assert(cupidimage_pdf_jb2_context_init(&context, &work, 10000));
                pdf_jb2_bitmap *target = cupidimage_pdf_jb2_bitmap_new(&context, 5, 4, 1);
                pdf_jb2_bitmap *source = cupidimage_pdf_jb2_bitmap_new(&context, 3, 3, 0);
                assert(source && target);
                for (unsigned i = 0; i < 3; i++)
                    cupidimage_pdf_jb2_pixel_set(source, i, i, 1);
                assert(cupidimage_pdf_jb2_blit(&context, target, source, x, y, operation));
                for (int row = 0; row < 4; row++)
                    for (int col = 0; col < 5; col++) {
                        unsigned want = 1;
                        int sx = col - x, sy = row - y;
                        if (sx >= 0 && sx < 3 && sy >= 0 && sy < 3) {
                            unsigned b = sx == sy;
                            want = operation == 0   ? 1
                                   : operation == 1 ? b
                                   : operation == 2 ? !b
                                   : operation == 3 ? b
                                                    : b;
                        }
                        assert(cupidimage_pdf_jb2_pixel(target, col, row) == want);
                    }
                for (unsigned row = 0; row < 4; row++)
                    assert(!(target->data[row] & 7));
                cupidimage_pdf_jb2_clear(&context);
                assert(!live_count);
                checks++;
            }
}

static void ownership_tests(void) {
    size_t total = 0;
    for (size_t failure = 0; failure <= total + 1; failure++) {
        fail_at = failure;
        allocation_count = 0;
        size_t work = 100000;
        pdf_jb2_context context;
        assert(cupidimage_pdf_jb2_context_init(&context, &work, 100000));
        pdf_jb2_bitmap *bitmap = cupidimage_pdf_jb2_bitmap_new(&context, 100, 20, 1);
        pdf_jb2_huffman table;
        int ok = bitmap && cupidimage_pdf_jb2_huffman_standard(&context, 15, &table);
        if (!failure)
            total = allocation_count;
        assert(ok == (!failure || failure > total));
        if (ok) {
            unsigned char *data = cupidimage_pdf_jb2_detach(&context, bitmap->data);
            assert(data == bitmap->data);
            fault_free(data);
        }
        cupidimage_pdf_jb2_clear(&context);
        assert(!live_count);
        checks++;
    }
    fail_at = 0;
    (void)fault_realloc;
    (void)fault_malloc;
}

int main(void) {
    arithmetic_tests();
    huffman_tests();
    long_huffman_tests();
    custom_huffman_tests();
    symbol_code_tests();
    bitmap_tests();
    ownership_tests();
    printf("JBIG2 primitives: %u arithmetic, Huffman, bitmap and ownership checks\n", checks);
    return 0;
}
