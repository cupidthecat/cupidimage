#include "fonts/encoding.h"
#include "fonts/program.h"
#include "fonts/standard.h"
#include "pdf_internal.h"
#include <stdlib.h>
#include <string.h>

static int standard_extended(pdf_font_program *program, const char *name) {
    if (program->extended)
        return 1;
    const pdf_extended_font *source = cupidimage_pdf_extended_font(name);
    if (!source || source->decoded_length > 1024u * 1024u)
        return 0;
    pdf_font_program *extended = calloc(1, sizeof(*extended));
    if (!extended)
        return 0;
    program->extended = extended;
    extended->bytes = malloc(source->decoded_length);
    extended->length = source->decoded_length;
    size_t decoded = 0;
    return extended->bytes &&
           cupidimage_inflate_zlib(source->data, source->length, extended->bytes, extended->length,
                                   &decoded) &&
           decoded == extended->length &&
           cupidimage_pdf_tt_init(&extended->font, extended->bytes, extended->length);
}

int cupidimage_pdf_parse_standard_font(const unsigned char *data, size_t size,
                                       const pdf_object_view *view, pdf_font_map *font,
                                       size_t *budget) {
    if (font->program || font->cid || font->type3 ||
        !cupidimage_pdf_entry_is_name(data, size, view, "Subtype", "Type1"))
        return 1;
    size_t pos;
    if (!cupidimage_pdf_page_value(view, "BaseFont", &pos))
        return 1;
    pdf_object_view value = {0};
    char name[128];
    pos = 0;
    int ok = cupidimage_pdf_dictionary_value(data, size, view, "BaseFont", &value) &&
             cupidimage_pdf_read_name(&value, &pos, name);
    cupidimage_pdf_object_view_release(&value);
    if (!ok)
        return 0;
    const pdf_standard_font *standard = cupidimage_pdf_standard_font(name);
    if (!standard)
        return 1;
    unsigned glyphs[256];
    memcpy(glyphs, standard->encoding, sizeof(glyphs));
    if (!cupidimage_pdf_simple_encoding(data, size, view, glyphs, standard,
                                        cupidimage_pdf_standard_glyph))
        return 0;
    const pdf_standard_outline *outline = cupidimage_pdf_standard_outline(name);
    if (!outline)
        return 0;
    pdf_font_program *program = calloc(1, sizeof(*program));
    if (!program)
        return 0;
    program->bytes = malloc(outline->length);
    program->length = outline->length;
    program->is_cff = 1;
    if (!program->bytes)
        goto fail;
    memcpy(program->bytes, outline->data, outline->length);
    if (!cupidimage_pdf_cff_init(&program->cff, program->bytes, program->length))
        goto fail;
    for (unsigned i = 0; i < 256; i++) {
        const char *glyph_name = standard->metrics[glyphs[i]].name;
        program->glyphs[i] = cupidimage_pdf_cff_name(&program->cff, glyph_name);
        if (!program->glyphs[i] && strcmp(glyph_name, ".notdef")) {
            if (!standard_extended(program, name))
                goto fail;
            const pdf_tt_font *fallback = &program->extended->font;
            unsigned glyph = cupidimage_pdf_tt_name(fallback, glyph_name);
            uint32_t scalar;
            if (!glyph && cupidimage_pdf_glyph_scalar(glyph_name, &scalar))
                glyph = cupidimage_pdf_tt_map(fallback, scalar, 3, 1);
            if (glyph)
                program->glyphs[i] = PDF_FONT_EXTENDED_GLYPH + glyph;
        }
    }
    size_t cost = cupidimage_pdf_font_program_bytes(program);
    if (*budget > 64u * 1024u * 1024u || cost > 64u * 1024u * 1024u - *budget)
        goto fail;
    if (!font->has_widths && !font->has_missing_width) {
        for (unsigned i = 0; i < 256; i++)
            font->widths[i] = standard->metrics[glyphs[i]].width / 1000.0;
        font->first_char = 0;
        font->last_char = 255;
        font->has_widths = 1;
    }
    *budget += cost;
    font->program = program;
    return 1;
fail:
    cupidimage_pdf_font_program_free(program);
    return 0;
}
