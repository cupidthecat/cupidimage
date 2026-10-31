#include "symbols.h"
#include <limits.h>

static int jb2_coordinate(pdf_jb2_context *context, int64_t value) {
    return (value >= INT32_MIN && value <= INT32_MAX) || cupidimage_pdf_jb2_fail(context);
}

static int64_t jb2_half_floor(int64_t value) {
    return value / 2 - (value < 0 && value % 2 != 0);
}

static int jb2_instance(pdf_jb2_coding *coding, const pdf_jb2_text *parameters,
                        pdf_jb2_bitmap **result, int *temporary) {
    *result = NULL;
    *temporary = 0;
    uint32_t id;
    if (!cupidimage_pdf_jb2_symbol_id(coding, &id) || id >= parameters->symbol_count ||
        !parameters->symbols[id])
        return cupidimage_pdf_jb2_fail(coding->context);
    pdf_jb2_bitmap *original = parameters->symbols[id];
    int64_t refine = 0;
    if (parameters->refine) {
        if (coding->huffman) {
            uint32_t bit;
            if (!cupidimage_pdf_jb2_read(&coding->bits, 1, &bit))
                return 0;
            refine = bit;
        } else if (!cupidimage_pdf_jb2_integer_required(coding, JB2_RI, NULL, &refine))
            return 0;
        if (refine < 0 || refine > 1)
            return cupidimage_pdf_jb2_fail(coding->context);
    }
    if (!refine) {
        *result = original;
        return 1;
    }
    int64_t dw, dh, dx, dy;
    if (!cupidimage_pdf_jb2_integer_required(coding, JB2_RDW, parameters->rdw, &dw) ||
        !cupidimage_pdf_jb2_integer_required(coding, JB2_RDH, parameters->rdh, &dh) ||
        !cupidimage_pdf_jb2_integer_required(coding, JB2_RDX, parameters->rdx, &dx) ||
        !cupidimage_pdf_jb2_integer_required(coding, JB2_RDY, parameters->rdy, &dy))
        return 0;
    int64_t width = original->width + dw, height = original->height + dh;
    dx += jb2_half_floor(dw);
    dy += jb2_half_floor(dh);
    if (width < 0 || width > 65535 || height < 0 || height > 65535 ||
        !jb2_coordinate(coding->context, dx) || !jb2_coordinate(coding->context, dy))
        return cupidimage_pdf_jb2_fail(coding->context);
    pdf_jb2_refinement refinement = parameters->refinement;
    refinement.width = (uint32_t)width;
    refinement.height = (uint32_t)height;
    refinement.dx = (int32_t)dx;
    refinement.dy = (int32_t)dy;
    refinement.reference = original;
    refinement.typical = 0;
    *temporary = 1;
    return cupidimage_pdf_jb2_refine_symbol(coding, &refinement, parameters->refinement_states,
                                            parameters->refinement_count, parameters->rsize,
                                            result);
}

int cupidimage_pdf_jb2_text_decode(pdf_jb2_coding *coding, const pdf_jb2_text *p,
                                   pdf_jb2_bitmap **output) {
    if (!coding || !output)
        return 0;
    *output = NULL;
    pdf_jb2_context *context = coding->context;
    if (!p || p->instances > 1048576 || p->symbol_count > 65536 ||
        (p->symbol_count && !p->symbols) || (p->instances && !p->symbol_count) || p->refine > 1 ||
        p->strips_log > 3 || p->corner > 3 || p->transposed > 1 || p->combination > 3 ||
        p->default_pixel > 1 || p->ds_offset < -16 || p->ds_offset > 15)
        return cupidimage_pdf_jb2_fail(context);
    pdf_jb2_bitmap *bitmap =
        cupidimage_pdf_jb2_bitmap_new(context, p->width, p->height, p->default_pixel);
    if (!bitmap)
        return 0;
    int64_t initial;
    if (!cupidimage_pdf_jb2_integer_required(coding, JB2_DT, p->dt, &initial))
        return 0;
    unsigned strips = 1u << p->strips_log;
    int64_t strip = -initial * strips, first = 0;
    if (!jb2_coordinate(context, strip))
        return 0;
    uint32_t instances = 0;
    while (instances < p->instances) {
        int64_t delta, current;
        if (!cupidimage_pdf_jb2_take(context, 1) ||
            !cupidimage_pdf_jb2_integer_required(coding, JB2_DT, p->dt, &delta))
            return 0;
        strip += delta * strips;
        if (!jb2_coordinate(context, strip) ||
            !cupidimage_pdf_jb2_integer_required(coding, JB2_FS, p->fs, &delta))
            return 0;
        first += delta;
        if (!jb2_coordinate(context, first))
            return 0;
        current = first;
        for (;;) {
            if (instances >= p->instances || !cupidimage_pdf_jb2_take(context, 1))
                return cupidimage_pdf_jb2_fail(context);
            int64_t within = 0;
            if (p->strips_log) {
                if (coding->huffman) {
                    uint32_t value;
                    if (!cupidimage_pdf_jb2_read(&coding->bits, p->strips_log, &value))
                        return 0;
                    within = value;
                } else if (!cupidimage_pdf_jb2_integer_required(coding, JB2_IT, NULL, &within))
                    return 0;
                if (within < 0 || within >= strips)
                    return cupidimage_pdf_jb2_fail(context);
            }
            int64_t t = strip + within;
            if (!jb2_coordinate(context, t))
                return 0;
            pdf_jb2_bitmap *symbol;
            int temporary;
            if (!jb2_instance(coding, p, &symbol, &temporary))
                return 0;
            int right = p->corner >= 2, bottom = !(p->corner & 1);
            int before = p->transposed ? bottom : right;
            int64_t advance = (p->transposed ? (int64_t)symbol->height : symbol->width) - 1;
            if (before)
                current += advance;
            if (!jb2_coordinate(context, current))
                return 0;
            int64_t x = p->transposed ? t : current, y = p->transposed ? current : t;
            if (right)
                x -= (int64_t)symbol->width - 1;
            if (bottom)
                y -= (int64_t)symbol->height - 1;
            if (!cupidimage_pdf_jb2_blit(context, bitmap, symbol, x, y, p->combination))
                return 0;
            if (!before)
                current += advance;
            if (!jb2_coordinate(context, current))
                return 0;
            if (temporary && (!cupidimage_pdf_jb2_release(context, symbol->data) ||
                              !cupidimage_pdf_jb2_release(context, symbol)))
                return 0;
            instances++;
            int oob;
            if (!cupidimage_pdf_jb2_integer(coding, JB2_DS, p->ds, &delta, &oob))
                return 0;
            if (oob)
                break;
            current += delta + p->ds_offset;
            if (!jb2_coordinate(context, current))
                return 0;
        }
    }
    *output = bitmap;
    return 1;
}
