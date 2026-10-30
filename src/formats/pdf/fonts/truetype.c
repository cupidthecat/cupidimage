#include "truetype_data.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned tt_u16(const unsigned char *p) {
    return (unsigned)p[0] * 256 + p[1];
}

static int tt_i16(const unsigned char *p) {
    unsigned v = tt_u16(p);
    return v < 32768 ? (int)v : (int)v - 65536;
}

static uint32_t tt_u32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static size_t tt_location(const pdf_tt_font *f, unsigned glyph) {
    const unsigned char *p = f->loca.data + (size_t)glyph * f->location_size;
    return f->location_size == 2 ? (size_t)tt_u16(p) * 2 : tt_u32(p);
}

int cupidimage_pdf_tt_init(pdf_tt_font *f, const unsigned char *data, size_t length) {
    memset(f, 0, sizeof(*f));
    if (length < 12 || (tt_u32(data) != 0x10000 && memcmp(data, "true", 4)))
        return 0;
    unsigned count = tt_u16(data + 4);
    if (!count || count > 4096 || count > (length - 12) / 16)
        return 0;
    const char *tags[] = {"head", "maxp", "loca", "glyf", "hhea", "hmtx", "cmap",
                          "post", "cvt ", "fpgm", "prep", "OS/2", "vhea", "vmtx"};
    pdf_tt_table *tables[] = {&f->head, &f->maxp, &f->loca, &f->glyf, &f->hhea, &f->hmtx, &f->cmap,
                              &f->post, &f->cvt,  &f->fpgm, &f->prep, &f->os2,  &f->vhea, &f->vmtx};
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *record = data + 12 + i * 16;
        size_t offset = tt_u32(record + 8), size = tt_u32(record + 12);
        if (offset > length || size > length - offset)
            return 0;
        for (unsigned j = 0; j < sizeof(tags) / sizeof(*tags); j++)
            if (!memcmp(record, tags[j], 4)) {
                if (tables[j]->data)
                    return 0;
                tables[j]->data = data + offset;
                tables[j]->length = size;
            }
    }
    if (f->head.length < 54 || f->maxp.length < 6 || f->hhea.length < 36 || !f->glyf.data ||
        tt_u32(f->head.data + 12) != 0x5f0f3cf5)
        return 0;
    f->units = tt_u16(f->head.data + 18);
    f->glyphs = tt_u16(f->maxp.data + 4);
    f->hmetrics = tt_u16(f->hhea.data + 34);
    unsigned format = tt_u16(f->head.data + 50);
    if (format > 1 || tt_u16(f->head.data + 52) || f->units < 16 || f->units > 16384 ||
        !f->glyphs || !f->hmetrics || f->hmetrics > f->glyphs)
        return 0;
    f->location_size = format ? 4 : 2;
    if (f->loca.length < (size_t)(f->glyphs + 1) * f->location_size ||
        f->hmtx.length < (size_t)f->hmetrics * 4 + (f->glyphs - f->hmetrics) * 2)
        return 0;
    size_t previous = 0;
    for (unsigned i = 0; i <= f->glyphs; i++) {
        size_t offset = tt_location(f, i);
        if (offset < previous || offset > f->glyf.length)
            return 0;
        previous = offset;
    }
    return 1;
}

unsigned cupidimage_pdf_tt_advance(const pdf_tt_font *f, unsigned glyph) {
    if (glyph >= f->glyphs)
        glyph = 0;
    if (glyph >= f->hmetrics)
        glyph = f->hmetrics - 1;
    return tt_u16(f->hmtx.data + glyph * 4);
}

void cupidimage_pdf_tt_outline_free(pdf_tt_outline *o) {
    free(o->points);
    memset(o, 0, sizeof(*o));
}

static int tt_reserve(pdf_tt_outline *o, unsigned added) {
    if (added > 65536 - o->count)
        return 0;
    unsigned need = o->count + added;
    if (need <= o->capacity)
        return 1;
    unsigned capacity = o->capacity ? o->capacity : 128;
    while (capacity < need)
        capacity *= 2;
    pdf_tt_point *p = realloc(o->points, (size_t)capacity * sizeof(*p));
    if (!p)
        return 0;
    o->points = p;
    o->capacity = capacity;
    return 1;
}

static int tt_simple(const unsigned char *p, size_t length, int contours, pdf_tt_outline *o,
                     pdf_tt_table *program, int *memory_failed) {
    if (contours > 32767 || length < 10 + (size_t)contours * 2)
        return 0;
    unsigned count = contours ? tt_u16(p + 10 + (contours - 1) * 2) + 1 : 0, previous = 0;
    for (int i = 0; i < contours; i++) {
        unsigned end = tt_u16(p + 10 + i * 2);
        if (i && end <= previous)
            return 0;
        previous = end;
    }
    size_t at = 10 + (size_t)contours * 2;
    if (!contours && at == length)
        return 1;
    if (length - at < 2)
        return 0;
    unsigned instructions = tt_u16(p + at);
    at += 2;
    if (instructions > length - at)
        return 0;
    if (program)
        *program = (pdf_tt_table){p + at, instructions};
    at += instructions;
    unsigned base = o->count;
    if (!tt_reserve(o, count)) {
        if (memory_failed)
            *memory_failed = 1;
        return 0;
    }
    for (unsigned i = 0; i < count;) {
        if (at == length)
            return 0;
        unsigned flags = p[at++], repeat = 0;
        if (flags & 0x80)
            return 0;
        if (flags & 8) {
            if (at == length)
                return 0;
            repeat = p[at++];
        }
        if (repeat >= count - i)
            return 0;
        do {
            o->points[base + i++] = (pdf_tt_point){0, 0, (unsigned char)flags, 0};
        } while (repeat--);
    }
    for (int axis = 0; axis < 2; axis++) {
        int value = 0;
        unsigned short_flag = axis ? 4 : 2, same_flag = axis ? 32 : 16;
        for (unsigned i = 0; i < count; i++) {
            unsigned flags = o->points[base + i].on;
            int delta = 0;
            if (flags & short_flag) {
                if (at == length)
                    return 0;
                delta = p[at++];
                if (!(flags & same_flag))
                    delta = -delta;
            } else if (!(flags & same_flag)) {
                if (length - at < 2)
                    return 0;
                delta = tt_i16(p + at);
                at += 2;
            }
            value += delta;
            if (value < -32768 || value > 32767)
                return 0;
            if (axis)
                o->points[base + i].y = value;
            else
                o->points[base + i].x = value;
        }
    }
    for (unsigned i = 0; i < count; i++)
        o->points[base + i].on &= 1;
    for (int i = 0; i < contours; i++)
        o->points[base + tt_u16(p + 10 + i * 2)].end = 1;
    o->count += count;
    return 1;
}

int cupidimage_pdf_tt_glyph_data(const pdf_tt_font *font, unsigned glyph, pdf_tt_table *view) {
    *view = (pdf_tt_table){0};
    if (glyph >= font->glyphs)
        return 0;
    size_t start = tt_location(font, glyph), end = tt_location(font, glyph + 1);
    if (start > end || end > font->glyf.length)
        return 0;
    *view = (pdf_tt_table){font->glyf.data + start, end - start};
    return 1;
}

int cupidimage_pdf_tt_simple_points(const pdf_tt_table *view, pdf_tt_outline *outline,
                                    pdf_tt_table *program) {
    outline->count = 0;
    outline->advance = 0;
    *program = (pdf_tt_table){0};
    if (!view->length)
        return 1;
    if (view->length < 10 || tt_i16(view->data) < 0)
        return 0;
    int memory_failed = 0;
    if (!tt_simple(view->data, view->length, tt_i16(view->data), outline, program,
                   &memory_failed)) {
        outline->count = 0;
        *program = (pdf_tt_table){0};
        return memory_failed ? -1 : 0;
    }
    return 1;
}

int cupidimage_pdf_tt_left_bearing(const pdf_tt_font *f, unsigned glyph) {
    size_t offset = glyph < f->hmetrics ? (size_t)glyph * 4 + 2
                                        : (size_t)f->hmetrics * 4 + (glyph - f->hmetrics) * 2;
    return tt_i16(f->hmtx.data + offset);
}

static int tt_glyph(const pdf_tt_font *f, unsigned glyph, pdf_tt_outline *o, unsigned chain[16],
                    unsigned depth, unsigned *calls, double *origin, unsigned *advance) {
    if (depth == 16 || glyph >= f->glyphs || !*calls)
        return 0;
    (*calls)--;
    for (unsigned i = 0; i < depth; i++)
        if (chain[i] == glyph)
            return 0;
    chain[depth] = glyph;
    size_t start = tt_location(f, glyph), end = tt_location(f, glyph + 1), length = end - start;
    *origin = -cupidimage_pdf_tt_left_bearing(f, glyph);
    *advance = cupidimage_pdf_tt_advance(f, glyph);
    if (!length)
        return 1;
    if (length < 10)
        return 0;
    const unsigned char *p = f->glyf.data + start;
    *origin = tt_i16(p + 2) - cupidimage_pdf_tt_left_bearing(f, glyph);
    int contours = tt_i16(p);
    if (contours >= 0)
        return tt_simple(p, length, contours, o, NULL, NULL);
    size_t at = 10;
    unsigned flags = 0, base = o->count, instructions = 0;
    pdf_tt_table view = {p, length};
    do {
        pdf_tt_component record;
        if (!cupidimage_pdf_tt_component_read(&view, &at, &record))
            return 0;
        flags = record.flags;
        instructions |= flags & 256;
        unsigned component = record.glyph;
        int32_t arg1 = record.argument[0], arg2 = record.argument[1];
        double a = record.matrix[0] / 16384.0, b = record.matrix[1] / 16384.0,
               c = record.matrix[2] / 16384.0, d = record.matrix[3] / 16384.0;
        unsigned child = o->count;
        double child_origin;
        unsigned child_advance;
        if (!tt_glyph(f, component, o, chain, depth + 1, calls, &child_origin, &child_advance))
            return 0;
        for (unsigned i = child; i < o->count; i++) {
            double x = o->points[i].x, y = o->points[i].y;
            o->points[i].x = a * x + c * y;
            o->points[i].y = b * x + d * y;
        }
        double dx = arg1, dy = arg2;
        if (flags & 2) {
            if (flags & 2048) {
                dx = a * arg1 + c * arg2;
                dy = b * arg1 + d * arg2;
            }
        } else {
            if ((unsigned)arg1 >= child - base || (unsigned)arg2 >= o->count - child)
                return 0;
            dx = o->points[base + arg1].x - o->points[child + arg2].x;
            dy = o->points[base + arg1].y - o->points[child + arg2].y;
        }
        if (flags & 512) {
            *origin = child_origin;
            *advance = child_advance;
        }
        for (unsigned i = child; i < o->count; i++) {
            o->points[i].x += dx;
            o->points[i].y += dy;
            if (!isfinite(o->points[i].x) || !isfinite(o->points[i].y) ||
                fabs(o->points[i].x) > 1e9 || fabs(o->points[i].y) > 1e9)
                return 0;
        }
    } while (flags & 32);
    pdf_tt_table program;
    if (!cupidimage_pdf_tt_component_program(&view, at, instructions != 0, &program))
        return 0;
    return 1;
}

int cupidimage_pdf_tt_outline(const pdf_tt_font *font, unsigned glyph, pdf_tt_outline *outline) {
    outline->count = 0;
    outline->advance = 0;
    unsigned chain[16], calls = 4096;
    double origin;
    if (!tt_glyph(font, glyph, outline, chain, 0, &calls, &origin, &outline->advance)) {
        outline->count = 0;
        outline->advance = 0;
        return 0;
    }
    /* The horizontal pen origin is the first phantom point, not necessarily x=0.
     * Normalize only the assembled glyph; component placement uses design coordinates. */
    for (unsigned i = 0; i < outline->count; i++)
        outline->points[i].x -= origin;
    return 1;
}
