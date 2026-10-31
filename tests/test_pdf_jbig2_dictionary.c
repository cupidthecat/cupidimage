#include "formats/pdf/images/jbig2/symbols.h"
#include <stdio.h>

int main(void) {
    pdf_jb2_dictionary p = {0};
    unsigned huffman, template_id, refinement_template, length;
    if (scanf("%u %u %u %u %u %u %u %u", &huffman, &p.aggregate, &template_id, &refinement_template,
              &p.imported_count, &p.new_count, &p.exported_count, &length) != 8 ||
        length > 1048576 || p.imported_count > 256 || p.new_count > 256 || p.exported_count > 512 ||
        template_id > 3 || refinement_template > 1)
        return 2;
    size_t work = 100000000;
    pdf_jb2_context context;
    if (!cupidimage_pdf_jb2_context_init(&context, &work, PDF_JB2_MEMORY_LIMIT))
        return 2;
    int ok = cupidimage_pdf_jb2_generic_defaults(&p.generic, template_id, 0);
    p.refinement.template_id = refinement_template;
    p.refinement.at_x[0] = p.refinement.at_x[1] = -1;
    p.refinement.at_y[0] = p.refinement.at_y[1] = -1;
    p.generic_count = cupidimage_pdf_jb2_generic_contexts(template_id);
    p.refinement_count = refinement_template ? 1024 : 8192;
    p.generic_states = cupidimage_pdf_jb2_alloc(&context, p.generic_count, 1);
    p.refinement_states = cupidimage_pdf_jb2_alloc(&context, p.refinement_count, 1);
    p.imported = cupidimage_pdf_jb2_alloc(&context, p.imported_count, sizeof(*p.imported));
    ok = ok && p.generic_states && p.refinement_states && p.imported;
    for (unsigned i = 0; ok && i < p.imported_count; i++) {
        unsigned width, height;
        ok = scanf("%u %u", &width, &height) == 2 && width <= 1024 && height <= 1024;
        if (!ok)
            break;
        p.imported[i] = cupidimage_pdf_jb2_bitmap_new(&context, width, height, 0);
        ok = p.imported[i] != NULL;
        for (unsigned y = 0; ok && y < height; y++)
            for (unsigned x = 0; ok && x < width; x++) {
                unsigned bit;
                ok = scanf("%u", &bit) == 1 && bit < 2;
                if (ok)
                    cupidimage_pdf_jb2_pixel_set(p.imported[i], x, y, bit);
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
    pdf_jb2_huffman standard[15];
    if (huffman)
        for (unsigned i = 0; ok && i < 15; i++)
            ok = cupidimage_pdf_jb2_huffman_standard(&context, i + 1, standard + i);
    p.dh = standard + 3;
    p.dw = standard + 1;
    p.bitmap_size = p.aggregate_count = p.standard1 = standard;
    p.standard6 = standard + 5;
    p.standard8 = standard + 7;
    p.standard11 = standard + 10;
    p.standard15 = standard + 14;
    unsigned id_bits = 0;
    while ((1u << id_bits) < p.imported_count + p.new_count)
        id_bits++;
    pdf_jb2_coding coding;
    ok = ok && cupidimage_pdf_jb2_coding_init(&coding, &context, data, length, huffman, id_bits);
    coding.fixed_ids = 1;
    pdf_jb2_bitmap **output = NULL;
    ok = ok && cupidimage_pdf_jb2_dictionary_decode(&coding, &p, &output);
    if (ok) {
        ok = fwrite(&p.exported_count, 4, 1, stdout) == 1;
        for (unsigned i = 0; ok && i < p.exported_count; i++) {
            uint32_t header[] = {output[i]->width, output[i]->height, (uint32_t)output[i]->stride};
            size_t size = output[i]->stride * output[i]->height;
            ok = fwrite(header, 4, 3, stdout) == 3 &&
                 fwrite(output[i]->data, 1, size, stdout) == size;
        }
    }
    cupidimage_pdf_jb2_clear(&context);
    return ok ? 0 : 1;
}
