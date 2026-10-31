#include "symbols.h"
#include <limits.h>
#include <string.h>

static int jb2_dictionary_bitmap(pdf_jb2_coding *coding, const pdf_jb2_dictionary *p,
                                 pdf_jb2_bitmap **symbols, uint32_t count, uint32_t width,
                                 uint32_t height, pdf_jb2_bitmap **output) {
    if (!p->aggregate) {
        pdf_jb2_generic generic = p->generic;
        generic.width = width;
        generic.height = height;
        generic.typical = 0;
        generic.skip = NULL;
        return cupidimage_pdf_jb2_generic_decode(coding->context, &generic, &coding->arithmetic,
                                                 p->generic_states, p->generic_count, output);
    }
    int64_t instances;
    if (!cupidimage_pdf_jb2_integer_required(coding, JB2_AI, p->aggregate_count, &instances) ||
        instances < 1 || instances > 1048576)
        return cupidimage_pdf_jb2_fail(coding->context);
    if (instances == 1) {
        uint32_t id;
        int64_t dx, dy;
        if (!cupidimage_pdf_jb2_symbol_id(coding, &id) || id >= count || !symbols[id] ||
            !cupidimage_pdf_jb2_integer_required(coding, JB2_RDX, p->standard15, &dx) ||
            !cupidimage_pdf_jb2_integer_required(coding, JB2_RDY, p->standard15, &dy))
            return cupidimage_pdf_jb2_fail(coding->context);
        pdf_jb2_refinement refinement = p->refinement;
        refinement.width = width;
        refinement.height = height;
        refinement.dx = (int32_t)dx;
        refinement.dy = (int32_t)dy;
        refinement.reference = symbols[id];
        refinement.typical = 0;
        return cupidimage_pdf_jb2_refine_symbol(coding, &refinement, p->refinement_states,
                                                p->refinement_count, p->standard1, output);
    }
    pdf_jb2_text text = {.width = width,
                         .height = height,
                         .instances = (uint32_t)instances,
                         .symbol_count = count,
                         .symbols = symbols,
                         .refine = 1,
                         .corner = 1,
                         .fs = p->standard6,
                         .ds = p->standard8,
                         .dt = p->standard11,
                         .rdw = p->standard15,
                         .rdh = p->standard15,
                         .rdx = p->standard15,
                         .rdy = p->standard15,
                         .rsize = p->standard1,
                         .refinement = p->refinement,
                         .refinement_states = p->refinement_states,
                         .refinement_count = p->refinement_count};
    return cupidimage_pdf_jb2_text_decode(coding, &text, output);
}

static int jb2_collective(pdf_jb2_coding *coding, const pdf_jb2_dictionary *p, uint32_t width,
                          uint32_t height, pdf_jb2_bitmap **output) {
    *output = NULL;
    int64_t length;
    if (!cupidimage_pdf_jb2_integer_required(coding, JB2_AI, p->bitmap_size, &length) ||
        length < 0 || !cupidimage_pdf_jb2_align(&coding->bits))
        return cupidimage_pdf_jb2_fail(coding->context);
    size_t available = coding->bits.length - coding->bits.bit / 8;
    const unsigned char *data = coding->bits.data + coding->bits.bit / 8;
    if (length) {
        size_t consumed;
        if ((uint64_t)length > available ||
            !cupidimage_pdf_jb2_mmr(coding->context, data, (size_t)length, width, height, 0,
                                    &consumed, output))
            return cupidimage_pdf_jb2_fail(coding->context);
        coding->bits.bit += consumed * 8;
        return 1;
    }
    pdf_jb2_bitmap *bitmap = cupidimage_pdf_jb2_bitmap_new(coding->context, width, height, 0);
    if (!bitmap)
        return 0;
    size_t bytes = bitmap->stride * bitmap->height;
    if (bytes > available || !cupidimage_pdf_jb2_take(coding->context, bytes))
        return cupidimage_pdf_jb2_fail(coding->context);
    if (bytes)
        memcpy(bitmap->data, data, bytes);
    if (width % 8) {
        unsigned padding = (1u << (8 - width % 8)) - 1;
        for (uint32_t y = 0; y < height; y++)
            if (bitmap->data[y * bitmap->stride + bitmap->stride - 1] & padding)
                return cupidimage_pdf_jb2_fail(coding->context);
    }
    coding->bits.bit += bytes * 8;
    *output = bitmap;
    return 1;
}

int cupidimage_pdf_jb2_dictionary_decode(pdf_jb2_coding *coding, const pdf_jb2_dictionary *p,
                                         pdf_jb2_bitmap ***output) {
    if (!coding || !output)
        return 0;
    *output = NULL;
    pdf_jb2_context *context = coding->context;
    if (!p || p->aggregate > 1 || p->imported_count > 65536 ||
        p->new_count > 65536 - p->imported_count ||
        p->exported_count > p->imported_count + p->new_count || (p->imported_count && !p->imported))
        return cupidimage_pdf_jb2_fail(context);
    uint32_t total = p->imported_count + p->new_count;
    pdf_jb2_bitmap **symbols = cupidimage_pdf_jb2_alloc(context, total, sizeof(*symbols));
    uint32_t *widths = cupidimage_pdf_jb2_alloc(context, p->new_count, sizeof(*widths));
    pdf_jb2_bitmap **exported =
        cupidimage_pdf_jb2_alloc(context, p->exported_count, sizeof(*exported));
    if (!symbols || !widths || !exported)
        return 0;
    if (p->imported_count)
        memcpy(symbols, p->imported, p->imported_count * sizeof(*symbols));
    uint32_t decoded = 0;
    int64_t height = 0;
    while (decoded < p->new_count) {
        int64_t delta;
        if (!cupidimage_pdf_jb2_take(context, 1) ||
            !cupidimage_pdf_jb2_integer_required(coding, JB2_DH, p->dh, &delta))
            return 0;
        height += delta;
        if (height < 0 || height > 65535)
            return cupidimage_pdf_jb2_fail(context);
        int64_t width = 0;
        uint64_t total_width = 0;
        uint32_t first = decoded;
        for (;;) {
            int oob;
            if (!cupidimage_pdf_jb2_integer(coding, JB2_DW, p->dw, &delta, &oob))
                return 0;
            if (oob)
                break;
            width += delta;
            if (width < 0 || width > 65535 || decoded == p->new_count)
                return cupidimage_pdf_jb2_fail(context);
            total_width += (uint64_t)width;
            widths[decoded] = (uint32_t)width;
            if (!coding->huffman || p->aggregate) {
                if (!jb2_dictionary_bitmap(coding, p, symbols, p->imported_count + decoded,
                                           (uint32_t)width, (uint32_t)height,
                                           symbols + p->imported_count + decoded))
                    return 0;
            }
            decoded++;
        }
        if (coding->huffman && !p->aggregate) {
            if (total_width > 65535)
                return cupidimage_pdf_jb2_fail(context);
            pdf_jb2_bitmap *collective;
            if (!jb2_collective(coding, p, (uint32_t)total_width, (uint32_t)height, &collective))
                return 0;
            int64_t offset = 0;
            for (uint32_t i = first; i < decoded; i++) {
                pdf_jb2_bitmap *symbol =
                    cupidimage_pdf_jb2_bitmap_new(context, widths[i], (uint32_t)height, 0);
                if (!symbol || !cupidimage_pdf_jb2_blit(context, symbol, collective, -offset, 0, 4))
                    return 0;
                symbols[p->imported_count + i] = symbol;
                offset += widths[i];
            }
            if (!cupidimage_pdf_jb2_release(context, collective->data) ||
                !cupidimage_pdf_jb2_release(context, collective))
                return 0;
        }
    }
    uint32_t index = 0, exported_count = 0;
    unsigned flag = 0;
    do {
        int64_t run;
        if (!cupidimage_pdf_jb2_take(context, 1) ||
            !cupidimage_pdf_jb2_integer_required(coding, JB2_EX, p->standard1, &run) || run < 0 ||
            (uint64_t)run > total - index ||
            (flag && (uint64_t)run > p->exported_count - exported_count))
            return cupidimage_pdf_jb2_fail(context);
        if (!cupidimage_pdf_jb2_take(context, (uint64_t)run))
            return 0;
        for (uint32_t i = 0; i < (uint32_t)run; i++, index++)
            if (flag) {
                if (!symbols[index])
                    return cupidimage_pdf_jb2_fail(context);
                exported[exported_count++] = symbols[index];
            }
        flag ^= 1;
    } while (index < total);
    if (exported_count != p->exported_count || !cupidimage_pdf_jb2_release(context, symbols) ||
        !cupidimage_pdf_jb2_release(context, widths))
        return cupidimage_pdf_jb2_fail(context);
    *output = exported;
    return 1;
}
