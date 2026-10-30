#include "encoding.h"
#include "encoding_data.h"
#include "glyph_names_data.h"
#include <stddef.h>
#include <string.h>

static int encoding_hex(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

int cupidimage_pdf_glyph_scalar(const char *name, uint32_t *out) {
    char base[128];
    size_t n = 0;
    while (name[n] && name[n] != '.') {
        if (n == 127 || name[n] == '_')
            return 0;
        base[n] = name[n];
        n++;
    }
    base[n] = 0;
    size_t lo = 0, hi = sizeof(pdf_glyph_names) / sizeof(pdf_glyph_names[0]);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = strcmp(base, pdf_glyph_names[mid].name);
        if (!c) {
            *out = pdf_glyph_names[mid].scalar;
            return pdf_glyph_names[mid].single;
        }
        if (c > 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    size_t first = 0;
    if (n == 7 && !memcmp(base, "uni", 3))
        first = 3;
    else if (n >= 5 && n <= 7 && base[0] == 'u')
        first = 1;
    else
        return 0;
    uint32_t scalar = 0;
    for (size_t i = first; i < n; i++) {
        int v = encoding_hex(base[i]);
        if (v < 0)
            return 0;
        scalar = scalar * 16 + (unsigned)v;
    }
    *out = scalar;
    return scalar <= 0x10ffff && (scalar < 0xd800 || scalar > 0xdfff);
}

uint32_t cupidimage_pdf_glyph_unicode(const char *name) {
    uint32_t scalar = 0;
    return cupidimage_pdf_glyph_scalar(name, &scalar) ? scalar : 0;
}

const char *cupidimage_pdf_encoding_name(const char *encoding, unsigned code) {
    if (code > 255)
        return ".notdef";
    if (!strcmp(encoding, "MacRomanEncoding"))
        return pdf_MacRomanEncoding[code];
    if (!strcmp(encoding, "MacExpertEncoding"))
        return pdf_MacExpertEncoding[code];
    if (!strcmp(encoding, "WinAnsiEncoding"))
        return pdf_WinAnsiEncoding[code];
    return pdf_StandardEncoding[code];
}

unsigned cupidimage_pdf_mac_roman_code(uint32_t scalar) {
    static const struct {
        uint32_t scalar;
        unsigned code;
    } extra[] = {{0x2260, 173}, {0x221e, 176}, {0x2264, 178}, {0x2265, 179}, {0x2202, 182},
                 {0x2211, 183}, {0x220f, 184}, {0x03c0, 185}, {0x222b, 186}, {0x03a9, 189},
                 {0x2126, 189}, {0x221a, 195}, {0x2248, 197}, {0x2206, 198}, {0x25ca, 215},
                 {0x20ac, 219}, {0xf8ff, 240}};

    for (size_t i = 0; i < sizeof(extra) / sizeof(extra[0]); i++)
        if (extra[i].scalar == scalar)
            return extra[i].code;
    if (!scalar)
        return 256;
    for (unsigned code = 0; code < 256; code++)
        if (code != 219 && cupidimage_pdf_glyph_unicode(pdf_MacRomanEncoding[code]) == scalar)
            return code;
    return 256;
}
