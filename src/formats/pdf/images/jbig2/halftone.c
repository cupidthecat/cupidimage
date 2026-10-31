#include "halftone.h"

static int64_t jb2_grid_floor(int64_t coordinate) {
    return coordinate / 256 - (coordinate < 0 && coordinate % 256 != 0);
}

static void jb2_grid(const pdf_jb2_halftone *p, uint32_t n, uint32_t m, int64_t *x, int64_t *y) {
    *x = jb2_grid_floor((int64_t)p->grid_x + (int64_t)m * p->vector_y + (int64_t)n * p->vector_x);
    *y = jb2_grid_floor((int64_t)p->grid_y + (int64_t)m * p->vector_x - (int64_t)n * p->vector_y);
}

int cupidimage_pdf_jb2_halftone_decode(pdf_jb2_context *context, const unsigned char *data,
                                       size_t length, const pdf_jb2_halftone *p,
                                       pdf_jb2_bitmap **output) {
    if (!output)
        return cupidimage_pdf_jb2_fail(context);
    *output = NULL;
    if (!p || !p->patterns || !p->patterns->count || p->patterns->count > 65536 ||
        !p->patterns->patterns || p->mmr > 1 || p->template_id > 3 || p->skip > 1 ||
        (p->mmr && (p->template_id || p->skip)) || p->combination > 4 || p->default_pixel > 1 ||
        p->grid_width > 65535 || p->grid_height > 65535 ||
        (uint64_t)p->grid_width * p->grid_height > PDF_JB2_PIXEL_LIMIT ||
        length > PDF_JB2_INPUT_LIMIT || (length && !data))
        return cupidimage_pdf_jb2_fail(context);
    pdf_jb2_bitmap *bitmap =
        cupidimage_pdf_jb2_bitmap_new(context, p->width, p->height, p->default_pixel);
    size_t cells = (size_t)p->grid_width * p->grid_height;
    uint16_t *values = cupidimage_pdf_jb2_alloc(context, cells, sizeof(*values));
    if (!bitmap || !values)
        return 0;
    pdf_jb2_bitmap *skip = NULL;
    if (p->skip) {
        skip = cupidimage_pdf_jb2_bitmap_new(context, p->grid_width, p->grid_height, 0);
        if (!skip || !cupidimage_pdf_jb2_take(context, (uint64_t)cells * 3))
            return 0;
        for (uint32_t m = 0; m < p->grid_height; m++)
            for (uint32_t n = 0; n < p->grid_width; n++) {
                int64_t x, y;
                jb2_grid(p, n, m, &x, &y);
                unsigned outside = x + p->patterns->width <= 0 || x >= p->width ||
                                   y + p->patterns->height <= 0 || y >= p->height;
                cupidimage_pdf_jb2_pixel_set(skip, n, m, outside);
            }
    }
    unsigned planes = 0;
    while ((1u << planes) < p->patterns->count)
        planes++;
    pdf_jb2_arithmetic decoder;
    unsigned char *states = NULL;
    size_t states_count = cupidimage_pdf_jb2_generic_contexts(p->template_id);
    if (planes && !p->mmr) {
        states = cupidimage_pdf_jb2_alloc(context, states_count, 1);
        if (!states || !cupidimage_pdf_jb2_arithmetic_init(&decoder, context, data, length))
            return 0;
    }
    size_t offset = 0;
    for (unsigned bit = planes; bit;) {
        bit--;
        pdf_jb2_bitmap *plane = NULL;
        if (p->mmr) {
            size_t consumed;
            if (!cupidimage_pdf_jb2_mmr(context, data ? data + offset : NULL, length - offset,
                                        p->grid_width, p->grid_height, 1, &consumed, &plane))
                return 0;
            offset += consumed;
        } else {
            pdf_jb2_generic generic;
            cupidimage_pdf_jb2_generic_defaults(&generic, p->template_id, 0);
            generic.width = p->grid_width;
            generic.height = p->grid_height;
            generic.skip = skip;
            if (!cupidimage_pdf_jb2_generic_decode(context, &generic, &decoder, states,
                                                   states_count, &plane))
                return 0;
        }
        if (!cupidimage_pdf_jb2_take(context, (uint64_t)cells * 2))
            return 0;
        for (uint32_t m = 0; m < p->grid_height; m++)
            for (uint32_t n = 0; n < p->grid_width; n++) {
                size_t index = (size_t)m * p->grid_width + n;
                unsigned decoded = cupidimage_pdf_jb2_pixel(plane, n, m);
                unsigned higher = (values[index] >> (bit + 1)) & 1u;
                values[index] |= (uint16_t)((decoded ^ higher) << bit);
            }
        if (!cupidimage_pdf_jb2_release(context, plane->data) ||
            !cupidimage_pdf_jb2_release(context, plane))
            return 0;
    }
    if (p->mmr && offset != length)
        return cupidimage_pdf_jb2_fail(context);
    if (!cupidimage_pdf_jb2_take(context, (uint64_t)cells * 3))
        return 0;
    for (uint32_t m = 0; m < p->grid_height; m++)
        for (uint32_t n = 0; n < p->grid_width; n++) {
            unsigned index = values[(size_t)m * p->grid_width + n];
            if (index >= p->patterns->count || !p->patterns->patterns[index])
                return cupidimage_pdf_jb2_fail(context);
            int64_t x, y;
            jb2_grid(p, n, m, &x, &y);
            if (!cupidimage_pdf_jb2_blit(context, bitmap, p->patterns->patterns[index], x, y,
                                         p->combination))
                return 0;
        }
    if (!cupidimage_pdf_jb2_release(context, values) ||
        !cupidimage_pdf_jb2_release(context, states))
        return 0;
    if (skip && (!cupidimage_pdf_jb2_release(context, skip->data) ||
                 !cupidimage_pdf_jb2_release(context, skip)))
        return 0;
    *output = bitmap;
    return 1;
}
