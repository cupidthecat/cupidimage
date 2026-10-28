#include "fonts/encoding.h"
#include "fonts/program.h"
#include "pdf_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned tt_named_glyph(const pdf_tt_font *f, const char *name) {
    uint32_t scalar = 0;
    int mapped = cupidimage_pdf_glyph_scalar(name, &scalar);
    unsigned glyph = 0;
    if (mapped) {
        glyph = scalar <= 65535 ? cupidimage_pdf_tt_map(f, scalar, 3, 1) : 0;
        if (!glyph)
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

static int tt_pdf_simple_encoding(const unsigned char *data, size_t size,
                                  const pdf_object_view *view, const pdf_object_view *descriptor,
                                  pdf_font_program *program) {
    pdf_object_view encoding = {0}, differences = {0};
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
    if (ok && !symbolic && has_encoding && encoding_dictionary &&
        cupidimage_pdf_page_value(&encoding, "Differences", &pos)) {
        int present =
            cupidimage_pdf_optional_value(data, size, &encoding, "Differences", &differences);
        if (present <= 0) {
            cupidimage_pdf_object_view_release(&encoding);
            return present == 0;
        }
        pos = 0;
        cupidimage_pdf_skip_ws_and_comments(differences.data, differences.len, &pos);
        ok = ok && pos < differences.len && differences.data[pos++] == '[';
        int code = -1;
        unsigned entries = 0;
        while (ok) {
            cupidimage_pdf_skip_ws_and_comments(differences.data, differences.len, &pos);
            if (pos < differences.len && differences.data[pos] == ']')
                break;
            if (++entries > 1024) {
                ok = 0;
                break;
            }
            if (pos < differences.len && differences.data[pos] == '/') {
                char name[128];
                ok = code >= 0 && code < 256 && cupidimage_pdf_read_name(&differences, &pos, name);
                if (ok)
                    program->glyphs[code++] = tt_named_glyph(&program->font, name);
            } else {
                double value;
                ok = cupidimage_pdf_parse_pdf_number(differences.data, differences.len, &pos,
                                                     &value) &&
                     value >= 0 && value <= 255 && floor(value) == value;
                if (ok)
                    code = (int)value;
            }
        }
    }
    cupidimage_pdf_object_view_release(&differences);
    cupidimage_pdf_object_view_release(&encoding);
    return ok;
}

static unsigned ps_named_glyph(const void *context, const char *name) {
    const pdf_font_program *program = context;
    return program->is_type1 ? cupidimage_pdf_type1_name(&program->type1, name)
                             : cupidimage_pdf_cff_name(&program->cff, name);
}

static int ps_pdf_encoding(const unsigned char *data, size_t size, const pdf_object_view *view,
                           pdf_font_program *program) {
    memcpy(program->glyphs, program->is_type1 ? program->type1.encoding : program->cff.encoding,
           sizeof(program->glyphs));
    return cupidimage_pdf_simple_encoding(data, size, view, program->glyphs, program,
                                          ps_named_glyph);
}

static uint32_t cff_sfnt_uint(const unsigned char *p) {
    return (uint32_t)p[0] * 16777216u + (uint32_t)p[1] * 65536u + (unsigned)p[2] * 256u + p[3];
}

static int cff_sfnt_table(const unsigned char *data, size_t length, pdf_cff_slice *table) {
    if (length < 12 || memcmp(data, "OTTO", 4))
        return 0;
    unsigned count = (unsigned)data[4] * 256 + data[5];
    if (count > (length - 12) / 16)
        return 0;
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *entry = data + 12 + i * 16;
        if (memcmp(entry, "CFF ", 4))
            continue;
        uint32_t offset = cff_sfnt_uint(entry + 8), size = cff_sfnt_uint(entry + 12);
        if (offset > length || size > length - offset)
            return 0;
        *table = (pdf_cff_slice){data + offset, size};
        return 1;
    }
    return 0;
}

size_t cupidimage_pdf_font_program_bytes(const pdf_font_program *program) {
    return program
               ? sizeof(*program) + program->length + program->gid_length +
                     (program->is_cff ? 65536 * sizeof(uint16_t) +
                                            program->cff.private_count * sizeof(pdf_cff_private) +
                                            (program->cff.fdselect ? program->cff.chars.count : 0)
                                      : 0) +
                     (program->is_type1
                          ? program->type1.private_length +
                                program->type1.glyph_capacity * sizeof(pdf_type1_glyph) +
                                program->type1.subr_count * sizeof(pdf_cff_slice)
                          : 0)
               : 0;
}

void cupidimage_pdf_font_program_free(pdf_font_program *program) {
    if (program) {
        cupidimage_pdf_cff_free(&program->cff);
        cupidimage_pdf_type1_free(&program->type1);
        free(program->bytes);
        free(program->gid_map);
        free(program);
    }
}

unsigned cupidimage_pdf_font_program_glyph(const pdf_font_program *program, unsigned code) {
    if (!program->cid)
        return code < 256 ? program->glyphs[code] : 0;
    if (program->is_cff)
        return code < 65536 ? program->cff.selectors[code] : 0;
    unsigned glyph = code;
    if (program->gid_map)
        glyph = (size_t)code * 2 + 1 < program->gid_length
                    ? (unsigned)program->gid_map[code * 2] * 256 + program->gid_map[code * 2 + 1]
                    : 0;
    return glyph < program->font.glyphs ? glyph : 0;
}

int cupidimage_pdf_font_program_has_glyph(const pdf_font_program *program, unsigned code) {
    if (program->is_type1)
        return code < 256 && program->glyphs[code] < program->type1.glyph_count;
    if (program->is_cff)
        return !code || (program->cid ? code < 65536 && program->cff.selectors[code] != 0
                                      : code < 256 && program->glyphs[code] != 0);
    if (!program->cid)
        return code < 256 && program->glyphs[code] < program->font.glyphs;
    if (!code)
        return 1;
    unsigned glyph = code;
    if (program->gid_map) {
        if ((size_t)code * 2 + 1 >= program->gid_length)
            return 0;
        glyph = (unsigned)program->gid_map[code * 2] * 256 + program->gid_map[code * 2 + 1];
    }
    return glyph > 0 && glyph < program->font.glyphs;
}

double cupidimage_pdf_font_program_advance(const pdf_font_program *program, unsigned glyph) {
    if (program->is_type1) {
        pdf_cff_outline outline;
        if (!cupidimage_pdf_type1_outline(&program->type1, glyph, &outline))
            return 0;
        double width =
            outline.width * program->type1.matrix[0] + outline.width_y * program->type1.matrix[2];
        cupidimage_pdf_type2_free(&outline);
        return width;
    }
    if (program->is_cff) {
        pdf_cff_outline outline;
        if (!cupidimage_pdf_type2_outline(&program->cff, glyph, &outline))
            return 0;
        unsigned fd = program->cff.fdselect ? program->cff.fdselect[glyph] : 0;
        double width = outline.width * program->cff.private_dicts[fd].matrix[0];
        cupidimage_pdf_type2_free(&outline);
        return width;
    }
    return (double)cupidimage_pdf_tt_advance(&program->font, glyph) / program->font.units;
}

int cupidimage_pdf_parse_font_program(const unsigned char *data, size_t size,
                                      const pdf_object_view *view, pdf_font_map *font,
                                      size_t *budget) {
    int type1 = cupidimage_pdf_entry_is_name(data, size, view, "Subtype", "Type1") ||
                cupidimage_pdf_entry_is_name(data, size, view, "Subtype", "MMType1");
    if (!font->cid && !cupidimage_pdf_entry_is_name(data, size, view, "Subtype", "TrueType") &&
        !type1)
        return 1;
    pdf_object_view descendant = {0}, array = {0}, descriptor = {0}, stream = {0}, map = {0};
    const pdf_object_view *owner = view;
    pdf_font_program *program = NULL;
    int ok = 0;
    size_t p = 0;
    if (font->cid) {
        if (!cupidimage_pdf_dictionary_value(data, size, view, "DescendantFonts", &array))
            goto done;
        cupidimage_pdf_skip_ws_and_comments(array.data, array.len, &p);
        if (p == array.len || array.data[p++] != '[')
            goto done;
        if (!cupidimage_pdf_font_dictionary_at(data, size, &array, p, &descendant))
            goto done;
        owner = &descendant;
        if (!cupidimage_pdf_entry_is_name(data, size, owner, "Subtype", "CIDFontType2") &&
            !cupidimage_pdf_entry_is_name(data, size, owner, "Subtype", "CIDFontType0")) {
            ok = 1;
            goto done;
        }
    }
    int present =
        cupidimage_pdf_optional_dictionary(data, size, owner, "FontDescriptor", &descriptor);
    if (present <= 0) {
        ok = present == 0;
        goto done;
    }
    const char *program_keys[] = {"FontFile2", "FontFile3", "FontFile"};
    const char *program_key = NULL;
    for (unsigned i = 0; i < ((!font->cid && type1) ? 3u : 2u); i++) {
        present = cupidimage_pdf_optional_value(data, size, &descriptor, program_keys[i], &stream);
        if (present < 0)
            goto done;
        if (present) {
            program_key = program_keys[i];
            break;
        }
    }
    if (!program_key) {
        ok = 1;
        goto done;
    }
    int cff_stream = cupidimage_pdf_entry_is_name(data, size, &stream, "Subtype", "Type1C") ||
                     cupidimage_pdf_entry_is_name(data, size, &stream, "Subtype", "CIDFontType0C");
    if (!strcmp(program_key, "FontFile3") && !cff_stream &&
        !cupidimage_pdf_entry_is_name(data, size, &stream, "Subtype", "OpenType")) {
        ok = 1;
        goto done;
    }
    const unsigned char *bytes;
    size_t length;
    program = calloc(1, sizeof(*program));
    if (!program)
        goto done;
    if (!cupidimage_pdf_object_stream(data, size, &stream, &bytes, &length) ||
        !cupidimage_pdf_decode_stream(&stream, bytes, length, &program->bytes, &program->length) ||
        program->length > 16u * 1024u * 1024u)
        goto done;
    program->cid = font->cid != NULL;
    pdf_cff_slice cff = {program->bytes, program->length};
    if (!strcmp(program_key, "FontFile")) {
        size_t parts[3] = {0};
        const char *keys[] = {"Length1", "Length2", "Length3"};
        for (unsigned i = 0; i < 3; i++) {
            pdf_object_view value = {0};
            size_t at = 0;
            double number;
            if (i == 2 && !cupidimage_pdf_page_value(&stream, keys[i], &at))
                continue;
            at = 0;
            int valid = cupidimage_pdf_dictionary_value(data, size, &stream, keys[i], &value);
            cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &at);
            valid = valid && cupidimage_pdf_parse_pdf_number(value.data, value.len, &at, &number) &&
                    isfinite(number) && number >= 0 && number <= 16u * 1024u * 1024u &&
                    floor(number) == number;
            cupidimage_pdf_object_view_release(&value);
            if (!valid)
                goto done;
            parts[i] = (size_t)number;
        }
        if (parts[0] > program->length || parts[1] > program->length - parts[0] ||
            parts[2] > program->length - parts[0] - parts[1])
            goto done;
        program->is_type1 = 1;
        if (!cupidimage_pdf_type1_init(&program->type1, program->bytes, program->length, parts[0],
                                       parts[1]) ||
            !ps_pdf_encoding(data, size, view, program))
            goto done;
    } else if (cff_stream || cff_sfnt_table(program->bytes, program->length, &cff)) {
        program->is_cff = 1;
        if (!cupidimage_pdf_cff_init(&program->cff, cff.data, cff.length) ||
            program->cff.top.cid != (unsigned)program->cid ||
            (!program->cid && !ps_pdf_encoding(data, size, view, program)))
            goto done;
    } else if (!cupidimage_pdf_tt_init(&program->font, program->bytes, program->length))
        goto done;
    if (program->cid && !program->is_cff && !program->is_type1) {
        present = cupidimage_pdf_optional_value(data, size, owner, "CIDToGIDMap", &map);
        if (present < 0)
            goto done;
        if (present) {
            char name[128];
            size_t at = 0;
            if (!(cupidimage_pdf_read_name(&map, &at, name) && !strcmp(name, "Identity"))) {
                if (!cupidimage_pdf_object_stream(data, size, &map, &bytes, &length) ||
                    !cupidimage_pdf_decode_stream(&map, bytes, length, &program->gid_map,
                                                  &program->gid_length) ||
                    program->gid_length > 131072 || program->gid_length % 2)
                    goto done;
            }
        }
    } else if (!program->cid && !program->is_cff && !program->is_type1 &&
               !tt_pdf_simple_encoding(data, size, view, &descriptor, program))
        goto done;
    size_t cost = cupidimage_pdf_font_program_bytes(program);
    if (*budget > 64u * 1024u * 1024u || cost > 64u * 1024u * 1024u - *budget)
        goto done;
    *budget += cost;
    font->program = program;
    program = NULL;
    ok = 1;
done:
    cupidimage_pdf_font_program_free(program);
    cupidimage_pdf_object_view_release(&map);
    cupidimage_pdf_object_view_release(&stream);
    cupidimage_pdf_object_view_release(&descriptor);
    cupidimage_pdf_object_view_release(&descendant);
    cupidimage_pdf_object_view_release(&array);
    return ok;
}
