#include "fonts/encoding.h"
#include "fonts/program.h"
#include "fonts/standard.h"
#include "pdf_internal.h"
#include <stdlib.h>
#include <string.h>

pdf_font_program *cupidimage_pdf_load_substitute_program(const char *name) {
    const pdf_extended_font *source = cupidimage_pdf_extended_font(name);
    if (!source || source->decoded_length > 1024u * 1024u)
        return NULL;
    pdf_font_program *program = calloc(1, sizeof(*program));
    if (!program)
        return NULL;
    program->bytes = malloc(source->decoded_length);
    program->length = source->decoded_length;
    size_t decoded = 0;
    if (program->bytes &&
        cupidimage_inflate_zlib(source->data, source->length, program->bytes, program->length,
                                &decoded) &&
        decoded == program->length &&
        cupidimage_pdf_tt_init(&program->font, program->bytes, program->length))
        return program;
    cupidimage_pdf_font_program_free(program);
    return NULL;
}

static const char *substitute_face(const char *name) {
    static const struct {
        const char *family, *face;
    } families[] = {
        {"Arial", "Helvetica"},     {"ArialMT", "Helvetica"},     {"LiberationSans", "Helvetica"},
        {"TimesNewRoman", "Times"}, {"TimesNewRomanPS", "Times"}, {"LiberationSerif", "Times"},
        {"CourierNew", "Courier"},  {"CourierNewPS", "Courier"},  {"LiberationMono", "Courier"}};

    static const struct {
        const char *suffix;
        unsigned style;
    } styles[] = {{"", 0},        {"MT", 0},          {"-Regular", 0},      {"-Bold", 1},
                  {"-BoldMT", 1}, {",Bold", 1},       {"-Italic", 2},       {"-ItalicMT", 2},
                  {",Italic", 2}, {"-BoldItalic", 3}, {"-BoldItalicMT", 3}, {",BoldItalic", 3}};

    static const char *faces[3][4] = {
        {"Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique"},
        {"Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic"},
        {"Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique"}};
    for (size_t i = 0; i < sizeof(families) / sizeof(*families); i++) {
        size_t length = strlen(families[i].family);
        if (strncmp(name, families[i].family, length))
            continue;
        for (size_t j = 0; j < sizeof(styles) / sizeof(*styles); j++)
            if (!strcmp(name + length, styles[j].suffix)) {
                unsigned family = !strcmp(families[i].face, "Helvetica") ? 0
                                  : !strcmp(families[i].face, "Times")   ? 1
                                                                         : 2;
                return faces[family][styles[j].style];
            }
    }
    return NULL;
}

static unsigned substitute_glyph(const void *context, const char *name) {
    const pdf_tt_font *font = context;
    unsigned glyph = cupidimage_pdf_tt_name(font, name);
    uint32_t scalar;
    if (!glyph && cupidimage_pdf_glyph_scalar(name, &scalar))
        glyph = cupidimage_pdf_tt_map(font, scalar, 3, 1);
    return glyph;
}

int cupidimage_pdf_parse_substitute_font(const unsigned char *data, size_t size,
                                         const pdf_object_view *view, pdf_font_map *font,
                                         size_t *budget) {
    if (font->program || font->cid || font->type3)
        return 1;
    int truetype = cupidimage_pdf_entry_is_name(data, size, view, "Subtype", "TrueType");
    if (!truetype && !cupidimage_pdf_entry_is_name(data, size, view, "Subtype", "Type1"))
        return 1;
    pdf_object_view value = {0};
    char name[128];
    size_t at = 0;
    int present = cupidimage_pdf_optional_value(data, size, view, "BaseFont", &value);
    int valid = present >= 0 && (!present || cupidimage_pdf_read_name(&value, &at, name));
    cupidimage_pdf_object_view_release(&value);
    if (!valid)
        return 0;
    const char *face = present ? substitute_face(name) : NULL;
    if (!face)
        return 1;
    pdf_object_view descriptor = {0};
    if (cupidimage_pdf_optional_dictionary(data, size, view, "FontDescriptor", &descriptor) < 0)
        return 0;
    const char *keys[] = {"FontFile", "FontFile2", "FontFile3"};
    for (unsigned i = 0; i < 3; i++) {
        pdf_object_view embedded = {0};
        int supplied = cupidimage_pdf_optional_value(data, size, &descriptor, keys[i], &embedded);
        cupidimage_pdf_object_view_release(&embedded);
        if (supplied != 0) {
            cupidimage_pdf_object_view_release(&descriptor);
            return 0;
        }
    }
    pdf_font_program *program = cupidimage_pdf_load_substitute_program(face);
    if (!program) {
        cupidimage_pdf_object_view_release(&descriptor);
        return 0;
    }
    if (truetype) {
        valid = cupidimage_pdf_truetype_encoding(data, size, view, &descriptor, program);
    } else {
        for (unsigned code = 0; code < 256; code++)
            program->glyphs[code] = substitute_glyph(
                &program->font, cupidimage_pdf_encoding_name("StandardEncoding", code));
        valid = cupidimage_pdf_simple_encoding(data, size, view, program->glyphs, &program->font,
                                               substitute_glyph);
    }
    cupidimage_pdf_object_view_release(&descriptor);
    size_t cost = cupidimage_pdf_font_program_bytes(program);
    if (!valid || *budget > 64u * 1024u * 1024u || cost > 64u * 1024u * 1024u - *budget) {
        cupidimage_pdf_font_program_free(program);
        return 0;
    }
    *budget += cost;
    font->program = program;
    return 1;
}
