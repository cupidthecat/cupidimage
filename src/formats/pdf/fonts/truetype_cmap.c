#include "truetype.h"

static unsigned cmap_u16(const unsigned char *p) {
    return (unsigned)p[0] * 256 + p[1];
}

static uint32_t cmap_u32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static unsigned tt_cmap(const unsigned char *p, size_t length, uint32_t code) {
    if (length < 4)
        return 0;
    unsigned format = cmap_u16(p);
    size_t size;
    if (format == 8 || format == 10 || format == 12 || format == 13) {
        if (length < 8 || cmap_u16(p + 2))
            return 0;
        size = cmap_u32(p + 4);
    } else
        size = cmap_u16(p + 2);
    if (size > length)
        return 0;
    if (format == 0)
        return size >= 262 && code < 256 ? p[6 + code] : 0;
    if (format == 2) {
        if (size < 526 || code > 65535)
            return 0;
        unsigned maximum = 0;
        for (unsigned i = 0; i < 256; i++) {
            unsigned key = cmap_u16(p + 6 + i * 2);
            if (key % 8)
                return 0;
            if (key > maximum)
                maximum = key;
        }
        size_t array_start = 518 + (size_t)maximum + 8;
        if (array_start > size)
            return 0;
        unsigned high = code >> 8, low = code & 255;
        unsigned key = cmap_u16(p + 6 + (high ? high : low) * 2);
        if ((!high && key) || (high && !key))
            return 0;
        size_t sub = 518 + key;
        unsigned first = cmap_u16(p + sub), count = cmap_u16(p + sub + 2);
        unsigned delta = cmap_u16(p + sub + 4), offset = cmap_u16(p + sub + 6);
        if (first > 255 || count > 256 - first || low < first || low - first >= count || offset % 2)
            return 0;
        size_t at = sub + 6 + offset;
        if (at < array_start || at > size || count > (size - at) / 2)
            return 0;
        unsigned glyph = cmap_u16(p + at + (low - first) * 2);
        return glyph ? (glyph + delta) & 65535 : 0;
    }
    if (format == 10) {
        if (size < 20)
            return 0;
        uint32_t start = cmap_u32(p + 12), count = cmap_u32(p + 16);
        if (count > (size - 20) / 2 || (count && count - 1 > UINT32_MAX - start) || code < start ||
            code - start >= count)
            return 0;
        return cmap_u16(p + 20 + (size_t)(code - start) * 2);
    }
    if (format == 6) {
        if (size < 10)
            return 0;
        unsigned start = cmap_u16(p + 6), count = cmap_u16(p + 8);
        return size >= 10 + (size_t)count * 2 && code >= start && code - start < count
                   ? cmap_u16(p + 10 + (code - start) * 2)
                   : 0;
    }
    if (format == 4) {
        if (size < 16 || code > 65535)
            return 0;
        unsigned segments = cmap_u16(p + 6) / 2;
        if (!segments || cmap_u16(p + 6) % 2 || size < 16 + (size_t)segments * 8)
            return 0;
        const unsigned char *ends = p + 14, *starts = ends + 2 * segments + 2,
                            *deltas = starts + 2 * segments, *offsets = deltas + 2 * segments;
        unsigned lo = 0, hi = segments;
        while (lo < hi) {
            unsigned mid = lo + (hi - lo) / 2;
            if (cmap_u16(ends + mid * 2) < code)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo == segments || code < cmap_u16(starts + lo * 2))
            return 0;
        unsigned delta = cmap_u16(deltas + lo * 2), offset = cmap_u16(offsets + lo * 2);
        if (!offset)
            return (code + delta) & 65535;
        size_t at =
            (size_t)(offsets - p) + lo * 2 + offset + (code - cmap_u16(starts + lo * 2)) * 2;
        if (at > size || size - at < 2)
            return 0;
        unsigned glyph = cmap_u16(p + at);
        return glyph ? (glyph + delta) & 65535 : 0;
    }
    if (format == 8 || format == 12 || format == 13) {
        size_t header = format == 8 ? 8208 : 16;
        if (size < header)
            return 0;
        unsigned count = cmap_u32(p + header - 4);
        if (count > (size - header) / 12)
            return 0;
        unsigned lo = 0, hi = count;
        while (lo < hi) {
            unsigned mid = lo + (hi - lo) / 2;
            if (cmap_u32(p + header + (size_t)mid * 12 + 4) < code)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo == count)
            return 0;
        const unsigned char *group = p + header + (size_t)lo * 12;
        uint32_t start = cmap_u32(group), end = cmap_u32(group + 4), glyph = cmap_u32(group + 8);
        if (code < start || code > end)
            return 0;
        uint64_t result = (uint64_t)glyph + (format == 13 ? 0 : code - start);
        return result <= 65535 ? (unsigned)result : 0;
    }
    return 0;
}

int cupidimage_pdf_tt_has_cmap(const pdf_tt_font *font, unsigned platform, unsigned encoding) {
    const unsigned char *data = font->cmap.data;
    size_t length = font->cmap.length;
    if (length < 4 || cmap_u16(data))
        return 0;
    unsigned count = cmap_u16(data + 2);
    if (count > (length - 4) / 8)
        return 0;
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *entry = data + 4 + i * 8;
        size_t offset = cmap_u32(entry + 4);
        if (cmap_u16(entry) == platform && cmap_u16(entry + 2) == encoding && offset < length)
            return 1;
    }
    return 0;
}

unsigned cupidimage_pdf_tt_map(const pdf_tt_font *f, uint32_t code, int platform, int encoding) {
    const unsigned char *p = f->cmap.data;
    size_t length = f->cmap.length;
    if (length < 4 || cmap_u16(p))
        return 0;
    unsigned count = cmap_u16(p + 2);
    if (count > (length - 4) / 8)
        return 0;
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *entry = p + 4 + i * 8;
        unsigned plat = cmap_u16(entry), enc = cmap_u16(entry + 2);
        size_t at = cmap_u32(entry + 4);
        if ((platform < 0 || plat == (unsigned)platform) &&
            (encoding < 0 || enc == (unsigned)encoding) && at < length) {
            unsigned glyph = tt_cmap(p + at, length - at, code);
            if (glyph && glyph < f->glyphs)
                return glyph;
        }
    }
    return 0;
}
