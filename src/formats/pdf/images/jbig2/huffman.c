#include "internal.h"
#include <limits.h>
#include <string.h>

#include "third_party/jbig2-tables/huffman.inc"

static unsigned jb2_huffman_node(pdf_jb2_huffman *table) {
    unsigned index = table->used++;
    table->nodes[index] = (pdf_jb2_huff_node){{-1, -1}, -1};
    return index;
}

static int jb2_code_add(unsigned char code[32], unsigned amount) {
    for (unsigned i = 32; i && amount;) {
        i--;
        unsigned sum = code[i] + amount;
        code[i] = (unsigned char)sum;
        amount = sum >> 8;
    }
    return amount == 0;
}

static int jb2_code_shift(unsigned char code[32]) {
    unsigned carry = 0;
    for (unsigned i = 32; i;) {
        i--;
        unsigned next = code[i] >> 7;
        code[i] = (unsigned char)((code[i] << 1) | carry);
        carry = next;
    }
    return carry == 0;
}

int cupidimage_pdf_jb2_huffman_build(pdf_jb2_context *context, const pdf_jb2_huff_row *rows,
                                     unsigned count, pdf_jb2_huffman *table) {
    if (!table)
        return cupidimage_pdf_jb2_fail(context);
    memset(table, 0, sizeof(*table));
    if (!rows || !count || count > 65536 || !cupidimage_pdf_jb2_take(context, count))
        return cupidimage_pdf_jb2_fail(context);
    unsigned histogram[256] = {0};
    unsigned char next[256][32] = {{0}};
    unsigned prefix_work = 0, maximum_length = 0, used = 0;
    for (unsigned i = 0; i < count; i++) {
        const pdf_jb2_huff_row *row = rows + i;
        if (row->prefix > 255 || row->range > 255 || row->flags > 2 ||
            (row->flags == 2 && row->range))
            return cupidimage_pdf_jb2_fail(context);
        if (row->low < (int64_t)INT32_MIN - 1 || row->low > INT32_MAX)
            return cupidimage_pdf_jb2_fail(context);
        if (row->prefix) {
            histogram[row->prefix]++;
            prefix_work += row->prefix;
            if (row->prefix > maximum_length)
                maximum_length = row->prefix;
            used++;
        }
    }
    if (!used || !cupidimage_pdf_jb2_take(context, (uint64_t)count * 32 + prefix_work + 8192))
        return cupidimage_pdf_jb2_fail(context);
    unsigned char code[32] = {0};
    for (unsigned length = 1; length <= maximum_length; length++) {
        if (!jb2_code_add(code, histogram[length - 1]) || !jb2_code_shift(code))
            return cupidimage_pdf_jb2_fail(context);
        memcpy(next[length], code, sizeof(code));
        unsigned char end[32], limit[32] = {0};
        memcpy(end, code, sizeof(end));
        limit[31 - length / 8] = (unsigned char)(1u << (length % 8));
        if (!jb2_code_add(end, histogram[length]) || memcmp(end, limit, sizeof(end)) > 0)
            return cupidimage_pdf_jb2_fail(context);
    }
    /* A canonical prefix tree has at most one unfilled right fringe per depth;
     * all other internal nodes branch to two populated subtrees. */
    unsigned capacity = used * 2 + maximum_length + 1;
    pdf_jb2_huffman result = {.rows = rows, .count = count, .capacity = capacity};
    result.nodes = cupidimage_pdf_jb2_alloc(context, capacity, sizeof(*result.nodes));
    if (!result.nodes)
        return 0;
    jb2_huffman_node(&result);
    for (unsigned i = 0; i < count; i++) {
        unsigned length = rows[i].prefix;
        if (!length)
            continue;
        unsigned char word[32];
        memcpy(word, next[length], sizeof(word));
        if (!jb2_code_add(next[length], 1))
            return cupidimage_pdf_jb2_fail(context);
        unsigned node = 0;
        for (unsigned depth = length; depth; depth--) {
            if (result.nodes[node].row >= 0)
                return cupidimage_pdf_jb2_fail(context);
            unsigned bit = (word[31 - (depth - 1) / 8] >> ((depth - 1) % 8)) & 1u;
            if (result.nodes[node].child[bit] < 0) {
                if (result.used == result.capacity)
                    return cupidimage_pdf_jb2_fail(context);
                result.nodes[node].child[bit] = (int32_t)jb2_huffman_node(&result);
            }
            node = (unsigned)result.nodes[node].child[bit];
        }
        if (result.nodes[node].row >= 0 || result.nodes[node].child[0] >= 0 ||
            result.nodes[node].child[1] >= 0)
            return cupidimage_pdf_jb2_fail(context);
        result.nodes[node].row = (int32_t)i;
    }
    *table = result;
    return 1;
}

int cupidimage_pdf_jb2_huffman_standard(pdf_jb2_context *context, unsigned number,
                                        pdf_jb2_huffman *table) {
    if (!number || number > 15)
        return cupidimage_pdf_jb2_fail(context);
    unsigned start = jb2_standard_offsets[number - 1], end = jb2_standard_offsets[number];
    return cupidimage_pdf_jb2_huffman_build(context, jb2_standard_rows + start, end - start, table);
}

int cupidimage_pdf_jb2_huffman_value(pdf_jb2_bits *bits, const pdf_jb2_huffman *table,
                                     int64_t *value, int *out_of_band) {
    if (!bits || !table || !value || !out_of_band)
        return 0;
    *value = 0;
    *out_of_band = 0;
    if (!table->nodes || !table->rows || !table->used)
        return cupidimage_pdf_jb2_fail(bits->context);
    unsigned node = 0;
    for (unsigned depth = 0; depth <= 255; depth++) {
        if (node >= table->used)
            break;
        int32_t index = table->nodes[node].row;
        if (index >= 0) {
            if ((unsigned)index >= table->count)
                break;
            const pdf_jb2_huff_row *row = table->rows + index;
            if (row->flags == 2) {
                *out_of_band = 1;
                return 1;
            }
            uint32_t extra = 0;
            unsigned remaining = row->range;
            while (remaining > 32) {
                unsigned count = remaining - 32 > 32 ? 32 : remaining - 32;
                if (!cupidimage_pdf_jb2_read(bits, count, &extra) || extra)
                    return cupidimage_pdf_jb2_fail(bits->context);
                remaining -= count;
            }
            if (!cupidimage_pdf_jb2_read(bits, remaining, &extra))
                return 0;
            int64_t result = row->flags == 1 ? row->low - extra : row->low + extra;
            if (result < INT32_MIN || result > INT32_MAX)
                return cupidimage_pdf_jb2_fail(bits->context);
            *value = result;
            return 1;
        }
        uint32_t bit;
        if (depth == 255)
            return cupidimage_pdf_jb2_fail(bits->context);
        if (!cupidimage_pdf_jb2_read(bits, 1, &bit))
            return 0;
        int32_t next_node = table->nodes[node].child[bit];
        if (next_node < 0)
            break;
        node = (unsigned)next_node;
    }
    return cupidimage_pdf_jb2_fail(bits->context);
}
