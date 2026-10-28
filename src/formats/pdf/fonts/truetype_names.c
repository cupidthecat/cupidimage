#include "post_names.inc"
#include "truetype.h"
#include <string.h>

static unsigned post_u16(const unsigned char *p) {
    return (unsigned)p[0] * 256 + p[1];
}

static uint32_t post_u32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

unsigned cupidimage_pdf_tt_name(const pdf_tt_font *font, const char *name) {
    const unsigned char *p = font->post.data;
    size_t length = font->post.length;
    if (length < 32)
        return 0;
    unsigned standard = 258;
    for (unsigned i = 0; i < 258; i++)
        if (!strcmp(name, post_standard_names[i])) {
            standard = i;
            break;
        }
    uint32_t version = post_u32(p);
    if (version == 0x10000)
        return font->glyphs == 258 && standard < 258 ? standard : 0;
    if (version != 0x20000 && version != 0x25000)
        return 0;
    if (length < 34)
        return 0;
    unsigned count = post_u16(p + 32);
    if (count != font->glyphs)
        return 0;
    if (version == 0x25000) {
        if (count > 258 || count > length - 34 || standard == 258)
            return 0;
        unsigned found = count;
        for (unsigned i = 0; i < count; i++) {
            int offset = p[34 + i] < 128 ? p[34 + i] : (int)p[34 + i] - 256;
            int index = (int)i + offset;
            if (index < 0 || index >= 258)
                return 0;
            if ((unsigned)index == standard && found == count)
                found = i;
        }
        return found < count ? found : 0;
    }
    if (count > (length - 34) / 2)
        return 0;
    if (standard < 258) {
        for (unsigned i = 0; i < count; i++)
            if (post_u16(p + 34 + i * 2) == standard)
                return i;
    }
    size_t at = 34 + (size_t)count * 2, wanted_length = strlen(name);
    for (unsigned index = 258; index < 32768 && at < length; index++) {
        unsigned size = p[at++];
        if (size > length - at)
            return 0;
        if (wanted_length == size && !memcmp(p + at, name, size)) {
            for (unsigned i = 0; i < count; i++)
                if (post_u16(p + 34 + i * 2) == index)
                    return i;
        }
        at += size;
    }
    return 0;
}
