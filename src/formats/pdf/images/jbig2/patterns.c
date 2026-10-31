#include "halftone.h"

int cupidimage_pdf_jb2_patterns_decode(pdf_jb2_context *context, const unsigned char *data,
                                       size_t length, unsigned mmr, unsigned template_id,
                                       uint32_t width, uint32_t height, uint32_t count,
                                       pdf_jb2_patterns **output) {
    if (!output)
        return cupidimage_pdf_jb2_fail(context);
    *output = NULL;
    if (!count || count > 65536 || !width || width > 255 || !height || height > 255 || mmr > 1 ||
        template_id > 3 || (mmr && template_id) || (uint64_t)width * count > 65535)
        return cupidimage_pdf_jb2_fail(context);
    pdf_jb2_bitmap *collective = NULL;
    if (mmr) {
        size_t consumed;
        if (!cupidimage_pdf_jb2_mmr(context, data, length, width * count, height, 0, &consumed,
                                    &collective))
            return 0;
    } else {
        pdf_jb2_generic p;
        cupidimage_pdf_jb2_generic_defaults(&p, template_id, 0);
        p.width = width * count;
        p.height = height;
        p.pattern_width = width;
        p.at_x[0] = -(int16_t)width;
        p.at_y[0] = 0;
        size_t states_count = cupidimage_pdf_jb2_generic_contexts(template_id);
        unsigned char *states = cupidimage_pdf_jb2_alloc(context, states_count, 1);
        pdf_jb2_arithmetic decoder;
        if (!states || !cupidimage_pdf_jb2_arithmetic_init(&decoder, context, data, length) ||
            !cupidimage_pdf_jb2_generic_decode(context, &p, &decoder, states, states_count,
                                               &collective) ||
            !cupidimage_pdf_jb2_release(context, states))
            return 0;
    }
    pdf_jb2_patterns *result = cupidimage_pdf_jb2_alloc(context, 1, sizeof(*result));
    if (!result)
        return 0;
    *result = (pdf_jb2_patterns){count, width, height, NULL};
    result->patterns = cupidimage_pdf_jb2_alloc(context, count, sizeof(*result->patterns));
    if (!result->patterns)
        return 0;
    for (uint32_t i = 0; i < count; i++) {
        result->patterns[i] = cupidimage_pdf_jb2_bitmap_new(context, width, height, 0);
        if (!result->patterns[i] || !cupidimage_pdf_jb2_blit(context, result->patterns[i],
                                                             collective, -(int64_t)i * width, 0, 4))
            return 0;
    }
    if (!cupidimage_pdf_jb2_release(context, collective->data) ||
        !cupidimage_pdf_jb2_release(context, collective))
        return 0;
    *output = result;
    return 1;
}
