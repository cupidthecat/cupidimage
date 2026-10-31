#include "document.h"
#include <string.h>

int cupidimage_pdf_jb2_decode_dictionary(pdf_jb2_document *document, pdf_jb2_segment *segment) {
    pdf_jb2_context *context = document->context;
    pdf_jb2_bits bits = {context, segment->data, segment->length, 0};
    uint32_t flags;
    if (!cupidimage_pdf_jb2_read(&bits, 16, &flags))
        return 0;
    unsigned huffman = flags & 1, aggregate = (flags >> 1) & 1;
    unsigned template_id = (flags >> 10) & 3, refinement_template = (flags >> 12) & 1;
    unsigned dh = (flags >> 2) & 3, dw = (flags >> 4) & 3;
    if ((flags & 0xe000) || (!huffman && (flags & 0xfc)) ||
        (huffman && (template_id || dh == 2 || dw == 2)) || (!aggregate && refinement_template) ||
        (huffman && !aggregate && (flags & 0x300)))
        return cupidimage_pdf_jb2_fail(context);
    pdf_jb2_dictionary p = {.aggregate = aggregate};
    cupidimage_pdf_jb2_generic_defaults(&p.generic, template_id, 0);
    p.refinement.template_id = refinement_template;
    if (!huffman)
        for (unsigned i = 0; i < p.generic.at_count; i++)
            if (!cupidimage_pdf_jb2_signed_byte(&bits, p.generic.at_x + i) ||
                !cupidimage_pdf_jb2_signed_byte(&bits, p.generic.at_y + i))
                return 0;
    if (aggregate && !refinement_template)
        for (unsigned i = 0; i < 2; i++)
            if (!cupidimage_pdf_jb2_signed_byte(&bits, p.refinement.at_x + i) ||
                !cupidimage_pdf_jb2_signed_byte(&bits, p.refinement.at_y + i))
                return 0;
    if (!cupidimage_pdf_jb2_read(&bits, 32, &p.exported_count) ||
        !cupidimage_pdf_jb2_read(&bits, 32, &p.new_count))
        return 0;
    pdf_jb2_huffman *custom[8];
    unsigned custom_count, custom_index = 0;
    pdf_jb2_dictionary_result *last;
    if (!cupidimage_pdf_jb2_imports(document, segment, &p.imported, &p.imported_count, custom,
                                    &custom_count, &last) ||
        p.new_count > 65536 - p.imported_count || p.exported_count > p.imported_count + p.new_count)
        return cupidimage_pdf_jb2_fail(context);
    if (huffman) {
        p.dh = cupidimage_pdf_jb2_select_table(document, dh == 3 ? 0 : dh + 4, custom, custom_count,
                                               &custom_index, 0);
        p.dw = cupidimage_pdf_jb2_select_table(document, dw == 3 ? 0 : dw + 2, custom, custom_count,
                                               &custom_index, 1);
        p.bitmap_size = cupidimage_pdf_jb2_select_table(document, flags & 64 ? 0 : 1, custom,
                                                        custom_count, &custom_index, 0);
        p.aggregate_count = cupidimage_pdf_jb2_select_table(document, flags & 128 ? 0 : 1, custom,
                                                            custom_count, &custom_index, 0);
        p.standard1 = cupidimage_pdf_jb2_standard(document, 1);
        if (aggregate) {
            p.standard6 = cupidimage_pdf_jb2_standard(document, 6);
            p.standard8 = cupidimage_pdf_jb2_standard(document, 8);
            p.standard11 = cupidimage_pdf_jb2_standard(document, 11);
            p.standard15 = cupidimage_pdf_jb2_standard(document, 15);
        }
    }
    if (context->failed || custom_index != custom_count)
        return cupidimage_pdf_jb2_fail(context);
    p.generic_count = huffman ? 0 : cupidimage_pdf_jb2_generic_contexts(template_id);
    p.refinement_count = !aggregate ? 0 : refinement_template ? 1024 : 8192;
    if (p.generic_count)
        p.generic_states = cupidimage_pdf_jb2_alloc(context, p.generic_count, 1);
    if (p.refinement_count)
        p.refinement_states = cupidimage_pdf_jb2_alloc(context, p.refinement_count, 1);
    if (context->failed)
        return 0;
    if (flags & 256) {
        if (!last || !(last->flags & 512) || ((last->flags ^ flags) & 0x1c03) ||
            last->generic_count != p.generic_count ||
            last->refinement_count != p.refinement_count ||
            memcmp(last->generic.at_x, p.generic.at_x, sizeof(p.generic.at_x)) ||
            memcmp(last->generic.at_y, p.generic.at_y, sizeof(p.generic.at_y)) ||
            memcmp(last->refinement.at_x, p.refinement.at_x, sizeof(p.refinement.at_x)) ||
            memcmp(last->refinement.at_y, p.refinement.at_y, sizeof(p.refinement.at_y)))
            return cupidimage_pdf_jb2_fail(context);
        if (!cupidimage_pdf_jb2_take(context, p.generic_count + p.refinement_count))
            return 0;
        if (p.generic_count)
            memcpy(p.generic_states, last->generic_states, p.generic_count);
        if (p.refinement_count)
            memcpy(p.refinement_states, last->refinement_states, p.refinement_count);
    }
    unsigned id_bits = 0;
    while ((1u << id_bits) < p.imported_count + p.new_count)
        id_bits++;
    pdf_jb2_coding coding;
    if (!cupidimage_pdf_jb2_coding_init(&coding, context, segment->data + bits.bit / 8,
                                        segment->length - bits.bit / 8, huffman, id_bits))
        return 0;
    coding.fixed_ids = 1;
    pdf_jb2_bitmap **symbols;
    if (!cupidimage_pdf_jb2_dictionary_decode(&coding, &p, &symbols))
        return 0;
    pdf_jb2_dictionary_result *result = cupidimage_pdf_jb2_alloc(context, 1, sizeof(*result));
    if (!result)
        return 0;
    *result = (pdf_jb2_dictionary_result){p.exported_count,    symbols,         flags,
                                          p.generic,           p.refinement,    p.generic_states,
                                          p.refinement_states, p.generic_count, p.refinement_count};
    if (!(flags & 512)) {
        if (!cupidimage_pdf_jb2_release(context, p.generic_states) ||
            !cupidimage_pdf_jb2_release(context, p.refinement_states))
            return 0;
        result->generic_states = result->refinement_states = NULL;
    }
    if (!cupidimage_pdf_jb2_release(context, p.imported) ||
        !cupidimage_pdf_jb2_release(context, coding.ids))
        return 0;
    segment->result = result;
    return 1;
}
