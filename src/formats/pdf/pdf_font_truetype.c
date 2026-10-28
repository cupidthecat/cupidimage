#include "fonts/encoding.h"
#include "fonts/program.h"
#include "pdf_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned tt_named_glyph(const void *context, const char *name) {
    const pdf_tt_font *f = context;
    uint32_t scalar = 0;
    int mapped = cupidimage_pdf_glyph_scalar(name, &scalar);
    unsigned glyph = 0;
    int unicode = cupidimage_pdf_tt_has_cmap(f, 3, 1);
    int roman = cupidimage_pdf_tt_has_cmap(f, 1, 0);
    if (mapped && unicode)
        glyph = cupidimage_pdf_tt_map(f, scalar, 3, 1);
    else if (mapped && roman) {
        unsigned mac = cupidimage_pdf_mac_roman_code(scalar);
        if (mac < 256)
            glyph = cupidimage_pdf_tt_map(f, mac, 1, 0);
    }
    /* ISO 32000-1 9.6.6.4 requires named post-table lookup when the
     * selected Unicode or Roman subtable cannot map the glyph name. */
    if (!glyph && (unicode || roman))
        glyph = cupidimage_pdf_tt_name(f, name);
    if (!glyph && mapped) {
        glyph = cupidimage_pdf_tt_map(f, scalar, 3, 10);
        if (!glyph)
            glyph = cupidimage_pdf_tt_map(f, scalar, 0, -1);
        if (!glyph) {
            unsigned mac = cupidimage_pdf_mac_roman_code(scalar);
            if (mac < 256)
                glyph = cupidimage_pdf_tt_map(f, mac, 1, 0);
        }
    }
    if (!glyph)
        glyph = cupidimage_pdf_tt_name(f, name);
    return glyph;
}

int cupidimage_pdf_truetype_encoding(const unsigned char *data, size_t size,
                                     const pdf_object_view *view, const pdf_object_view *descriptor,
                                     pdf_font_program *program) {
    pdf_object_view encoding = {0};
    size_t p;
    double flags = 0;
    if (cupidimage_pdf_page_value(descriptor, "Flags", &p)) {
        pdf_object_view value = {0};
        size_t at = 0;
        int valid = cupidimage_pdf_dictionary_value(data, size, descriptor, "Flags", &value);
        cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &at);
        valid = valid && cupidimage_pdf_parse_pdf_number(value.data, value.len, &at, &flags) &&
                isfinite(flags) && flags >= 0 && flags <= 0x7fffffff && floor(flags) == flags;
        cupidimage_pdf_object_view_release(&value);
        if (!valid)
            return 0;
    }
    int symbolic = ((unsigned)flags & 4) != 0,
        has_encoding = cupidimage_pdf_optional_value(data, size, view, "Encoding", &encoding),
        ok = has_encoding >= 0;
    char base[128] = "StandardEncoding";
    size_t pos = 0;
    cupidimage_pdf_skip_ws_and_comments(encoding.data, encoding.len, &pos);
    int encoding_dictionary =
        encoding.len - pos >= 2 && encoding.data[pos] == '<' && encoding.data[pos + 1] == '<';
    if (ok && !symbolic && has_encoding) {
        if (pos < encoding.len && encoding.data[pos] == '/')
            ok = cupidimage_pdf_read_name(&encoding, &pos, base);
        else if (!encoding_dictionary)
            ok = 0;
        else if (cupidimage_pdf_page_value(&encoding, "BaseEncoding", &pos)) {
            pdf_object_view value = {0};
            size_t at = 0;
            int present =
                cupidimage_pdf_optional_value(data, size, &encoding, "BaseEncoding", &value);
            ok = present == 0 || (present > 0 && cupidimage_pdf_read_name(&value, &at, base));
            cupidimage_pdf_object_view_release(&value);
        }
        if (strcmp(base, "StandardEncoding") && strcmp(base, "WinAnsiEncoding") &&
            strcmp(base, "MacRomanEncoding"))
            ok = 0;
    }
    for (unsigned code = 0; ok && code < 256; code++) {
        unsigned glyph = 0;
        if (symbolic || !has_encoding) {
            const unsigned banks[] = {0, 0xf000, 0xf100, 0xf200};
            for (unsigned i = 0; i < 4 && !glyph; i++)
                glyph = cupidimage_pdf_tt_map(&program->font, banks[i] + code, 3, 0);
            if (!glyph)
                glyph = cupidimage_pdf_tt_map(&program->font, code, 1, 0);
            if (!glyph)
                glyph = cupidimage_pdf_tt_map(&program->font, code, 3, 1);
        } else {
            const char *name = cupidimage_pdf_encoding_name(base, code);
            if (!strcmp(name, ".notdef"))
                name = cupidimage_pdf_encoding_name("StandardEncoding", code);
            glyph = tt_named_glyph(&program->font, name);
        }
        program->glyphs[code] = glyph;
    }
    if (ok && !symbolic && has_encoding && encoding_dictionary)
        ok = cupidimage_pdf_encoding_differences(data, size, &encoding, program->glyphs,
                                                 &program->font, tt_named_glyph);
    cupidimage_pdf_object_view_release(&encoding);
    return ok;
}
