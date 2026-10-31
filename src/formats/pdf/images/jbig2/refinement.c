#include "internal.h"

/* T.88 Figures 12-15, gathered in reading order, output pixels before reference
 * pixels. Context permutation is arbitrary; the SLTP context uses the same order. */
static const int8_t jb2_ref_x[2][9] = {{-1, 0, 1, -1, 0, 1, -1, 0, 1}, {0, -1, 0, 1, 0, 1}};
static const int8_t jb2_ref_y[2][9] = {{-1, -1, -1, 0, 0, 0, 1, 1, 1}, {-1, 0, 0, 0, 1, 1}};

int cupidimage_pdf_jb2_refinement_decode(pdf_jb2_context *context,
                                         const pdf_jb2_refinement *parameters,
                                         pdf_jb2_arithmetic *decoder, unsigned char *states,
                                         size_t state_count, pdf_jb2_bitmap **output) {
    if (!output)
        return cupidimage_pdf_jb2_fail(context);
    *output = NULL;
    if (!parameters || !decoder || decoder->context != context || !states ||
        !parameters->reference || parameters->template_id > 1 || parameters->typical > 1 ||
        state_count < (parameters->template_id ? 1024u : 8192u))
        return cupidimage_pdf_jb2_fail(context);
    unsigned template_id = parameters->template_id;
    if (!template_id) {
        for (unsigned i = 0; i < 2; i++)
            if (parameters->at_x[i] < -128 || parameters->at_x[i] > 127 ||
                parameters->at_y[i] < -128 || parameters->at_y[i] > 127)
                return cupidimage_pdf_jb2_fail(context);
        if (parameters->at_y[0] > 0 || (!parameters->at_y[0] && parameters->at_x[0] >= 0))
            return cupidimage_pdf_jb2_fail(context);
    }
    pdf_jb2_bitmap *bitmap =
        cupidimage_pdf_jb2_bitmap_new(context, parameters->width, parameters->height, 0);
    if (!bitmap)
        return 0;
    unsigned typical = 0, reference_count = template_id ? 6 : 9;
    for (uint32_t y = 0; y < parameters->height; y++) {
        if (!cupidimage_pdf_jb2_take(context, 1))
            return 0;
        if (parameters->typical) {
            unsigned difference;
            if (!cupidimage_pdf_jb2_arithmetic_bit(decoder, states + (template_id ? 8 : 16),
                                                   &difference))
                return 0;
            typical ^= difference;
        }
        for (uint32_t x = 0; x < parameters->width; x++) {
            if (!cupidimage_pdf_jb2_take(context, typical ? 23 : 14))
                return 0;
            int64_t rx = (int64_t)x - parameters->dx, ry = (int64_t)y - parameters->dy;
            unsigned value = cupidimage_pdf_jb2_pixel(parameters->reference, rx, ry);
            unsigned predicted = typical;
            if (typical) {
                for (int oy = -1; oy <= 1; oy++)
                    for (int ox = -1; ox <= 1; ox++)
                        if (cupidimage_pdf_jb2_pixel(parameters->reference, rx + ox, ry + oy) !=
                            value)
                            predicted = 0;
            }
            if (!predicted) {
                int ax = template_id ? -1 : parameters->at_x[0];
                int ay = template_id ? -1 : parameters->at_y[0];
                unsigned index = cupidimage_pdf_jb2_pixel(bitmap, (int64_t)x + ax, (int64_t)y + ay);
                index = (index << 1) | cupidimage_pdf_jb2_pixel(bitmap, x, (int64_t)y - 1);
                index =
                    (index << 1) | cupidimage_pdf_jb2_pixel(bitmap, (int64_t)x + 1, (int64_t)y - 1);
                index = (index << 1) | cupidimage_pdf_jb2_pixel(bitmap, (int64_t)x - 1, y);
                for (unsigned i = 0; i < reference_count; i++) {
                    ax = !template_id && !i ? parameters->at_x[1] : jb2_ref_x[template_id][i];
                    ay = !template_id && !i ? parameters->at_y[1] : jb2_ref_y[template_id][i];
                    index = (index << 1) |
                            cupidimage_pdf_jb2_pixel(parameters->reference, rx + ax, ry + ay);
                }
                if (!cupidimage_pdf_jb2_arithmetic_bit(decoder, states + index, &value))
                    return 0;
            }
            cupidimage_pdf_jb2_pixel_set(bitmap, x, y, value);
        }
    }
    *output = bitmap;
    return 1;
}
