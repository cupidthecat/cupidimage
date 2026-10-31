#include "formats/pdf/images/jbig2/halftone.h"
#include <stdio.h>

int main(void) {
    pdf_jb2_halftone p = {0};
    unsigned pattern_mmr, pattern_template, pw, ph, count, pattern_length, length;
    unsigned vx, vy;
    if (scanf("%u %u %u %u %u %u %u %u %u %u %u %u %d %d %u %u %u %u %u %u", &pattern_mmr,
              &pattern_template, &pw, &ph, &count, &pattern_length, &length, &p.width, &p.height,
              &p.mmr, &p.template_id, &p.skip, &p.grid_x, &p.grid_y, &vx, &vy, &p.grid_width,
              &p.grid_height, &p.combination, &p.default_pixel) != 20 ||
        vx > 65535 || vy > 65535 || pattern_length > 1048576 || length > 1048576)
        return 2;
    p.vector_x = (uint16_t)vx;
    p.vector_y = (uint16_t)vy;
    size_t work = 200000000;
    pdf_jb2_context context;
    if (!cupidimage_pdf_jb2_context_init(&context, &work, PDF_JB2_MEMORY_LIMIT))
        return 2;
    unsigned char *pattern_data = cupidimage_pdf_jb2_alloc(&context, pattern_length, 1);
    unsigned char *data = cupidimage_pdf_jb2_alloc(&context, length, 1);
    int ok = pattern_data && data;
    for (unsigned i = 0; ok && i < pattern_length + length; i++) {
        unsigned value;
        ok = scanf("%x", &value) == 1 && value < 256;
        if (ok) {
            if (i < pattern_length)
                pattern_data[i] = (unsigned char)value;
            else
                data[i - pattern_length] = (unsigned char)value;
        }
    }
    pdf_jb2_patterns *patterns = NULL;
    ok = ok &&
         cupidimage_pdf_jb2_patterns_decode(&context, pattern_data, pattern_length, pattern_mmr,
                                            pattern_template, pw, ph, count, &patterns);
    p.patterns = patterns;
    pdf_jb2_bitmap *output = NULL;
    ok = ok && cupidimage_pdf_jb2_halftone_decode(&context, data, length, &p, &output);
    if (ok) {
        size_t bytes = output->stride * output->height;
        ok = fwrite(output->data, 1, bytes, stdout) == bytes;
    }
    cupidimage_pdf_jb2_clear(&context);
    return ok ? 0 : 1;
}
