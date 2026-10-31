#include "formats/pdf/images/jbig2/symbols.h"
#include <stdio.h>

int main(void) {
    pdf_jb2_text p = {0};
    unsigned huffman, length;
    if (scanf("%u %u %u %u %u %u %u %u %u %d %u %u %u", &p.width, &p.height, &p.instances, &huffman,
              &p.strips_log, &p.corner, &p.transposed, &p.combination, &p.default_pixel,
              &p.ds_offset, &p.refine, &p.refinement.template_id, &length) != 13 ||
        length > 1048576)
        return 2;
    size_t work = 100000000;
    pdf_jb2_context context;
    if (!cupidimage_pdf_jb2_context_init(&context, &work, PDF_JB2_MEMORY_LIMIT))
        return 2;
    pdf_jb2_bitmap *symbols[3];
    int ok = 1;
    for (unsigned i = 0; i < 3; i++) {
        symbols[i] = cupidimage_pdf_jb2_bitmap_new(&context, 3, 3, 0);
        if (!symbols[i]) {
            ok = 0;
            break;
        }
        for (unsigned y = 0; y < 3; y++)
            for (unsigned x = 0; x < 3; x++) {
                unsigned value;
                if (scanf("%u", &value) != 1 || value > 1) {
                    ok = 0;
                    break;
                }
                cupidimage_pdf_jb2_pixel_set(symbols[i], x, y, value);
            }
    }
    unsigned char *data = cupidimage_pdf_jb2_alloc(&context, length, 1);
    ok = ok && data;
    for (unsigned i = 0; ok && i < length; i++) {
        unsigned value;
        ok = scanf("%x", &value) == 1 && value < 256;
        if (ok)
            data[i] = (unsigned char)value;
    }
    p.symbols = symbols;
    p.symbol_count = 3;
    p.refinement.at_x[0] = p.refinement.at_x[1] = -1;
    p.refinement.at_y[0] = p.refinement.at_y[1] = -1;
    p.refinement_count = p.refinement.template_id ? 1024 : 8192;
    p.refinement_states = cupidimage_pdf_jb2_alloc(&context, p.refinement_count, 1);
    ok = ok && p.refinement_states;
    pdf_jb2_huffman fs, ds, dt, refine, size;
    if (huffman) {
        ok = ok && cupidimage_pdf_jb2_huffman_standard(&context, 6, &fs) &&
             cupidimage_pdf_jb2_huffman_standard(&context, 8, &ds) &&
             cupidimage_pdf_jb2_huffman_standard(&context, 11, &dt) &&
             cupidimage_pdf_jb2_huffman_standard(&context, 15, &refine) &&
             cupidimage_pdf_jb2_huffman_standard(&context, 1, &size);
        p.rdw = p.rdh = p.rdx = p.rdy = &refine;
        p.rsize = &size;
        p.fs = &fs;
        p.ds = &ds;
        p.dt = &dt;
    }
    pdf_jb2_coding coding;
    ok = ok && cupidimage_pdf_jb2_coding_init(&coding, &context, data, length, huffman, 2);
    coding.fixed_ids = 1;
    pdf_jb2_bitmap *output = NULL;
    ok = ok && cupidimage_pdf_jb2_text_decode(&coding, &p, &output);
    if (ok)
        ok = fwrite(output->data, 1, output->stride * output->height, stdout) ==
             output->stride * output->height;
    cupidimage_pdf_jb2_clear(&context);
    return ok ? 0 : 1;
}
