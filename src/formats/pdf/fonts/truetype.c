#include "truetype.h"
#include "encoding.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned tt_u16(const unsigned char *p) { return (unsigned)p[0] * 256 + p[1]; }
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
    const char *tags[] = {"head", "maxp", "loca", "glyf", "hhea", "hmtx", "cmap", "post"};
    pdf_tt_table *tables[] = {&f->head, &f->maxp, &f->loca, &f->glyf,
                              &f->hhea, &f->hmtx, &f->cmap, &f->post};
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *record = data + 12 + i * 16;
        size_t offset = tt_u32(record + 8), size = tt_u32(record + 12);
        if (offset > length || size > length - offset)
            return 0;
        for (unsigned j = 0; j < 8; j++)
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
static unsigned tt_cmap(const unsigned char *p, size_t length, uint32_t code) {
    if (length < 4)
        return 0;
    unsigned format = tt_u16(p);
    size_t size;
    if (format == 12 || format == 13) {
        if (length < 16)
            return 0;
        size = tt_u32(p + 4);
    } else
        size = tt_u16(p + 2);
    if (size > length)
        return 0;
    if (format == 0)
        return size >= 262 && code < 256 ? p[6 + code] : 0;
    if (format == 6) {
        if (size < 10)
            return 0;
        unsigned start = tt_u16(p + 6), count = tt_u16(p + 8);
        return size >= 10 + (size_t)count * 2 && code >= start && code - start < count
                   ? tt_u16(p + 10 + (code - start) * 2)
                   : 0;
    }
    if (format == 4) {
        if (size < 16 || code > 65535)
            return 0;
        unsigned segments = tt_u16(p + 6) / 2;
        if (!segments || tt_u16(p + 6) % 2 || size < 16 + (size_t)segments * 8)
            return 0;
        const unsigned char *ends = p + 14, *starts = ends + 2 * segments + 2,
                            *deltas = starts + 2 * segments, *offsets = deltas + 2 * segments;
        unsigned lo = 0, hi = segments;
        while (lo < hi) {
            unsigned mid = lo + (hi - lo) / 2;
            if (tt_u16(ends + mid * 2) < code)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo == segments || code < tt_u16(starts + lo * 2))
            return 0;
        unsigned delta = tt_u16(deltas + lo * 2), offset = tt_u16(offsets + lo * 2);
        if (!offset)
            return (code + delta) & 65535;
        size_t at = (size_t)(offsets - p) + lo * 2 + offset + (code - tt_u16(starts + lo * 2)) * 2;
        if (at > size || size - at < 2)
            return 0;
        unsigned glyph = tt_u16(p + at);
        return glyph ? (glyph + delta) & 65535 : 0;
    }
    if (format == 12 || format == 13) {
        unsigned count = tt_u32(p + 12);
        if (size < 16 || count > (size - 16) / 12)
            return 0;
        unsigned lo = 0, hi = count;
        while (lo < hi) {
            unsigned mid = lo + (hi - lo) / 2;
            if (tt_u32(p + 16 + mid * 12 + 4) < code)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo == count)
            return 0;
        const unsigned char *group = p + 16 + lo * 12;
        uint32_t start = tt_u32(group), end = tt_u32(group + 4), glyph = tt_u32(group + 8);
        if (code < start || code > end)
            return 0;
        uint64_t result = (uint64_t)glyph + (format == 12 ? code - start : 0);
        return result <= 65535 ? (unsigned)result : 0;
    }
    return 0;
}
unsigned cupidimage_pdf_tt_map(const pdf_tt_font *f, uint32_t code, int platform, int encoding) {
    const unsigned char *p = f->cmap.data;
    size_t length = f->cmap.length;
    if (length < 4)
        return 0;
    unsigned count = tt_u16(p + 2);
    if (count > (length - 4) / 8)
        return 0;
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *entry = p + 4 + i * 8;
        unsigned plat = tt_u16(entry), enc = tt_u16(entry + 2);
        size_t at = tt_u32(entry + 4);
        if ((platform < 0 || plat == (unsigned)platform) &&
            (encoding < 0 || enc == (unsigned)encoding) && at < length) {
            unsigned glyph = tt_cmap(p + at, length - at, code);
            if (glyph && glyph < f->glyphs)
                return glyph;
        }
    }
    return 0;
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
static int tt_simple(const unsigned char *p, size_t length, int contours, pdf_tt_outline *o) {
    if (contours > 4096 || length < 10 + (size_t)contours * 2)
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
    at += instructions;
    unsigned base = o->count;
    if (!tt_reserve(o, count))
        return 0;
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
static int tt_glyph(const pdf_tt_font *f, unsigned glyph, pdf_tt_outline *o, unsigned chain[16],
                    unsigned depth, unsigned *calls) {
    if (depth == 16 || glyph >= f->glyphs || !*calls)
        return 0;
    (*calls)--;
    for (unsigned i = 0; i < depth; i++)
        if (chain[i] == glyph)
            return 0;
    chain[depth] = glyph;
    size_t start = tt_location(f, glyph), end = tt_location(f, glyph + 1), length = end - start;
    if (!length)
        return 1;
    if (length < 10)
        return 0;
    const unsigned char *p = f->glyf.data + start;
    int contours = tt_i16(p);
    if (contours >= 0)
        return tt_simple(p, length, contours, o);
    if (contours != -1)
        return 0;
    size_t at = 10;
    unsigned flags = 0, base = o->count, instructions = 0;
    do {
        if (length - at < 4)
            return 0;
        flags = tt_u16(p + at);
        instructions |= flags & 256;
        unsigned component = tt_u16(p + at + 2);
        at += 4;
        if (flags & 0xe000)
            return 0;
        int arg1, arg2;
        if (flags & 1) {
            if (length - at < 4)
                return 0;
            arg1 = flags & 2 ? tt_i16(p + at) : (int)tt_u16(p + at);
            arg2 = flags & 2 ? tt_i16(p + at + 2) : (int)tt_u16(p + at + 2);
            at += 4;
        } else {
            if (length - at < 2)
                return 0;
            arg1 = p[at++];
            arg2 = p[at++];
            if (flags & 2) {
                if (arg1 > 127)
                    arg1 -= 256;
                if (arg2 > 127)
                    arg2 -= 256;
            }
        }
        double a = 1, b = 0, c = 0, d = 1;
        unsigned transforms = !!(flags & 8) + !!(flags & 64) + !!(flags & 128);
        if (transforms > 1 || (flags & 0x1800) == 0x1800)
            return 0;
        if (flags & 8) {
            if (length - at < 2)
                return 0;
            a = d = tt_i16(p + at) / 16384.0;
            at += 2;
        }
        if (flags & 64) {
            if (length - at < 4)
                return 0;
            a = tt_i16(p + at) / 16384.0;
            d = tt_i16(p + at + 2) / 16384.0;
            at += 4;
        }
        if (flags & 128) {
            if (length - at < 8)
                return 0;
            a = tt_i16(p + at) / 16384.0;
            b = tt_i16(p + at + 2) / 16384.0;
            c = tt_i16(p + at + 4) / 16384.0;
            d = tt_i16(p + at + 6) / 16384.0;
            at += 8;
        }
        unsigned child = o->count;
        if (!tt_glyph(f, component, o, chain, depth + 1, calls))
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
        for (unsigned i = child; i < o->count; i++) {
            o->points[i].x += dx;
            o->points[i].y += dy;
            if (!isfinite(o->points[i].x) || !isfinite(o->points[i].y) ||
                fabs(o->points[i].x) > 1e9 || fabs(o->points[i].y) > 1e9)
                return 0;
        }
    } while (flags & 32);
    if (instructions) {
        if (length - at < 2)
            return 0;
        unsigned size = tt_u16(p + at);
        at += 2;
        if (size > length - at)
            return 0;
    }
    return 1;
}
int cupidimage_pdf_tt_outline(const pdf_tt_font *font, unsigned glyph, pdf_tt_outline *outline) {
    outline->count = 0;
    unsigned chain[16], calls = 4096;
    if (!tt_glyph(font, glyph, outline, chain, 0, &calls)) {
        outline->count = 0;
        return 0;
    }
    return 1;
}

unsigned cupidimage_pdf_tt_name(const pdf_tt_font *f, const char *name) {
    const unsigned char *p = f->post.data;
    size_t length = f->post.length;
    if (length < 32)
        return 0;
    uint32_t version = tt_u32(p);
    if (version == 0x20000) {
        if (length < 34)
            return 0;
        unsigned count = tt_u16(p + 32);
        if (count > f->glyphs || count > (length - 34) / 2)
            return 0;
        unsigned wanted = 0;
        size_t at = 34 + (size_t)count * 2;
        unsigned index = 258;
        while (at < length) {
            unsigned size = p[at++];
            if (size > length - at)
                return 0;
            if (strlen(name) == size && !memcmp(p + at, name, size)) {
                wanted = index;
                break;
            }
            at += size;
            index++;
            if (index > 65535)
                return 0;
        }
        for (unsigned i = 0; i < count; i++) {
            unsigned id = tt_u16(p + 34 + i * 2);
            if (wanted && id == wanted)
                return i;
            if (id >= 3 && id <= 97 &&
                !strcmp(name, cupidimage_pdf_encoding_name("MacRomanEncoding", id + 29)))
                return i;
        }
    }
    return 0;
}
