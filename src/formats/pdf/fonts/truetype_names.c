#include "post_names.inc"
#include "truetype.h"
#include <string.h>

static unsigned post_u16(const unsigned char *p) {
    return (unsigned)p[0] * 256 + p[1];
}

static uint32_t post_u32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int post_character_code(const char *name, unsigned *code) {
    if (name[0] != 'a' || !name[1])
        return 0;
    unsigned value = 0;
    for (size_t i = 1; name[i]; i++) {
        unsigned char c = (unsigned char)name[i];
        unsigned digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else
            return 0;
        if (value > (65535u - digit) / 16)
            return 0;
        value = value * 16 + digit;
    }
    if (value == 65535)
        return 0;
    *code = value;
    return 1;
}

unsigned cupidimage_pdf_tt_name(const pdf_tt_font *font, const char *name) {
    const unsigned char *p = font->post.data;
    size_t length = font->post.length;
    if (length < 32)
        return 0;
    uint32_t version = post_u32(p);
    if (version == 0x40000) {
        unsigned code;
        if ((length - 32) % 2 || font->glyphs != (length - 32) / 2 ||
            !post_character_code(name, &code))
            return 0;
        for (unsigned i = 0; i < font->glyphs; i++)
            if (post_u16(p + 32 + (size_t)i * 2) == code)
                return i;
        return 0;
    }
    unsigned standard = 258;
    for (unsigned i = 0; i < 258; i++)
        if (!strcmp(name, post_standard_names[i])) {
            standard = i;
            break;
        }
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
    /* Unreferenced strings must not trigger another scan of every glyph.
     * One bit per 16-bit name index keeps lookup linear without allocation. */
    unsigned char referenced[65536 / 8] = {0};
    for (unsigned i = 0; i < count; i++) {
        unsigned index = post_u16(p + 34 + (size_t)i * 2);
        referenced[index / 8] |= (unsigned char)(1u << (index % 8));
    }
    size_t at = 34 + (size_t)count * 2, wanted_length = strlen(name);
    for (unsigned index = 258; index < 65536 && at < length; index++) {
        unsigned size = p[at++];
        if (size > length - at)
            return 0;
        if ((referenced[index / 8] & (1u << (index % 8))) && wanted_length == size &&
            !memcmp(p + at, name, size)) {
            for (unsigned i = 0; i < count; i++)
                if (post_u16(p + 34 + i * 2) == index)
                    return i;
        }
        at += size;
    }
    return 0;
}
