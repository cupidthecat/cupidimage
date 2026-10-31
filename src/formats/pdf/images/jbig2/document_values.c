#include "document.h"
#include <string.h>

int cupidimage_pdf_jb2_signed_byte(pdf_jb2_bits *bits, int16_t *value) {
    uint32_t raw;
    if (!cupidimage_pdf_jb2_read(bits, 8, &raw))
        return 0;
    *value = (int16_t)(raw >= 128 ? (int)raw - 256 : (int)raw);
    return 1;
}

int cupidimage_pdf_jb2_region_header(pdf_jb2_bits *bits, pdf_jb2_region *region) {
    uint32_t flags;
    memset(region, 0, sizeof(*region));
    if (!cupidimage_pdf_jb2_read(bits, 32, &region->width) ||
        !cupidimage_pdf_jb2_read(bits, 32, &region->height) ||
        !cupidimage_pdf_jb2_read(bits, 32, &region->x) ||
        !cupidimage_pdf_jb2_read(bits, 32, &region->y) || !cupidimage_pdf_jb2_read(bits, 8, &flags))
        return 0;
    if (flags > 4)
        return cupidimage_pdf_jb2_fail(bits->context);
    region->combination = flags;
    return 1;
}

const pdf_jb2_huffman *cupidimage_pdf_jb2_standard(pdf_jb2_document *document, unsigned number) {
    if (!number || number > 15) {
        cupidimage_pdf_jb2_fail(document->context);
        return NULL;
    }
    pdf_jb2_huffman *table = document->standard + number - 1;
    if (!table->nodes && !cupidimage_pdf_jb2_huffman_standard(document->context, number, table))
        return NULL;
    return table;
}

int cupidimage_pdf_jb2_imports(pdf_jb2_document *document, const pdf_jb2_segment *segment,
                               pdf_jb2_bitmap ***symbols, uint32_t *count,
                               pdf_jb2_huffman *tables[8], unsigned *table_count,
                               pdf_jb2_dictionary_result **last) {
    *symbols = NULL;
    *count = 0;
    *table_count = 0;
    *last = NULL;
    for (uint32_t i = 0; i < segment->reference_count; i++) {
        pdf_jb2_segment *reference =
            cupidimage_pdf_jb2_segment_find(&document->segments, segment->references[i]);
        if (!reference || !reference->result || reference->state != 2)
            return cupidimage_pdf_jb2_fail(document->context);
        if (reference->type == 53) {
            if (*table_count >= 8)
                return cupidimage_pdf_jb2_fail(document->context);
            tables[(*table_count)++] = reference->result;
        } else if (reference->type == 0) {
            pdf_jb2_dictionary_result *dictionary = reference->result;
            if (dictionary->count > 65536 - *count)
                return cupidimage_pdf_jb2_fail(document->context);
            *count += dictionary->count;
            *last = dictionary;
        } else
            return cupidimage_pdf_jb2_fail(document->context);
    }
    *symbols = cupidimage_pdf_jb2_alloc(document->context, *count, sizeof(**symbols));
    if (!*symbols)
        return 0;
    uint32_t offset = 0;
    for (uint32_t i = 0; i < segment->reference_count; i++) {
        pdf_jb2_segment *reference =
            cupidimage_pdf_jb2_segment_find(&document->segments, segment->references[i]);
        if (!reference)
            return cupidimage_pdf_jb2_fail(document->context);
        if (reference->type == 0) {
            pdf_jb2_dictionary_result *dictionary = reference->result;
            if (!cupidimage_pdf_jb2_take(document->context, dictionary->count))
                return 0;
            if (dictionary->count)
                memcpy(*symbols + offset, dictionary->symbols,
                       dictionary->count * sizeof(**symbols));
            offset += dictionary->count;
        }
    }
    return 1;
}

const pdf_jb2_huffman *cupidimage_pdf_jb2_select_table(pdf_jb2_document *document, unsigned number,
                                                       pdf_jb2_huffman *const *custom,
                                                       unsigned count, unsigned *index,
                                                       int needs_oob) {
    const pdf_jb2_huffman *table;
    if (number)
        table = cupidimage_pdf_jb2_standard(document, number);
    else {
        if (*index >= count) {
            cupidimage_pdf_jb2_fail(document->context);
            return NULL;
        }
        table = custom[(*index)++];
    }
    if (!table)
        return NULL;
    int oob = 0;
    for (unsigned i = 0; i < table->count; i++) {
        if (!cupidimage_pdf_jb2_take(document->context, 1))
            return NULL;
        if (table->rows[i].prefix && table->rows[i].flags == 2)
            oob = 1;
    }
    if (oob != needs_oob) {
        cupidimage_pdf_jb2_fail(document->context);
        return NULL;
    }
    return table;
}
