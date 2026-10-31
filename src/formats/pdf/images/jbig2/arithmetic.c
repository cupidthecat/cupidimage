#include "internal.h"
#include <string.h>

static const struct {
    uint16_t qe;
    unsigned char next_mps, next_lps, swap;
} jb2_probability[47] = {

#include "third_party/jbig2-tables/arithmetic.inc"
};

static int jb2_byte_in(pdf_jb2_arithmetic *decoder) {
    if (!cupidimage_pdf_jb2_take(decoder->context, 1) || decoder->position >= decoder->length - 1)
        return cupidimage_pdf_jb2_fail(decoder->context);
    unsigned current = decoder->data[decoder->position];
    unsigned next = decoder->data[decoder->position + 1];
    if (current == 255 && next > 0x8f) {
        decoder->marker = next;
        decoder->c += 0xff00;
        decoder->ct = 8;
    } else {
        decoder->position++;
        decoder->c += (uint32_t)next << (current == 255 ? 9 : 8);
        decoder->ct = current == 255 ? 7 : 8;
    }
    return 1;
}

int cupidimage_pdf_jb2_arithmetic_init(pdf_jb2_arithmetic *decoder, pdf_jb2_context *context,
                                       const unsigned char *data, size_t length) {
    if (!decoder)
        return cupidimage_pdf_jb2_fail(context);
    memset(decoder, 0, sizeof(*decoder));
    if (!data || length < 2 || length > PDF_JB2_INPUT_LIMIT || !context || context->failed)
        return cupidimage_pdf_jb2_fail(context);
    if (data[length - 2] != 255 || data[length - 1] != 172 ||
        !cupidimage_pdf_jb2_take(context, length))
        return cupidimage_pdf_jb2_fail(context);
    for (size_t i = 0; i + 2 < length; i++)
        if (data[i] == 255 && data[i + 1] > 0x8f)
            return cupidimage_pdf_jb2_fail(context);
    decoder->context = context;
    decoder->data = data;
    decoder->length = length;
    decoder->c = (uint32_t)data[0] << 16;
    if (!jb2_byte_in(decoder))
        return 0;
    decoder->c <<= 7;
    decoder->ct -= 7;
    decoder->a = 0x8000;
    return 1;
}

int cupidimage_pdf_jb2_arithmetic_bit(pdf_jb2_arithmetic *decoder, unsigned char *state,
                                      unsigned *value) {
    if (!decoder || !state || !value)
        return 0;
    *value = 0;
    if (*state >= 94 || !decoder->data || decoder->length < 2 ||
        !cupidimage_pdf_jb2_take(decoder->context, 1))
        return cupidimage_pdf_jb2_fail(decoder->context);
    unsigned index = *state >> 1, mps = *state & 1, next = index;
    uint32_t qe = jb2_probability[index].qe;
    decoder->a -= qe;
    if ((decoder->c >> 16) < qe) {
        if (decoder->a < qe) {
            *value = mps;
            next = jb2_probability[index].next_mps;
        } else {
            *value = mps ^ 1;
            next = jb2_probability[index].next_lps;
            mps ^= jb2_probability[index].swap;
        }
        decoder->a = qe;
    } else {
        decoder->c -= qe << 16;
        if (decoder->a & 0x8000) {
            *value = mps;
            return 1;
        }
        if (decoder->a < qe) {
            *value = mps ^ 1;
            next = jb2_probability[index].next_lps;
            mps ^= jb2_probability[index].swap;
        } else {
            *value = mps;
            next = jb2_probability[index].next_mps;
        }
    }
    *state = (unsigned char)((next << 1) | mps);
    do {
        if (!cupidimage_pdf_jb2_take(decoder->context, 1) ||
            (!decoder->ct && !jb2_byte_in(decoder)))
            return 0;
        decoder->a <<= 1;
        decoder->c <<= 1;
        decoder->ct--;
    } while (!(decoder->a & 0x8000));
    return 1;
}

static int jb2_integer_bit(pdf_jb2_arithmetic *decoder, unsigned char *states, unsigned *previous,
                           unsigned *bit) {
    if (!cupidimage_pdf_jb2_arithmetic_bit(decoder, states + *previous, bit))
        return 0;
    *previous = (((*previous << 1) | *bit) & 511) | (*previous >= 256 ? 256u : 0u);
    return 1;
}

int cupidimage_pdf_jb2_arithmetic_integer(pdf_jb2_arithmetic *decoder, unsigned char states[512],
                                          int64_t *value, int *out_of_band) {
    if (!decoder || !states || !value || !out_of_band)
        return 0;
    *value = 0;
    *out_of_band = 0;
    unsigned previous = 1, sign, level = 0, bit;
    if (!jb2_integer_bit(decoder, states, &previous, &sign))
        return 0;
    for (; level < 5; level++) {
        if (!jb2_integer_bit(decoder, states, &previous, &bit))
            return 0;
        if (!bit)
            break;
    }
    static const unsigned widths[6] = {2, 4, 6, 8, 12, 32};
    static const unsigned offsets[6] = {0, 4, 20, 84, 340, 4436};
    uint64_t magnitude = 0;
    for (unsigned i = 0; i < widths[level]; i++) {
        if (!jb2_integer_bit(decoder, states, &previous, &bit))
            return 0;
        magnitude = (magnitude << 1) | bit;
    }
    magnitude += offsets[level];
    *out_of_band = sign && !magnitude;
    *value = sign ? -(int64_t)magnitude : (int64_t)magnitude;
    return 1;
}

int cupidimage_pdf_jb2_arithmetic_id(pdf_jb2_arithmetic *decoder, unsigned char *states,
                                     size_t capacity, unsigned length, uint32_t *value) {
    if (!decoder || !value)
        return 0;
    *value = 0;
    if (length > 16 || (length && (!states || capacity < (1u << length))) ||
        !cupidimage_pdf_jb2_take(decoder->context, 0))
        return cupidimage_pdf_jb2_fail(decoder->context);
    unsigned previous = 1, bit;
    for (unsigned i = 0; i < length; i++) {
        if (!cupidimage_pdf_jb2_arithmetic_bit(decoder, states + previous, &bit))
            return 0;
        previous = (previous << 1) | bit;
    }
    *value = previous - (1u << length);
    return 1;
}
