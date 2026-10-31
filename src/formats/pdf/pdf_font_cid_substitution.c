#include "fonts/encoding.h"
#include "fonts/program.h"
#include "fonts/substitute/cid_data.h"
#include "pdf_content_syntax.h"
#include "pdf_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int cid_substitute_string(const unsigned char *data, size_t size,
                                 const pdf_object_view *owner, const char *key,
                                 const char *expected) {
    pdf_object_view value = {0};
    int present = cupidimage_pdf_optional_value(data, size, owner, key, &value);
    if (present <= 0)
        return present;
    /* Collection identifiers used here are short ASCII strings. Do not allocate
     * an arbitrarily large string merely to rule out a known collection. */
    size_t at = 0, length = 0, limit = value.len < 1024 ? value.len : 1024;
    char *text = NULL;
    cupidimage_pdf_skip_ws_and_comments(value.data, limit, &at);
    int ok =
        at < limit && (value.data[at] == '('
                           ? cupidimage_pdf_content_literal(value.data, limit, &at, &text, &length)
                           : cupidimage_pdf_content_hex(value.data, limit, &at, &text, &length));
    int match = ok && length == strlen(expected) && !memcmp(text, expected, length);
    size_t original;
    pdf_object_id reference;
    if (ok && cupidimage_pdf_page_value(owner, key, &original) &&
        cupidimage_pdf_page_reference(owner, original, &reference)) {
        cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &at);
        ok = at == value.len;
    }
    free(text);
    cupidimage_pdf_object_view_release(&value);
    return ok ? match : -1;
}

static int cid_substitute_number(const unsigned char *data, size_t size,
                                 const pdf_object_view *owner, const char *key, double *number) {
    pdf_object_view value = {0};
    int present = cupidimage_pdf_optional_value(data, size, owner, key, &value);
    if (present <= 0)
        return present;
    size_t at = 0, original;
    pdf_object_id reference;
    int ok = cupidimage_pdf_resolved_number(data, size, &value, &at, number) && isfinite(*number);
    if (ok && cupidimage_pdf_page_value(owner, key, &original) &&
        cupidimage_pdf_page_reference(owner, original, &reference)) {
        cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &at);
        ok = at == value.len;
    }
    cupidimage_pdf_object_view_release(&value);
    return ok ? 1 : -1;
}

static pdf_font_program *cid_substitute_load(const pdf_cid_font_source *source) {
    if (source->decoded_length > 8u * 1024u * 1024u)
        return NULL;
    pdf_font_program *program = calloc(1, sizeof(*program));
    if (!program)
        return NULL;
    program->references = 1;
    program->cid = program->is_cff = 1;
    program->cid_substitute = source;
    program->length = source->decoded_length;
    program->bytes = malloc(program->length);
    size_t decoded = 0;
    pdf_cff_slice cff = {0};
    if (program->bytes &&
        cupidimage_inflate_zlib(source->data, source->length, program->bytes, program->length,
                                &decoded) &&
        decoded == program->length &&
        cupidimage_pdf_cff_sfnt_table(program->bytes, program->length, &cff) &&
        cupidimage_pdf_cff_init(&program->cff, cff.data, cff.length) && program->cff.top.cid)
        return program;
    cupidimage_pdf_font_program_free(program);
    return NULL;
}

int cupidimage_pdf_parse_cid_substitute(const unsigned char *data, size_t size,
                                        const pdf_object_view *view, pdf_font_map *font,
                                        pdf_font_table *table) {
    if (font->program || !font->cid)
        return 1;
    pdf_object_view array = {0}, descendant = {0}, collection = {0}, descriptor = {0};
    size_t at = 0;
    int ok = 0;
    if (!cupidimage_pdf_dictionary_value(data, size, view, "DescendantFonts", &array))
        goto done;
    cupidimage_pdf_skip_ws_and_comments(array.data, array.len, &at);
    if (at == array.len || array.data[at++] != '[' ||
        !cupidimage_pdf_font_dictionary_at(data, size, &array, at, &descendant))
        goto done;
    /* External Type 2 fonts require character-code/cmap selection, not direct
     * CID selection. This resolver supplies only CID-keyed Type 0 substitutes. */
    if (!cupidimage_pdf_entry_is_name(data, size, &descendant, "Subtype", "CIDFontType0")) {
        ok = 1;
        goto done;
    }
    int present =
        cupidimage_pdf_optional_dictionary(data, size, &descendant, "CIDSystemInfo", &collection);
    if (present < 0)
        goto done;
    int registry =
        present ? cid_substitute_string(data, size, &collection, "Registry", "Adobe") : 0;
    int ordering =
        registry > 0 ? cid_substitute_string(data, size, &collection, "Ordering", "Japan1") : 0;
    if (registry < 0 || ordering < 0)
        goto done;
    if (!registry || !ordering) {
        ok = 1;
        goto done;
    }
    double supplement = 0, flags = 0;
    present = cid_substitute_number(data, size, &collection, "Supplement", &supplement);
    if (present < 0 || supplement < 0 || floor(supplement) != supplement)
        goto done;
    if (!present || supplement > cupidimage_pdf_cid_gothic.supplement) {
        ok = 1;
        goto done;
    }
    if (cupidimage_pdf_optional_dictionary(data, size, &descendant, "FontDescriptor", &descriptor) <
        0)
        goto done;
    const char *keys[] = {"FontFile", "FontFile2", "FontFile3"};
    for (unsigned i = 0; i < 3; i++) {
        pdf_object_view embedded = {0};
        int supplied = cupidimage_pdf_optional_value(data, size, &descriptor, keys[i], &embedded);
        cupidimage_pdf_object_view_release(&embedded);
        if (supplied != 0)
            goto done;
    }
    if (cid_substitute_number(data, size, &descriptor, "Flags", &flags) < 0 || flags < 0 ||
        flags > 0xffffffffu || floor(flags) != flags)
        goto done;
    int serif = ((uint32_t)flags & 2) != 0;
    pdf_object_view name_value = {0};
    present = cupidimage_pdf_optional_value(data, size, &descendant, "BaseFont", &name_value);
    char name[128];
    at = 0;
    int valid_name =
        present == 0 || (present > 0 && cupidimage_pdf_read_name(&name_value, &at, name));
    if (present > 0 && valid_name) {
        const char *base = cupidimage_pdf_font_base_name(name);
        if (!strcmp(base, "HeiseiMin-W3") || !strcmp(base, "Ryumin-Light") ||
            !strcmp(base, "MS-Mincho") || !strcmp(base, "MSMincho"))
            serif = 1;
    }
    cupidimage_pdf_object_view_release(&name_value);
    if (!valid_name)
        goto done;
    const pdf_cid_font_source *source =
        serif ? &cupidimage_pdf_cid_mincho : &cupidimage_pdf_cid_gothic;
    for (int i = 0; i < table->count; i++) {
        pdf_font_program *cached = table->fonts[i].program;
        if (cached && cached->cid_substitute == source) {
            if (!cupidimage_pdf_font_program_retain(cached))
                goto done;
            font->program = cached;
            ok = 1;
            goto done;
        }
    }
    pdf_font_program *program = cid_substitute_load(source);
    if (!program)
        goto done;
    size_t cost = cupidimage_pdf_font_program_bytes(program);
    if (table->program_bytes > 64u * 1024u * 1024u ||
        cost > 64u * 1024u * 1024u - table->program_bytes) {
        cupidimage_pdf_font_program_free(program);
        goto done;
    }
    table->program_bytes += cost;
    font->program = program;
    ok = 1;
done:
    cupidimage_pdf_object_view_release(&descriptor);
    cupidimage_pdf_object_view_release(&collection);
    cupidimage_pdf_object_view_release(&descendant);
    cupidimage_pdf_object_view_release(&array);
    return ok;
}
