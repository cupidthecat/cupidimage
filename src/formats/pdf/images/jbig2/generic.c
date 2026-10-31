#include "internal.h"
#include <string.h>

/* Generic templates: low bits represent the most recent same-row pixels. */
static const int8_t jb2_template_x[4][16] = {
    {-1, -2, -3, -4, 3, 2, 1, 0, -1, -2, -3, 2, 1, 0, -1, -2},
    {-1, -2, -3, 3, 2, 1, 0, -1, -2, 2, 1, 0, -1},
    {-1, -2, 2, 1, 0, -1, -2, 1, 0, -1},
    {-1, -2, -3, -4, 2, 1, 0, -1, -2, -3}};
static const int8_t jb2_template_y[4][16] = {
    {0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -2, -2, -2, -2, -2},
    {0, 0, 0, -1, -1, -1, -1, -1, -1, -2, -2, -2, -2},
    {0, 0, -1, -1, -1, -1, -1, -2, -2, -2},
    {0, 0, 0, 0, -1, -1, -1, -1, -1, -1}};
static const unsigned jb2_template_bits[4] = {16, 13, 10, 10};
static const unsigned jb2_predictor_context[4] = {0x9b25, 0x795, 0xe5, 0x195};
static const unsigned jb2_extended_at_bits[12] = {1, 13, 9, 14, 12, 5, 2, 3, 11, 4, 15, 10};
static const unsigned jb2_default_at_bits[4][4] = {{4, 10, 11, 15}, {3}, {2}, {4}};

size_t cupidimage_pdf_jb2_generic_contexts(unsigned template_id) {
    return template_id < 4 ? (size_t)1 << jb2_template_bits[template_id] : 0;
}

int cupidimage_pdf_jb2_generic_defaults(pdf_jb2_generic *parameters, unsigned template_id,
                                        unsigned extended) {
    if (!parameters)
        return 0;
    memset(parameters, 0, sizeof(*parameters));
    if (template_id > 3 || extended > 1 || (extended && template_id))
        return 0;
    parameters->template_id = template_id;
    parameters->extended = extended;
    parameters->at_count = template_id ? 1 : extended ? 12 : 4;
    const unsigned *indices = extended ? jb2_extended_at_bits : jb2_default_at_bits[template_id];
    for (unsigned i = 0; i < parameters->at_count; i++) {
        parameters->at_x[i] = jb2_template_x[template_id][indices[i]];
        parameters->at_y[i] = jb2_template_y[template_id][indices[i]];
    }
    return 1;
}

int cupidimage_pdf_jb2_generic_decode(pdf_jb2_context *context, const pdf_jb2_generic *parameters,
                                      pdf_jb2_arithmetic *decoder, unsigned char *states,
                                      size_t state_count, pdf_jb2_bitmap **output) {
    if (!output)
        return cupidimage_pdf_jb2_fail(context);
    *output = NULL;
    if (!parameters || !decoder || decoder->context != context || !states ||
        parameters->template_id > 3 || parameters->extended > 1 || parameters->typical > 1 ||
        parameters->pattern_width > 255 || (parameters->pattern_width && parameters->extended) ||
        (parameters->extended && parameters->template_id) ||
        state_count < cupidimage_pdf_jb2_generic_contexts(parameters->template_id))
        return cupidimage_pdf_jb2_fail(context);
    unsigned template_id = parameters->template_id;
    unsigned count = template_id ? 1 : parameters->extended ? 12 : 4;
    if (parameters->at_count != count ||
        (parameters->skip && (parameters->skip->width != parameters->width ||
                              parameters->skip->height != parameters->height)))
        return cupidimage_pdf_jb2_fail(context);
    int16_t xs[16], ys[16];
    for (unsigned i = 0; i < jb2_template_bits[template_id]; i++) {
        xs[i] = jb2_template_x[template_id][i];
        ys[i] = jb2_template_y[template_id][i];
    }
    const unsigned *indices =
        parameters->extended ? jb2_extended_at_bits : jb2_default_at_bits[template_id];
    for (unsigned i = 0; i < count; i++) {
        int x = parameters->at_x[i], y = parameters->at_y[i];
        int pattern_at = !i && parameters->pattern_width;
        if ((pattern_at && (x != -(int)parameters->pattern_width || y)) ||
            (!pattern_at && (x < -128 || x > 127)) || y < -128 || y > 0 || (!y && x >= 0))
            return cupidimage_pdf_jb2_fail(context);
        xs[indices[i]] = (int16_t)x;
        ys[indices[i]] = (int16_t)y;
    }
    pdf_jb2_bitmap *bitmap =
        cupidimage_pdf_jb2_bitmap_new(context, parameters->width, parameters->height, 0);
    if (!bitmap)
        return 0;
    unsigned typical = 0;
    for (uint32_t y = 0; y < bitmap->height; y++) {
        if (!cupidimage_pdf_jb2_take(context, bitmap->stride + 1))
            return 0;
        if (parameters->typical) {
            unsigned bit;
            if (!cupidimage_pdf_jb2_arithmetic_bit(
                    decoder, states + jb2_predictor_context[template_id], &bit))
                return 0;
            typical ^= bit;
        }
        if (typical) {
            if (y && bitmap->stride)
                memcpy(bitmap->data + (size_t)y * bitmap->stride,
                       bitmap->data + (size_t)(y - 1) * bitmap->stride, bitmap->stride);
            continue;
        }
        for (uint32_t x = 0; x < bitmap->width; x++) {
            if (!cupidimage_pdf_jb2_take(context, jb2_template_bits[template_id] + 1))
                return 0;
            if (parameters->skip && cupidimage_pdf_jb2_pixel(parameters->skip, x, y))
                continue;
            unsigned index = 0;
            for (unsigned i = 0; i < jb2_template_bits[template_id]; i++)
                index |= cupidimage_pdf_jb2_pixel(bitmap, (int64_t)x + xs[i], (int64_t)y + ys[i])
                         << i;
            unsigned bit;
            if (!cupidimage_pdf_jb2_arithmetic_bit(decoder, states + index, &bit))
                return 0;
            cupidimage_pdf_jb2_pixel_set(bitmap, x, y, bit);
        }
    }
    *output = bitmap;
    return 1;
}
