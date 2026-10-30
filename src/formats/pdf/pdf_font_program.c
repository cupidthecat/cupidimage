#include "fonts/encoding.h"
#include "fonts/program.h"
#include "fonts/truetype_data.h"
#include "pdf_internal.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

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
                     cupidimage_pdf_font_program_bytes(program->extended) +
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

int cupidimage_pdf_font_program_retain(pdf_font_program *program) {
    if (!program || program->references == UINT_MAX)
        return 0;
    if (!program->references)
        program->references = 1;
    program->references++;
    return 1;
}

void cupidimage_pdf_font_program_free(pdf_font_program *program) {
    if (program) {
        if (program->references > 1) {
            program->references--;
            return;
        }
        cupidimage_pdf_font_program_free(program->extended);
        cupidimage_pdf_font_outline_cache_clear(program);
        cupidimage_pdf_cff_free(&program->cff);
        cupidimage_pdf_type1_free(&program->type1);
        free(program->bytes);
        free(program->gid_map);
        free(program);
    }
}

pdf_font_program *cupidimage_pdf_font_program_select(pdf_font_program *program, unsigned *glyph) {
    while (program->extended && *glyph >= PDF_FONT_EXTENDED_GLYPH) {
        *glyph -= PDF_FONT_EXTENDED_GLYPH;
        program = program->extended;
    }
    return program;
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

static int tt_inherited_metrics(const pdf_tt_table *description) {
    size_t at = 10;
    unsigned records = 4096;
    pdf_tt_component component;
    do {
        if (!records || !cupidimage_pdf_tt_component_read(description, &at, &component))
            return -1;
        records--;
        if (component.flags & 512)
            return 1;
    } while (component.flags & 32);
    return 0;
}

int cupidimage_pdf_font_program_advance(pdf_font_program *program, unsigned glyph,
                                        size_t *cache_bytes_left, double *advance) {
    program = cupidimage_pdf_font_program_select(program, &glyph);
    if (!program->is_type1 && !program->is_cff) {
        pdf_tt_table description;
        if (!cupidimage_pdf_tt_glyph_data(&program->font, glyph, &description))
            return 0;
        int inherited = description.length >= 10 && (description.data[0] & 128)
                            ? tt_inherited_metrics(&description)
                            : 0;
        if (inherited < 0)
            return 0;
        /* Avoid outline allocation when this glyph's own metrics suffice. */
        if (!description.length || (description.length >= 10 && !inherited)) {
            *advance =
                (double)cupidimage_pdf_tt_advance(&program->font, glyph) / program->font.units;
            return isfinite(*advance);
        }
    }
    pdf_tt_outline truetype = {0};
    pdf_cff_outline outline = {0};
    int borrowed = 0;
    int ok = cupidimage_pdf_font_outline(program, glyph, &truetype, &outline, cache_bytes_left,
                                         &borrowed);
    if (ok) {
        if (!program->is_type1 && !program->is_cff)
            *advance = (double)truetype.advance / program->font.units;
        else if (program->is_type1)
            *advance = outline.width * program->type1.matrix[0] +
                       outline.width_y * program->type1.matrix[2];
        else {
            unsigned fd = program->cff.fdselect ? program->cff.fdselect[glyph] : 0;
            *advance = outline.width * program->cff.private_dicts[fd].matrix[0];
        }
        ok = isfinite(*advance);
    }
    if (!borrowed) {
        cupidimage_pdf_tt_outline_free(&truetype);
        cupidimage_pdf_type2_free(&outline);
    }
    return ok;
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
    program->references = 1;
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
               !cupidimage_pdf_truetype_encoding(data, size, view, &descriptor, program))
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
