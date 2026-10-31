#include "symbols.h"
#include <string.h>

int cupidimage_pdf_jb2_symbol_codes(pdf_jb2_bits *bits, uint32_t count, pdf_jb2_huffman *result) {
    if (!bits || !result)
        return 0;
    memset(result, 0, sizeof(*result));
    if (count > 65536)
        return cupidimage_pdf_jb2_fail(bits->context);
    pdf_jb2_huff_row run_rows[35];
    for (unsigned i = 0; i < 35; i++) {
        uint32_t length;
        if (!cupidimage_pdf_jb2_read(bits, 4, &length))
            return 0;
        run_rows[i] = (pdf_jb2_huff_row){length, 0, 0, i};
    }
    if (!count)
        return cupidimage_pdf_jb2_align(bits);
    pdf_jb2_huffman runs;
    if (!cupidimage_pdf_jb2_huffman_build(bits->context, run_rows, 35, &runs))
        return 0;
    pdf_jb2_huff_row *rows = cupidimage_pdf_jb2_alloc(bits->context, count, sizeof(*rows));
    if (!rows)
        return 0;
    unsigned used = 0, previous = 0, active = 0;
    while (used < count) {
        int64_t value;
        int oob;
        if (!cupidimage_pdf_jb2_huffman_value(bits, &runs, &value, &oob) || oob || value < 0 ||
            value > 34)
            return cupidimage_pdf_jb2_fail(bits->context);
        uint32_t repeat = 1, length = (uint32_t)value;
        if (value >= 32) {
            if (value == 32 && !used)
                return cupidimage_pdf_jb2_fail(bits->context);
            unsigned extra = value == 32 ? 2 : value == 33 ? 3 : 7;
            if (!cupidimage_pdf_jb2_read(bits, extra, &repeat))
                return 0;
            repeat += value == 34 ? 11 : 3;
            length = value == 32 ? previous : 0;
        }
        if (repeat > count - used || !cupidimage_pdf_jb2_take(bits->context, repeat))
            return cupidimage_pdf_jb2_fail(bits->context);
        for (unsigned i = 0; i < repeat; i++, used++)
            rows[used] = (pdf_jb2_huff_row){length, 0, 0, used};
        active += length ? repeat : 0;
        previous = length;
    }
    if (!cupidimage_pdf_jb2_align(bits) || !cupidimage_pdf_jb2_release(bits->context, runs.nodes))
        return 0;
    if (!active) {
        result->rows = rows;
        result->count = count;
        return 1;
    }
    return cupidimage_pdf_jb2_huffman_build(bits->context, rows, count, result);
}
