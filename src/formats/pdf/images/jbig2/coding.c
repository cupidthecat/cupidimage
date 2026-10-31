#include "symbols.h"
#include <limits.h>
#include <string.h>

int cupidimage_pdf_jb2_coding_init(pdf_jb2_coding *coding, pdf_jb2_context *context,
                                   const unsigned char *data, size_t length, unsigned huffman,
                                   unsigned id_bits) {
    if (!coding)
        return cupidimage_pdf_jb2_fail(context);
    memset(coding, 0, sizeof(*coding));
    if (huffman > 1 || id_bits > 16 || length > PDF_JB2_INPUT_LIMIT || (length && !data))
        return cupidimage_pdf_jb2_fail(context);
    coding->context = context;
    coding->huffman = huffman;
    coding->id_bits = id_bits;
    coding->bits = (pdf_jb2_bits){context, data, length, 0};
    if (huffman)
        return 1;
    coding->id_count = (size_t)1 << id_bits;
    coding->ids = cupidimage_pdf_jb2_alloc(context, coding->id_count, 1);
    return coding->ids &&
           cupidimage_pdf_jb2_arithmetic_init(&coding->arithmetic, context, data, length);
}

int cupidimage_pdf_jb2_integer(pdf_jb2_coding *coding, unsigned kind, const pdf_jb2_huffman *table,
                               int64_t *value, int *oob) {
    if (!coding || !value || !oob)
        return 0;
    if (kind >= JB2_INTEGER_CONTEXTS)
        return cupidimage_pdf_jb2_fail(coding->context);
    int ok = coding->huffman ? cupidimage_pdf_jb2_huffman_value(&coding->bits, table, value, oob)
                             : cupidimage_pdf_jb2_arithmetic_integer(
                                   &coding->arithmetic, coding->integers[kind], value, oob);
    if (!ok || (!*oob && (*value < INT32_MIN || *value > INT32_MAX)))
        return cupidimage_pdf_jb2_fail(coding->context);
    return 1;
}

int cupidimage_pdf_jb2_integer_required(pdf_jb2_coding *coding, unsigned kind,
                                        const pdf_jb2_huffman *table, int64_t *value) {
    int oob;
    return cupidimage_pdf_jb2_integer(coding, kind, table, value, &oob) &&
           (!oob || cupidimage_pdf_jb2_fail(coding->context));
}

int cupidimage_pdf_jb2_symbol_id(pdf_jb2_coding *coding, uint32_t *value) {
    if (!coding || !value)
        return 0;
    if (!coding->huffman)
        return cupidimage_pdf_jb2_arithmetic_id(&coding->arithmetic, coding->ids, coding->id_count,
                                                coding->id_bits, value);
    if (coding->fixed_ids)
        return cupidimage_pdf_jb2_read(&coding->bits, coding->id_bits ? coding->id_bits : 1, value);
    int64_t decoded;
    int oob;
    if (!cupidimage_pdf_jb2_huffman_value(&coding->bits, coding->symbol_codes, &decoded, &oob) ||
        oob || decoded < 0 || decoded > 65535)
        return cupidimage_pdf_jb2_fail(coding->context);
    *value = (uint32_t)decoded;
    return 1;
}

int cupidimage_pdf_jb2_refine_symbol(pdf_jb2_coding *coding, const pdf_jb2_refinement *parameters,
                                     unsigned char *states, size_t state_count,
                                     const pdf_jb2_huffman *size_table, pdf_jb2_bitmap **output) {
    if (!coding || !output)
        return 0;
    *output = NULL;
    pdf_jb2_arithmetic local;
    pdf_jb2_arithmetic *decoder = &coding->arithmetic;
    size_t length = 0;
    if (coding->huffman) {
        int64_t size;
        if (!cupidimage_pdf_jb2_integer_required(coding, JB2_AI, size_table, &size) || size < 2 ||
            !cupidimage_pdf_jb2_align(&coding->bits) ||
            (uint64_t)size > coding->bits.length - coding->bits.bit / 8)
            return cupidimage_pdf_jb2_fail(coding->context);
        length = (size_t)size;
        const unsigned char *data = coding->bits.data + coding->bits.bit / 8;
        if (data[length - 2] != 255 || data[length - 1] != 172 ||
            !cupidimage_pdf_jb2_arithmetic_init(&local, coding->context, data, length))
            return cupidimage_pdf_jb2_fail(coding->context);
        decoder = &local;
    }
    if (!cupidimage_pdf_jb2_refinement_decode(coding->context, parameters, decoder, states,
                                              state_count, output))
        return 0;
    if (coding->huffman)
        coding->bits.bit += length * 8;
    return 1;
}
