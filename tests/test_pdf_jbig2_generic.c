#include "formats/pdf/images/jbig2/internal.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    unsigned width, height, template_id, extended, typical, skip, length;
    if (scanf("%u %u %u %u %u %u %u", &width, &height, &template_id, &extended, &typical, &skip,
              &length) != 7 ||
        length > 1024 * 1024 || (uint64_t)width * height > 1024 * 1024 || skip > 1)
        return 2;
    size_t work = 100000000;
    pdf_jb2_context context;
    if (!cupidimage_pdf_jb2_context_init(&context, &work, PDF_JB2_MEMORY_LIMIT))
        return 2;
    pdf_jb2_generic parameters;
    if (!cupidimage_pdf_jb2_generic_defaults(&parameters, template_id, extended))
        return 2;
    parameters.width = width;
    parameters.height = height;
    parameters.typical = typical;
    for (unsigned i = 0; i < parameters.at_count; i++) {
        int x, y;
        if (scanf("%d %d", &x, &y) != 2 || x < -128 || x > 127 || y < -128 || y > 127)
            return 2;
        parameters.at_x[i] = (int16_t)x;
        parameters.at_y[i] = (int16_t)y;
    }
    if (skip) {
        pdf_jb2_bitmap *mask = cupidimage_pdf_jb2_bitmap_new(&context, width, height, 0);
        if (!mask) {
            cupidimage_pdf_jb2_clear(&context);
            return 3;
        }
        for (unsigned y = 0; y < height; y++)
            for (unsigned x = 0; x < width; x++) {
                unsigned bit;
                if (scanf("%u", &bit) != 1 || bit > 1) {
                    cupidimage_pdf_jb2_clear(&context);
                    return 2;
                }
                cupidimage_pdf_jb2_pixel_set(mask, x, y, bit);
            }
        parameters.skip = mask;
    }
    unsigned char *data = cupidimage_pdf_jb2_alloc(&context, length, 1);
    size_t states_count = cupidimage_pdf_jb2_generic_contexts(template_id);
    unsigned char *states = cupidimage_pdf_jb2_alloc(&context, states_count, 1);
    if (!data || !states) {
        cupidimage_pdf_jb2_clear(&context);
        return 3;
    }
    for (unsigned i = 0; i < length; i++) {
        unsigned value;
        if (scanf("%x", &value) != 1 || value > 255) {
            cupidimage_pdf_jb2_clear(&context);
            return 2;
        }
        data[i] = (unsigned char)value;
    }
    pdf_jb2_arithmetic decoder;
    pdf_jb2_bitmap *bitmap = NULL;
    int ok = cupidimage_pdf_jb2_arithmetic_init(&decoder, &context, data, length) &&
             cupidimage_pdf_jb2_generic_decode(&context, &parameters, &decoder, states,
                                               states_count, &bitmap);
    if (ok && bitmap->stride && bitmap->height)
        ok = fwrite(bitmap->data, bitmap->stride, bitmap->height, stdout) == bitmap->height;
    cupidimage_pdf_jb2_clear(&context);
    return ok ? 0 : 3;
}
