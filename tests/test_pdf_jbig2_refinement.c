#include "formats/pdf/images/jbig2/internal.h"
#include <stdio.h>

int main(void) {
    pdf_jb2_refinement p = {0};
    unsigned rw, rh, length;
    if (scanf("%u %u %u %u %u %u %d %d %u", &p.width, &p.height, &rw, &rh, &p.template_id,
              &p.typical, &p.dx, &p.dy, &length) != 9 ||
        length > 1024 * 1024 || (uint64_t)p.width * p.height > 1024 * 1024 ||
        (uint64_t)rw * rh > 1024 * 1024 || p.template_id > 1)
        return 2;
    for (unsigned i = 0; i < 2; i++) {
        int x, y;
        if (scanf("%d %d", &x, &y) != 2 || x < -128 || x > 127 || y < -128 || y > 127)
            return 2;
        p.at_x[i] = (int16_t)x;
        p.at_y[i] = (int16_t)y;
    }
    size_t work = 100000000;
    pdf_jb2_context context;
    if (!cupidimage_pdf_jb2_context_init(&context, &work, PDF_JB2_MEMORY_LIMIT))
        return 2;
    pdf_jb2_bitmap *reference = cupidimage_pdf_jb2_bitmap_new(&context, rw, rh, 0);
    unsigned char *data = cupidimage_pdf_jb2_alloc(&context, length, 1);
    size_t state_count = p.template_id ? 1024 : 8192;
    unsigned char *states = cupidimage_pdf_jb2_alloc(&context, state_count, 1);
    int ok = reference && data && states;
    for (uint32_t y = 0; ok && y < rh; y++)
        for (uint32_t x = 0; ok && x < rw; x++) {
            unsigned value;
            ok = scanf("%u", &value) == 1 && value < 2;
            if (ok)
                cupidimage_pdf_jb2_pixel_set(reference, x, y, value);
        }
    for (unsigned i = 0; ok && i < length; i++) {
        unsigned value;
        ok = scanf("%x", &value) == 1 && value < 256;
        if (ok)
            data[i] = (unsigned char)value;
    }
    p.reference = reference;
    pdf_jb2_bitmap *output = NULL;
    pdf_jb2_arithmetic decoder;
    ok = ok && cupidimage_pdf_jb2_arithmetic_init(&decoder, &context, data, length) &&
         cupidimage_pdf_jb2_refinement_decode(&context, &p, &decoder, states, state_count, &output);
    if (ok)
        ok = fwrite(output->data, 1, output->stride * output->height, stdout) ==
             output->stride * output->height;
    cupidimage_pdf_jb2_clear(&context);
    return ok ? 0 : 1;
}
