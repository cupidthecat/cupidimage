#include "document.h"

int cupidimage_pdf_jb2_decode_text(pdf_jb2_document *document, pdf_jb2_segment *segment,
                                   pdf_jb2_region *region) {
    pdf_jb2_context *context = document->context;
    pdf_jb2_bits bits = {context, segment->data, segment->length, 17 * 8};
    uint32_t flags, huffman_flags = 0;
    if (!cupidimage_pdf_jb2_read(&bits, 16, &flags))
        return 0;
    unsigned huffman = flags & 1;
    pdf_jb2_text p = {.width = region->width,
                      .height = region->height,
                      .refine = (flags >> 1) & 1,
                      .strips_log = (flags >> 2) & 3,
                      .corner = (flags >> 4) & 3,
                      .transposed = (flags >> 6) & 1,
                      .combination = (flags >> 7) & 3,
                      .default_pixel = (flags >> 9) & 1};
    p.ds_offset = (int)((flags >> 10) & 31);
    if (p.ds_offset >= 16)
        p.ds_offset -= 32;
    p.refinement.template_id = flags >> 15;
    if ((!p.refine && p.refinement.template_id) ||
        (huffman && !cupidimage_pdf_jb2_read(&bits, 16, &huffman_flags)) ||
        (huffman_flags & 0x8000) || (!p.refine && (huffman_flags & 0x7fc0)))
        return cupidimage_pdf_jb2_fail(context);
    if (p.refine && !p.refinement.template_id)
        for (unsigned i = 0; i < 2; i++)
            if (!cupidimage_pdf_jb2_signed_byte(&bits, p.refinement.at_x + i) ||
                !cupidimage_pdf_jb2_signed_byte(&bits, p.refinement.at_y + i))
                return 0;
    if (!cupidimage_pdf_jb2_read(&bits, 32, &p.instances))
        return 0;
    pdf_jb2_huffman *custom[8];
    unsigned custom_count, custom_index = 0;
    pdf_jb2_dictionary_result *last;
    if (!cupidimage_pdf_jb2_imports(document, segment, &p.symbols, &p.symbol_count, custom,
                                    &custom_count, &last))
        return 0;
    if (huffman) {
        const pdf_jb2_huffman **targets[] = {&p.fs, &p.ds, &p.dt, &p.rdw, &p.rdh, &p.rdx, &p.rdy};
        for (unsigned i = 0; i < 7; i++) {
            unsigned selection = (huffman_flags >> (i * 2)) & 3;
            if (selection == 2 && (i == 0 || i >= 3))
                return cupidimage_pdf_jb2_fail(context);
            unsigned number = selection == 3 ? 0
                                             : selection + (i == 0   ? 6
                                                            : i == 1 ? 8
                                                            : i == 2 ? 11
                                                                     : 14);
            *targets[i] = cupidimage_pdf_jb2_select_table(document, number, custom, custom_count,
                                                          &custom_index, i == 1);
        }
        p.rsize = cupidimage_pdf_jb2_select_table(document, huffman_flags & 0x4000 ? 0 : 1, custom,
                                                  custom_count, &custom_index, 0);
    }
    if (context->failed || custom_count != custom_index)
        return cupidimage_pdf_jb2_fail(context);
    unsigned id_bits = 0;
    while ((1u << id_bits) < p.symbol_count)
        id_bits++;
    pdf_jb2_huffman symbol_codes;
    if (huffman && !cupidimage_pdf_jb2_symbol_codes(&bits, p.symbol_count, &symbol_codes))
        return 0;
    pdf_jb2_coding coding;
    if (!cupidimage_pdf_jb2_coding_init(&coding, context, segment->data + bits.bit / 8,
                                        segment->length - bits.bit / 8, huffman, id_bits))
        return 0;
    if (huffman)
        coding.symbol_codes = &symbol_codes;
    if (p.refine) {
        p.refinement_count = p.refinement.template_id ? 1024 : 8192;
        p.refinement_states = cupidimage_pdf_jb2_alloc(context, p.refinement_count, 1);
        if (!p.refinement_states)
            return 0;
    }
    if (!cupidimage_pdf_jb2_text_decode(&coding, &p, &region->bitmap) ||
        !cupidimage_pdf_jb2_release(context, p.symbols) ||
        !cupidimage_pdf_jb2_release(context, p.refinement_states) ||
        !cupidimage_pdf_jb2_release(context, coding.ids))
        return 0;
    if (huffman && (!cupidimage_pdf_jb2_release(context, symbol_codes.nodes) ||
                    !cupidimage_pdf_jb2_release(context, (void *)symbol_codes.rows)))
        return 0;
    return 1;
}
