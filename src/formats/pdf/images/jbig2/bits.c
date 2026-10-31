#include "internal.h"

int cupidimage_pdf_jb2_read(pdf_jb2_bits *bits, unsigned count, uint32_t *value) {
    if (!bits || !value)
        return 0;
    *value = 0;
    if (count > 32 || bits->length > PDF_JB2_INPUT_LIMIT || (bits->length && !bits->data) ||
        bits->bit > bits->length * 8 || count > bits->length * 8 - bits->bit ||
        !cupidimage_pdf_jb2_take(bits->context, count))
        return cupidimage_pdf_jb2_fail(bits->context);
    for (unsigned i = 0; i < count; i++, bits->bit++)
        *value = (*value << 1) | ((bits->data[bits->bit / 8] >> (7 - bits->bit % 8)) & 1u);
    return 1;
}

int cupidimage_pdf_jb2_align(pdf_jb2_bits *bits) {
    uint32_t ignored;
    return bits && cupidimage_pdf_jb2_read(bits, (unsigned)((8 - bits->bit % 8) % 8), &ignored);
}
