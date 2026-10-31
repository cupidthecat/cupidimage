#include "internal.h"
#include <string.h>

/* T.88 B.2. Scan before allocating so short input cannot reserve a large table. */
static int jb2_table_rows(pdf_jb2_bits *bits, unsigned prefix_bits, unsigned range_bits,
                          int64_t low, int64_t high, unsigned oob, pdf_jb2_huff_row *rows,
                          unsigned *count) {
    unsigned used = 0;
    int64_t current = low;
    do {
        uint32_t prefix, range;
        if (used >= 65533 || !cupidimage_pdf_jb2_read(bits, prefix_bits, &prefix) ||
            !cupidimage_pdf_jb2_read(bits, range_bits, &range))
            return cupidimage_pdf_jb2_fail(bits->context);
        if (rows)
            rows[used] = (pdf_jb2_huff_row){prefix, range, 0, current};
        used++;
        /* Any range of 32 or more bits reaches the signed 32-bit upper bound.
         * Avoid shifting a machine word by the encoded, up-to-255-bit length. */
        current = range >= 32 ? high : current + (INT64_C(1) << range);
    } while (current < high);
    for (unsigned i = 0; i < 2 + oob; i++) {
        uint32_t prefix;
        if (!cupidimage_pdf_jb2_read(bits, prefix_bits, &prefix))
            return 0;
        if (rows)
            rows[used] = (pdf_jb2_huff_row){prefix, i == 2 ? 0 : 32,
                                            i == 0   ? 1
                                            : i == 2 ? 2
                                                     : 0,
                                            i == 0   ? low - 1
                                            : i == 1 ? high
                                                     : 0};
        used++;
    }
    if (!cupidimage_pdf_jb2_align(bits) || bits->bit != bits->length * 8)
        return cupidimage_pdf_jb2_fail(bits->context);
    *count = used;
    return 1;
}

int cupidimage_pdf_jb2_huffman_custom(pdf_jb2_context *context, const unsigned char *data,
                                      size_t length, pdf_jb2_huffman *table) {
    if (!table)
        return cupidimage_pdf_jb2_fail(context);
    memset(table, 0, sizeof(*table));
    pdf_jb2_bits bits = {context, data, length, 0};
    uint32_t flags, low_bits, high_bits;
    if (!cupidimage_pdf_jb2_read(&bits, 8, &flags) || (flags & 128) ||
        !cupidimage_pdf_jb2_read(&bits, 32, &low_bits) ||
        !cupidimage_pdf_jb2_read(&bits, 32, &high_bits))
        return cupidimage_pdf_jb2_fail(context);
    int64_t low = low_bits & 0x80000000u ? (int64_t)low_bits - INT64_C(4294967296) : low_bits;
    int64_t high = high_bits & 0x80000000u ? (int64_t)high_bits - INT64_C(4294967296) : high_bits;
    if (low >= high)
        return cupidimage_pdf_jb2_fail(context);
    unsigned prefix_bits = ((flags >> 1) & 7) + 1, range_bits = ((flags >> 4) & 7) + 1;
    unsigned count = 0;
    pdf_jb2_bits scan = bits;
    if (!jb2_table_rows(&scan, prefix_bits, range_bits, low, high, flags & 1, NULL, &count))
        return 0;
    pdf_jb2_huff_row *rows = cupidimage_pdf_jb2_alloc(context, count, sizeof(*rows));
    if (!rows ||
        !jb2_table_rows(&bits, prefix_bits, range_bits, low, high, flags & 1, rows, &count))
        return 0;
    return cupidimage_pdf_jb2_huffman_build(context, rows, count, table);
}
