#include "document.h"
#include <string.h>

static int jb2_generic_region(pdf_jb2_document *document, pdf_jb2_segment *segment,
                              pdf_jb2_region *region) {
    pdf_jb2_context *context = document->context;
    pdf_jb2_bits bits = {context, segment->data, segment->length, 17 * 8};
    uint32_t flags;
    if (!cupidimage_pdf_jb2_read(&bits, 8, &flags) || (flags & 0xe0) ||
        ((flags & 1) && (flags & 30)))
        return cupidimage_pdf_jb2_fail(context);
    pdf_jb2_generic generic;
    if (!cupidimage_pdf_jb2_generic_defaults(&generic, (flags >> 1) & 3, (flags >> 4) & 1))
        return cupidimage_pdf_jb2_fail(context);
    generic.width = region->width;
    generic.height = segment->unknown_length ? segment->rows : region->height;
    generic.typical = (flags >> 3) & 1;
    if (!(flags & 1))
        for (unsigned i = 0; i < generic.at_count; i++)
            if (!cupidimage_pdf_jb2_signed_byte(&bits, generic.at_x + i) ||
                !cupidimage_pdf_jb2_signed_byte(&bits, generic.at_y + i))
                return 0;
    size_t offset = bits.bit / 8, length = segment->length - offset;
    size_t suffix = segment->unknown_length ? (flags & 1 ? 6 : 4) : 0;
    if (length < suffix)
        return cupidimage_pdf_jb2_fail(context);
    length -= suffix;
    if (flags & 1) {
        size_t consumed;
        if (!cupidimage_pdf_jb2_mmr(context, segment->data + offset, length, generic.width,
                                    generic.height, segment->unknown_length, &consumed,
                                    &region->bitmap))
            return 0;
        if (segment->unknown_length && consumed != length)
            return cupidimage_pdf_jb2_fail(context);
    } else {
        size_t state_count = cupidimage_pdf_jb2_generic_contexts(generic.template_id);
        unsigned char *states = cupidimage_pdf_jb2_alloc(context, state_count, 1);
        pdf_jb2_arithmetic decoder;
        if (!states ||
            !cupidimage_pdf_jb2_arithmetic_init(&decoder, context, segment->data + offset,
                                                length) ||
            !cupidimage_pdf_jb2_generic_decode(context, &generic, &decoder, states, state_count,
                                               &region->bitmap) ||
            !cupidimage_pdf_jb2_release(context, states))
            return 0;
    }
    region->height = generic.height;
    return 1;
}

static int jb2_refinement_region(pdf_jb2_document *document, pdf_jb2_segment *segment,
                                 pdf_jb2_region *region) {
    pdf_jb2_context *context = document->context;
    pdf_jb2_bits bits = {context, segment->data, segment->length, 17 * 8};
    uint32_t flags;
    if (!cupidimage_pdf_jb2_read(&bits, 8, &flags) || flags > 3)
        return cupidimage_pdf_jb2_fail(context);
    pdf_jb2_refinement p = {.width = region->width,
                            .height = region->height,
                            .template_id = flags & 1,
                            .typical = flags >> 1};
    if (!p.template_id)
        for (unsigned i = 0; i < 2; i++)
            if (!cupidimage_pdf_jb2_signed_byte(&bits, p.at_x + i) ||
                !cupidimage_pdf_jb2_signed_byte(&bits, p.at_y + i))
                return 0;
    pdf_jb2_bitmap *cropped = NULL;
    if (segment->reference_count) {
        pdf_jb2_segment *reference =
            cupidimage_pdf_jb2_segment_find(&document->segments, segment->references[0]);
        pdf_jb2_region *source = reference ? reference->result : NULL;
        if (!source || !source->bitmap || source->width != region->width ||
            source->height != region->height || source->x != region->x || source->y != region->y ||
            source->combination != region->combination)
            return cupidimage_pdf_jb2_fail(context);
        p.reference = source->bitmap;
    } else {
        if (region->combination != 4 || !document->page)
            return cupidimage_pdf_jb2_fail(context);
        cropped = cupidimage_pdf_jb2_bitmap_new(context, region->width, region->height, 0);
        if (!cropped || !cupidimage_pdf_jb2_blit(context, cropped, document->page,
                                                 -(int64_t)region->x, -(int64_t)region->y, 4))
            return 0;
        p.reference = cropped;
    }
    size_t state_count = p.template_id ? 1024 : 8192;
    unsigned char *states = cupidimage_pdf_jb2_alloc(context, state_count, 1);
    pdf_jb2_arithmetic decoder;
    if (!states ||
        !cupidimage_pdf_jb2_arithmetic_init(&decoder, context, segment->data + bits.bit / 8,
                                            segment->length - bits.bit / 8) ||
        !cupidimage_pdf_jb2_refinement_decode(context, &p, &decoder, states, state_count,
                                              &region->bitmap) ||
        !cupidimage_pdf_jb2_release(context, states))
        return 0;
    if (cropped && (!cupidimage_pdf_jb2_release(context, cropped->data) ||
                    !cupidimage_pdf_jb2_release(context, cropped)))
        return 0;
    return 1;
}

static int jb2_halftone_region(pdf_jb2_document *document, pdf_jb2_segment *segment,
                               pdf_jb2_region *region) {
    pdf_jb2_context *context = document->context;
    pdf_jb2_bits bits = {context, segment->data, segment->length, 17 * 8};
    uint32_t flags, gx, gy, vx, vy;
    pdf_jb2_halftone p = {.width = region->width, .height = region->height};
    if (!cupidimage_pdf_jb2_read(&bits, 8, &flags) ||
        !cupidimage_pdf_jb2_read(&bits, 32, &p.grid_width) ||
        !cupidimage_pdf_jb2_read(&bits, 32, &p.grid_height) ||
        !cupidimage_pdf_jb2_read(&bits, 32, &gx) || !cupidimage_pdf_jb2_read(&bits, 32, &gy) ||
        !cupidimage_pdf_jb2_read(&bits, 16, &vx) || !cupidimage_pdf_jb2_read(&bits, 16, &vy))
        return 0;
    p.mmr = flags & 1;
    p.template_id = (flags >> 1) & 3;
    p.skip = (flags >> 3) & 1;
    p.combination = (flags >> 4) & 7;
    p.default_pixel = flags >> 7;
    p.grid_x = (int32_t)(gx & 0x80000000u ? (int64_t)gx - INT64_C(4294967296) : gx);
    p.grid_y = (int32_t)(gy & 0x80000000u ? (int64_t)gy - INT64_C(4294967296) : gy);
    p.vector_x = (uint16_t)vx;
    p.vector_y = (uint16_t)vy;
    if (segment->reference_count != 1)
        return cupidimage_pdf_jb2_fail(context);
    pdf_jb2_segment *reference =
        cupidimage_pdf_jb2_segment_find(&document->segments, segment->references[0]);
    if (!reference || reference->type != 16 || !reference->result)
        return cupidimage_pdf_jb2_fail(context);
    p.patterns = reference->result;
    return cupidimage_pdf_jb2_halftone_decode(context, segment->data + bits.bit / 8,
                                              segment->length - bits.bit / 8, &p, &region->bitmap);
}

int cupidimage_pdf_jb2_decode_region(pdf_jb2_document *document, pdf_jb2_segment *segment) {
    pdf_jb2_context *context = document->context;
    pdf_jb2_region *region = cupidimage_pdf_jb2_alloc(context, 1, sizeof(*region));
    pdf_jb2_bits bits = {context, segment->data, segment->length, 0};
    if (!region || !document->page || !cupidimage_pdf_jb2_region_header(&bits, region) ||
        (int64_t)region->y <= document->stripe_end)
        return cupidimage_pdf_jb2_fail(context);
    unsigned type = segment->type;
    int ok =
        type == 4 || type == 6 || type == 7
            ? cupidimage_pdf_jb2_decode_text(document, segment, region)
        : type == 20 || type == 22 || type == 23 ? jb2_halftone_region(document, segment, region)
        : type == 36 || type == 38 || type == 39 ? jb2_generic_region(document, segment, region)
                                                 : jb2_refinement_region(document, segment, region);
    if (!ok)
        return 0;
    if (type & 2) {
        unsigned operation = region->combination;
        if (!(document->page_flags & 64) && !(type >= 40 && !segment->reference_count))
            operation = (document->page_flags >> 3) & 3;
        if (!cupidimage_pdf_jb2_blit(context, document->page, region->bitmap, region->x, region->y,
                                     operation) ||
            !cupidimage_pdf_jb2_release(context, region->bitmap->data) ||
            !cupidimage_pdf_jb2_release(context, region->bitmap) ||
            !cupidimage_pdf_jb2_release(context, region))
            return 0;
    } else
        segment->result = region;
    return 1;
}
