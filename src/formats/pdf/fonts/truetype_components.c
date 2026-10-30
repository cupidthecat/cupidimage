#include "truetype_data.h"

static unsigned tt_component_u16(const unsigned char *p) {
    return (unsigned)p[0] * 256 + p[1];
}

static int32_t tt_component_i16(const unsigned char *p) {
    unsigned value = tt_component_u16(p);
    return value < 32768 ? (int32_t)value : (int32_t)value - 65536;
}

int cupidimage_pdf_tt_component_read(const pdf_tt_table *view, size_t *offset,
                                     pdf_tt_component *component) {
    size_t at = *offset;
    if (at > view->length || view->length - at < 4)
        return 0;
    const unsigned char *data = view->data;
    unsigned flags = tt_component_u16(data + at);
    unsigned transforms = !!(flags & 8) + !!(flags & 64) + !!(flags & 128);
    if ((flags & 0xe000) || transforms > 1)
        return 0;
    /* The legacy bit 4 is ignored. Conflicting offset flags use the specified
     * default interpretation: unscaled offsets in the parent's coordinate space. */
    if ((flags & 0x1800) == 0x1800)
        flags &= ~0x800u;
    *component = (pdf_tt_component){
        .flags = flags, .glyph = tt_component_u16(data + at + 2), .matrix = {16384, 0, 0, 16384}};
    at += 4;
    for (unsigned axis = 0; axis < 2; axis++) {
        if (flags & 1) {
            if (view->length - at < 2)
                return 0;
            component->argument[axis] =
                flags & 2 ? tt_component_i16(data + at) : (int32_t)tt_component_u16(data + at);
            at += 2;
        } else {
            if (at == view->length)
                return 0;
            unsigned value = data[at++];
            component->argument[axis] =
                flags & 2 && value >= 128 ? (int32_t)value - 256 : (int32_t)value;
        }
    }
    unsigned entries = flags & 8 ? 1 : flags & 64 ? 2 : flags & 128 ? 4 : 0;
    if (entries > (view->length - at) / 2)
        return 0;
    if (entries == 1)
        component->matrix[0] = component->matrix[3] = tt_component_i16(data + at);
    else if (entries == 2) {
        component->matrix[0] = tt_component_i16(data + at);
        component->matrix[3] = tt_component_i16(data + at + 2);
    } else if (entries == 4)
        for (unsigned i = 0; i < 4; i++)
            component->matrix[i] = tt_component_i16(data + at + i * 2);
    *offset = at + entries * 2;
    return 1;
}

int cupidimage_pdf_tt_component_program(const pdf_tt_table *view, size_t at, int present,
                                        pdf_tt_table *program) {
    *program = (pdf_tt_table){0};
    if (at > view->length)
        return 0;
    if (!present)
        return 1;
    if (view->length - at < 2)
        return 0;
    unsigned length = tt_component_u16(view->data + at);
    at += 2;
    if (length > view->length - at)
        return 0;
    *program = (pdf_tt_table){view->data + at, length};
    return 1;
}
