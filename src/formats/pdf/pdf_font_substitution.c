#include "fonts/encoding.h"
#include "fonts/program.h"
#include "fonts/standard.h"
#include "pdf_content_syntax.h"
#include "pdf_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

pdf_font_program *cupidimage_pdf_load_substitute_program(const char *name) {
    const pdf_extended_font *source = cupidimage_pdf_extended_font(name);
    if (!source || source->decoded_length > 1024u * 1024u)
        return NULL;
    pdf_font_program *program = calloc(1, sizeof(*program));
    if (!program)
        return NULL;
    program->references = 1;
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

static const char *substitute_subset_name(const char *name) {
    if (strlen(name) > 7 && name[6] == '+') {
        unsigned i = 0;
        while (i < 6 && name[i] >= 'A' && name[i] <= 'Z')
            i++;
        if (i == 6)
            return name + 7;
    }
    return name;
}

static const char *substitute_face(const char *name) {
    name = substitute_subset_name(name);

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

static int substitute_value_end(const pdf_object_view *descriptor, const char *key,
                                const pdf_object_view *value, size_t at) {
    size_t original;
    pdf_object_id object;
    if (!cupidimage_pdf_page_value(descriptor, key, &original) ||
        !cupidimage_pdf_page_reference(descriptor, original, &object))
        return 1;
    cupidimage_pdf_skip_ws_and_comments(value->data, value->len, &at);
    return at == value->len;
}

static int substitute_number(const unsigned char *data, size_t size,
                             const pdf_object_view *descriptor, const char *key, double *number) {
    pdf_object_view value = {0};
    int present = cupidimage_pdf_optional_value(data, size, descriptor, key, &value);
    if (present <= 0)
        return present == 0;
    size_t at = 0;
    int ok = cupidimage_pdf_resolved_number(data, size, &value, &at, number) && isfinite(*number) &&
             substitute_value_end(descriptor, key, &value, at);
    cupidimage_pdf_object_view_release(&value);
    return ok;
}

static int substitute_family(const unsigned char *data, size_t size,
                             const pdf_object_view *descriptor, int *family) {
    pdf_object_view value = {0};
    int present = cupidimage_pdf_optional_value(data, size, descriptor, "FontFamily", &value);
    if (present <= 0)
        return present == 0;
    size_t at = 0, length = 0;
    char *name = NULL;
    cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &at);
    int ok = at < value.len &&
             (value.data[at] == '('
                  ? cupidimage_pdf_content_literal(value.data, value.len, &at, &name, &length)
                  : cupidimage_pdf_content_hex(value.data, value.len, &at, &name, &length));
    ok = ok && substitute_value_end(descriptor, "FontFamily", &value, at);
    static const char *families[][4] = {
        {"Arial", "Helvetica", "Liberation Sans", "LiberationSans"},
        {"Times New Roman", "Times", "Liberation Serif", "LiberationSerif"},
        {"Courier New", "Courier", "Liberation Mono", "LiberationMono"}};
    if (ok && !memchr(name, 0, length))
        for (unsigned i = 0; i < 3; i++)
            for (unsigned j = 0; j < 4; j++)
                if (!strcmp(name, families[i][j]))
                    *family = (int)i;
    free(name);
    cupidimage_pdf_object_view_release(&value);
    return ok;
}

static int substitute_descriptor(const unsigned char *data, size_t size,
                                 const pdf_object_view *descriptor, const char **face) {
    double flags_value = 0, weight = 400, angle = 0;
    if (!substitute_number(data, size, descriptor, "Flags", &flags_value) || flags_value < 0 ||
        flags_value > 0x7fffffff || floor(flags_value) != flags_value)
        return 0;
    unsigned flags = (unsigned)flags_value;
    /* A Latin substitute cannot supply arbitrary symbolic, script or small-cap
     * designs. ForceBold is a rasterization hint, not a requested font weight. */
    if (!(flags & 32) || (flags & (4 | 8 | 65536 | 131072)))
        return 1;
    if (!substitute_number(data, size, descriptor, "FontWeight", &weight) || weight < 100 ||
        weight > 900 || fmod(weight, 100) != 0 ||
        !substitute_number(data, size, descriptor, "ItalicAngle", &angle))
        return 0;
    int family = flags & 1 ? 2 : flags & 2 ? 1 : 0;
    if (!substitute_family(data, size, descriptor, &family))
        return 0;
    static const char *faces[3][4] = {
        {"Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique"},
        {"Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic"},
        {"Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique"}};
    unsigned style = (weight >= 600 ? 1 : 0) + ((flags & 64) || angle != 0 ? 2 : 0);
    *face = faces[family][style];
    return 1;
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
    pdf_object_view descriptor = {0};
    if (cupidimage_pdf_optional_dictionary(data, size, view, "FontDescriptor", &descriptor) < 0)
        return 0;
    if (!face && !substitute_descriptor(data, size, &descriptor, &face)) {
        cupidimage_pdf_object_view_release(&descriptor);
        return 0;
    }
    if (!face) {
        cupidimage_pdf_object_view_release(&descriptor);
        return 1;
    }
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
