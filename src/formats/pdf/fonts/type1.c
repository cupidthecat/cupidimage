#include "type1.h"
#include "encoding.h"
#include "type1/hints.h"
#include "type1/lexer.h"
#include "type1/syntax.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int t1_int(pdf_type1_token token, int low, int high, int *result) {
    if (token.type != PDF_T1_NUMBER || token.number < low || token.number > high ||
        floor(token.number) != token.number)
        return 0;
    *result = (int)token.number;
    return 1;
}

static int t1_word(pdf_type1_token token, const char *word) {
    return token.type == PDF_T1_WORD && !strcmp(token.text, word);
}

static unsigned t1_dictionary_target(pdf_type1_token token) {
    if (t1_word(token, "userdict"))
        return PDF_T1_USER_DICTIONARY;
    if (t1_word(token, "currentdict"))
        return PDF_T1_CURRENT_DICTIONARY;
    if (t1_word(token, "systemdict"))
        return PDF_T1_SYSTEM_DICTIONARY;
    return PDF_T1_NEW_DICTIONARY;
}

static int t1_reader(pdf_type1_syntax *syntax, pdf_type1_token token) {
    return token.type == PDF_T1_WORD &&
           (cupidimage_pdf_type1_syntax_lookup(syntax, token.text) & PDF_T1_READER);
}

static int t1_binary(pdf_cff_slice source, size_t *pos, int length, pdf_cff_slice *out) {
    if (*pos >= source.length || !cupidimage_pdf_type1_space(source.data[*pos]))
        return 0;
    /* RD consumes the delimiter before readstring starts reading binary bytes. */
    (*pos)++;
    if ((size_t)length > source.length - *pos)
        return 0;
    *out = (pdf_cff_slice){source.data + *pos, (size_t)length};
    *pos += (size_t)length;
    return 1;
}

static void t1_decrypt(unsigned char *data, size_t length, unsigned seed) {
    uint32_t state = seed;
    for (size_t i = 0; i < length; i++) {
        unsigned cipher = data[i];
        data[i] = (unsigned char)(cipher ^ (state >> 8));
        state = ((state + cipher) * 52845u + 22719u) & 65535u;
    }
}

static int t1_matrix(pdf_cff_slice source, size_t *position, double matrix[6]) {
    pdf_type1_token open = cupidimage_pdf_type1_lex(source, position, 0);
    int procedure = open.type == PDF_T1_PROC;
    if (!procedure && (open.type != PDF_T1_OTHER || strcmp(open.text, "[")))
        return 0;
    size_t inner = 0;
    size_t *pos = procedure ? &inner : position;
    pdf_cff_slice values = procedure ? open.body : source;
    for (unsigned i = 0; i < 6; i++) {
        pdf_type1_token value = cupidimage_pdf_type1_lex(values, pos, 0);
        if (value.type != PDF_T1_NUMBER || !isfinite(value.number) || fabs(value.number) > 1e30)
            return 0;
        matrix[i] = value.number;
    }
    pdf_type1_token close = cupidimage_pdf_type1_lex(values, pos, 0);
    return procedure ? close.type == PDF_T1_EOF
                     : close.type == PDF_T1_OTHER && !strcmp(close.text, "]");
}

static int t1_clear(pdf_type1_font *font, pdf_cff_slice source, char names[256][128], int plain,
                    size_t *private_start, pdf_type1_syntax *syntax) {
    size_t pos = 0;
    unsigned scope = syntax->depth, font_scope = 0, literal_depth = 0, dictionary_target = 0;
    unsigned font_dictionary = cupidimage_pdf_type1_syntax_dictionary(syntax);
    int encoding = 0, found_type = 0, found_eexec = 0, font_active = 1;
    while (pos < source.length) {
        if (!cupidimage_pdf_type1_syntax_take(syntax, 1))
            return 0;
        size_t token_start = pos;
        pdf_type1_token token = cupidimage_pdf_type1_lex(source, &pos, 0);
        unsigned target = dictionary_target;
        dictionary_target = t1_dictionary_target(token);
        if (token.type == PDF_T1_ERROR)
            return 0;
        if (token.type == PDF_T1_OTHER) {
            if (!strcmp(token.text, "[") || !strcmp(token.text, "<<")) {
                if (++literal_depth > 64)
                    return 0;
            } else if ((!strcmp(token.text, "]") || !strcmp(token.text, ">>")) && literal_depth)
                literal_depth--;
            continue;
        }
        if (literal_depth)
            continue;
        if (target && token.type == PDF_T1_NAME) {
            if (!cupidimage_pdf_type1_syntax_put_definition(syntax, token.text, source, &pos,
                                                            target))
                return 0;
            continue;
        }
        if (plain && token.type == PDF_T1_NAME && !strcmp(token.text, "Private") &&
            ((font_active && scope == font_scope) || scope == 0)) {
            *private_start = token_start;
            return found_type;
        }
        if (t1_word(token, "begin")) {
            if (!cupidimage_pdf_type1_syntax_begin_dictionary(syntax, target))
                return 0;
            scope = syntax->depth;
            if (!font_scope && !target) {
                font_scope = scope;
                font_dictionary = cupidimage_pdf_type1_syntax_dictionary(syntax);
            }
        }
        if (t1_word(token, "end") && scope) {
            if (scope == font_scope)
                font_active = 0;
            if (!cupidimage_pdf_type1_syntax_end(syntax))
                return 0;
            scope = syntax->depth;
        }
        if (t1_word(token, "eexec")) {
            found_eexec = 1;
            break;
        }
        if (!font_active || cupidimage_pdf_type1_syntax_dictionary(syntax) != font_dictionary) {
            if (token.type == PDF_T1_NAME &&
                !cupidimage_pdf_type1_syntax_definition(syntax, token.text, source, &pos))
                return 0;
            continue;
        }
        if (token.type == PDF_T1_NAME && !strcmp(token.text, "FontType")) {
            int type;
            if (!t1_int(cupidimage_pdf_type1_lex(source, &pos, 0), 1, 1, &type) ||
                !cupidimage_pdf_type1_syntax_end_definition(syntax, source, &pos))
                return 0;
            found_type = 1;
            encoding = 0;
        } else if (token.type == PDF_T1_NAME && !strcmp(token.text, "FontMatrix")) {
            if (!t1_matrix(source, &pos, font->matrix) ||
                !cupidimage_pdf_type1_syntax_end_definition(syntax, source, &pos))
                return 0;
            encoding = 0;
        } else if (token.type == PDF_T1_NAME && !strcmp(token.text, "Encoding")) {
            pdf_type1_token value = cupidimage_pdf_type1_lex(source, &pos, 0);
            if (t1_word(value, "StandardEncoding")) {
                for (unsigned i = 0; i < 256; i++)
                    strcpy(names[i], cupidimage_pdf_encoding_name("StandardEncoding", i));
            } else {
                int count;
                if (!t1_int(value, 256, 256, &count) ||
                    !t1_word(cupidimage_pdf_type1_lex(source, &pos, 0), "array"))
                    return 0;
                for (unsigned i = 0; i < 256; i++)
                    strcpy(names[i], ".notdef");
            }
            encoding = 1;
        } else if (encoding && t1_word(token, "dup")) {
            size_t saved = pos;
            int code;
            pdf_type1_token number = cupidimage_pdf_type1_lex(source, &pos, 0),
                            name = cupidimage_pdf_type1_lex(source, &pos, 0),
                            put = cupidimage_pdf_type1_lex(source, &pos, 0);
            if (t1_int(number, 0, 255, &code) && name.type == PDF_T1_NAME && t1_word(put, "put"))
                strcpy(names[code], name.text);
            else
                pos = saved;
        } else if (token.type == PDF_T1_WORD &&
                   (t1_word(token, "def") ||
                    (cupidimage_pdf_type1_syntax_lookup(syntax, token.text) & PDF_T1_DEFINER)))
            encoding = 0;
        else if (token.type == PDF_T1_NAME) {
            size_t saved = pos;
            if (!cupidimage_pdf_type1_syntax_definition(syntax, token.text, source, &pos))
                return 0;
            if (pos != saved)
                encoding = 0;
        }
    }
    return found_type && found_eexec && !syntax->failed;
}

static int t1_subrs(pdf_type1_font *font, pdf_cff_slice source, size_t *pos,
                    pdf_type1_syntax *syntax) {
    int count;
    if (!t1_int(cupidimage_pdf_type1_lex(source, pos, 0), 0, 65536, &count) ||
        !t1_word(cupidimage_pdf_type1_lex(source, pos, 0), "array"))
        return 0;
    free(font->subrs);
    font->subrs = NULL;
    font->subr_count = 0;
    if (count) {
        font->subrs = calloc((unsigned)count, sizeof(*font->subrs));
        if (!font->subrs)
            return 0;
    }
    font->subr_count = (unsigned)count;
    while (*pos < source.length) {
        size_t saved = *pos;
        pdf_type1_token token = cupidimage_pdf_type1_lex(source, pos, 0);
        if (token.type == PDF_T1_ERROR)
            return 0;
        if (t1_word(token, "dup")) {
            int index, length;
            if (!t1_int(cupidimage_pdf_type1_lex(source, pos, 0), 0, count - 1, &index) ||
                !t1_int(cupidimage_pdf_type1_lex(source, pos, 0), 0, 65535, &length) ||
                !t1_reader(syntax, cupidimage_pdf_type1_lex(source, pos, 0)) ||
                !t1_binary(source, pos, length, font->subrs + index))
                return 0;
        } else if (t1_word(token, "NP") || t1_word(token, "noaccess") || t1_word(token, "put") ||
                   t1_word(token, "|"))
            continue;
        else {
            *pos = saved;
            break;
        }
    }
    return 1;
}

static int t1_add_glyph(pdf_type1_font *font, const char *name, pdf_cff_slice code) {
    if (font->glyph_count == 65535)
        return 0;
    if (font->glyph_count == font->glyph_capacity) {
        unsigned capacity = font->glyph_capacity ? font->glyph_capacity * 2 : 64;
        if (capacity > 65535)
            capacity = 65535;
        pdf_type1_glyph *next = realloc(font->glyphs, (size_t)capacity * sizeof(*next));
        if (!next)
            return 0;
        font->glyphs = next;
        font->glyph_capacity = capacity;
    }
    pdf_type1_glyph *glyph = font->glyphs + font->glyph_count++;
    strcpy(glyph->name, name);
    glyph->code = code;
    glyph->order = font->glyph_count - 1;
    return 1;
}

static int t1_charstrings(pdf_type1_font *font, pdf_cff_slice source, size_t *pos,
                          pdf_type1_syntax *syntax) {
    int count;
    if (!t1_int(cupidimage_pdf_type1_lex(source, pos, 0), 1, 65535, &count) ||
        !t1_word(cupidimage_pdf_type1_lex(source, pos, 0), "dict"))
        return 0;
    pdf_type1_token token = cupidimage_pdf_type1_lex(source, pos, 0);
    if (t1_word(token, "dup"))
        token = cupidimage_pdf_type1_lex(source, pos, 0);
    if (!t1_word(token, "begin"))
        return 0;
    free(font->glyphs);
    font->glyphs = NULL;
    font->glyph_count = font->glyph_capacity = 0;
    while (*pos < source.length) {
        token = cupidimage_pdf_type1_lex(source, pos, 0);
        if (token.type == PDF_T1_ERROR || token.type == PDF_T1_EOF)
            return 0;
        if (t1_word(token, "end"))
            return 1;
        if (token.type == PDF_T1_NAME) {
            int length;
            pdf_cff_slice code;
            if (!t1_int(cupidimage_pdf_type1_lex(source, pos, 0), 0, 65535, &length) ||
                !t1_reader(syntax, cupidimage_pdf_type1_lex(source, pos, 0)) ||
                !t1_binary(source, pos, length, &code) || !t1_add_glyph(font, token.text, code))
                return 0;
        }
    }
    return 0;
}

static int t1_discard_dictionary_value(pdf_cff_slice source, size_t *position,
                                       pdf_type1_syntax *syntax, int charstrings) {
    pdf_type1_font discarded = {0};
    int ok = charstrings ? t1_charstrings(&discarded, source, position, syntax)
                         : t1_subrs(&discarded, source, position, syntax);
    free(discarded.glyphs);
    free(discarded.subrs);
    return ok;
}

static int t1_private(pdf_type1_font *font, pdf_type1_syntax *syntax) {
    pdf_cff_slice source = {font->private_data + 4, font->private_length - 4};
    size_t pos = 0;
    unsigned base_scope = syntax->depth, scope = base_scope;
    unsigned private_scope = 0, private_pending = 0, literal_depth = 0, dictionary_target = 0;
    unsigned private_dictionary = 0;
    while (pos < source.length) {
        if (!cupidimage_pdf_type1_syntax_take(syntax, 1))
            return 0;
        pdf_type1_token token = cupidimage_pdf_type1_lex(source, &pos, 0);
        unsigned target = dictionary_target;
        dictionary_target = t1_dictionary_target(token);
        if (token.type == PDF_T1_ERROR)
            return 0;
        if (token.type == PDF_T1_OTHER) {
            if (!strcmp(token.text, "<<") || !strcmp(token.text, "[")) {
                if (++literal_depth > 64)
                    return 0;
            } else if ((!strcmp(token.text, ">>") || !strcmp(token.text, "]")) && literal_depth)
                literal_depth--;
        }
        if (!literal_depth && t1_word(token, "begin")) {
            if (!cupidimage_pdf_type1_syntax_begin_dictionary(syntax, target))
                return 0;
            scope = syntax->depth;
            if (private_pending) {
                private_scope = scope;
                private_dictionary = cupidimage_pdf_type1_syntax_dictionary(syntax);
                private_pending = 0;
                cupidimage_pdf_ps_hints_init(&font->hints);
                font->unresolved_hints = 0;
                font->len_iv = 4;
                free(font->subrs);
                font->subrs = NULL;
                font->subr_count = 0;
            }
        } else if (!literal_depth && t1_word(token, "end") && scope) {
            if (scope == private_scope)
                private_scope = 0;
            if (!cupidimage_pdf_type1_syntax_end(syntax))
                return 0;
            scope = syntax->depth;
        }
        if (token.type == PDF_T1_NAME && !literal_depth) {
            if (target) {
                if (!cupidimage_pdf_type1_syntax_put_definition(syntax, token.text, source, &pos,
                                                                target))
                    return 0;
                continue;
            }
            unsigned field = cupidimage_pdf_type1_hint_field(token.text);
            int in_private = private_scope &&
                             cupidimage_pdf_type1_syntax_dictionary(syntax) == private_dictionary;
            if (!private_scope && scope == base_scope && !strcmp(token.text, "Private")) {
                size_t look = pos;
                int count;
                if (!t1_int(cupidimage_pdf_type1_lex(source, &look, 0), 0, 65536, &count) ||
                    !t1_word(cupidimage_pdf_type1_lex(source, &look, 0), "dict"))
                    return 0;
                private_pending = 1;
                continue;
            }
            if (in_private && field < PDF_PS_HINT_FIELDS) {
                cupidimage_pdf_type1_hint_read(&font->hints, &font->unresolved_hints, field, source,
                                               &pos, syntax);
            } else if (in_private && !strcmp(token.text, "lenIV")) {
                if (!t1_int(cupidimage_pdf_type1_lex(source, &pos, 0), -1, 255, &font->len_iv) ||
                    !cupidimage_pdf_type1_syntax_end_definition(syntax, source, &pos))
                    return 0;
            } else if (in_private && !strcmp(token.text, "Subrs")) {
                if (!t1_subrs(font, source, &pos, syntax))
                    return 0;
            } else if ((in_private || scope == base_scope) && !strcmp(token.text, "CharStrings")) {
                if (!t1_charstrings(font, source, &pos, syntax))
                    return 0;
            } else {
                /* Unrelated dictionaries may contain binary font-like arrays.
                 * Consume their lengths without assigning them to this font. */
                int subrs = !strcmp(token.text, "Subrs"),
                    chars = !strcmp(token.text, "CharStrings");
                size_t look = pos;
                pdf_type1_token number = cupidimage_pdf_type1_lex(source, &look, 0);
                pdf_type1_token constructor = cupidimage_pdf_type1_lex(source, &look, 0);
                if ((subrs || chars) && number.type == PDF_T1_NUMBER &&
                    t1_word(constructor, chars ? "dict" : "array")) {
                    if (!t1_discard_dictionary_value(source, &pos, syntax, chars))
                        return 0;
                } else if (!cupidimage_pdf_type1_syntax_definition(syntax, token.text, source,
                                                                   &pos))
                    return 0;
            }
        }
        if (!literal_depth && t1_word(token, "closefile"))
            break;
    }
    return font->glyph_count != 0 && !syntax->failed;
}

void cupidimage_pdf_type1_free(pdf_type1_font *font) {
    free(font->private_data);
    free(font->glyphs);
    free(font->subrs);
    memset(font, 0, sizeof(*font));
}

static int t1_glyph_compare(const void *left, const void *right) {
    const pdf_type1_glyph *a = left, *b = right;
    int name = strcmp(a->name, b->name);
    if (name)
        return name;
    return a->order < b->order ? -1 : a->order > b->order;
}

unsigned cupidimage_pdf_type1_name(const pdf_type1_font *font, const char *name) {
    unsigned lo = 0, hi = font->glyph_count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        unsigned index = mid == 0 ? font->notdef_slot : mid == font->notdef_slot ? 0 : mid;
        int order = strcmp(name, font->glyphs[index].name);
        if (!order)
            return index;
        if (order < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return 0;
}

static int t1_char_decrypt(pdf_type1_font *font, pdf_cff_slice *code) {
    if (!code->data)
        return 1;
    if (font->len_iv < 0)
        return 1;
    if (code->length < (unsigned)font->len_iv)
        return 0;
    t1_decrypt((unsigned char *)code->data, code->length, 4330);
    code->data += font->len_iv;
    code->length -= (unsigned)font->len_iv;
    return 1;
}

int cupidimage_pdf_type1_init(pdf_type1_font *font, const unsigned char *data, size_t length,
                              size_t clear_length, size_t encrypted_length) {
    memset(font, 0, sizeof(*font));
    font->len_iv = 4;
    cupidimage_pdf_ps_hints_init(&font->hints);
    font->matrix[0] = font->matrix[3] = .001;
    if (length > 16u * 1024u * 1024u || clear_length > length ||
        encrypted_length > length - clear_length || (encrypted_length && encrypted_length < 4))
        return 0;
    char (*names)[128] = calloc(256, 128);
    if (!names)
        return 0;
    for (unsigned i = 0; i < 256; i++)
        strcpy(names[i], cupidimage_pdf_encoding_name("StandardEncoding", i));
    size_t private_start = 0;
    pdf_type1_syntax syntax;
    cupidimage_pdf_type1_syntax_init(&syntax);
    if (!t1_clear(font, (pdf_cff_slice){data, clear_length}, names, !encrypted_length,
                  &private_start, &syntax))
        goto fail;
    font->private_length = encrypted_length ? encrypted_length : clear_length - private_start + 4;
    font->private_data = malloc(font->private_length);
    if (!font->private_data)
        goto fail;
    if (encrypted_length) {
        memcpy(font->private_data, data + clear_length, encrypted_length);
        t1_decrypt(font->private_data, encrypted_length, 55665);
    } else {
        memset(font->private_data, 0, 4);
        memcpy(font->private_data + 4, data + private_start, clear_length - private_start);
    }
    /* eexec executes with read-only systemdict above the caller's dictionaries. */
    if (encrypted_length &&
        !cupidimage_pdf_type1_syntax_begin_dictionary(&syntax, PDF_T1_SYSTEM_DICTIONARY))
        goto fail;
    if (!t1_private(font, &syntax))
        goto fail;
    qsort(font->glyphs, font->glyph_count, sizeof(*font->glyphs), t1_glyph_compare);
    unsigned unique = 0;
    for (unsigned i = 0; i < font->glyph_count;) {
        unsigned next = i + 1;
        while (next < font->glyph_count && !strcmp(font->glyphs[i].name, font->glyphs[next].name))
            next++;
        font->glyphs[unique++] = font->glyphs[next - 1];
        i = next;
    }
    font->glyph_count = unique;
    unsigned notdef = cupidimage_pdf_type1_name(font, ".notdef");
    if (strcmp(font->glyphs[notdef].name, ".notdef"))
        goto fail;
    if (notdef) {
        pdf_type1_glyph swap = font->glyphs[0];
        font->glyphs[0] = font->glyphs[notdef];
        font->glyphs[notdef] = swap;
    }
    font->notdef_slot = notdef;
    for (unsigned i = 0; i < font->glyph_count; i++)
        if (!t1_char_decrypt(font, &font->glyphs[i].code))
            goto fail;
    for (unsigned i = 0; i < font->subr_count; i++)
        if (!t1_char_decrypt(font, &font->subrs[i]))
            goto fail;
    for (unsigned i = 0; i < 256; i++)
        font->encoding[i] = cupidimage_pdf_type1_name(font, names[i]);
    free(names);
    return 1;
fail:
    free(names);
    cupidimage_pdf_type1_free(font);
    return 0;
}
