#include "type1.h"
#include "encoding.h"
#include "type1/hints.h"
#include "type1/lexer.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char readers[16][128];
    unsigned reader_count;
    char definers[16][128];
    unsigned definer_count;
} t1_syntax;

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

static int t1_reader(const t1_syntax *syntax, pdf_type1_token token) {
    if (token.type != PDF_T1_WORD)
        return 0;
    for (unsigned i = 0; i < syntax->reader_count; i++)
        if (!strcmp(token.text, syntax->readers[i]))
            return 1;
    return 0;
}

static int t1_reader_proc(pdf_type1_token token) {
    static const char *const words[] = {"string", "currentfile", "exch", "readstring", "pop"};
    if (token.type != PDF_T1_PROC)
        return 0;
    size_t pos = 0;
    for (unsigned i = 0; i < 5; i++)
        if (!t1_word(cupidimage_pdf_type1_lex(token.body, &pos, 0), words[i]))
            return 0;
    return cupidimage_pdf_type1_lex(token.body, &pos, 0).type == PDF_T1_EOF;
}

static int t1_definer_proc(pdf_type1_token token) {
    if (token.type != PDF_T1_PROC)
        return 0;
    size_t pos = 0;
    pdf_type1_token word = cupidimage_pdf_type1_lex(token.body, &pos, 0);
    if (t1_word(word, "noaccess") || t1_word(word, "readonly"))
        word = cupidimage_pdf_type1_lex(token.body, &pos, 0);
    return t1_word(word, "def") && cupidimage_pdf_type1_lex(token.body, &pos, 0).type == PDF_T1_EOF;
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

static int t1_matrix(pdf_cff_slice source, size_t *pos, double matrix[6]) {
    pdf_type1_token open = cupidimage_pdf_type1_lex(source, pos, 0);
    if (open.type != PDF_T1_OTHER || open.text[0] != '[')
        return 0;
    for (unsigned i = 0; i < 6; i++) {
        pdf_type1_token item = cupidimage_pdf_type1_lex(source, pos, 0);
        if (item.type != PDF_T1_NUMBER || fabs(item.number) > 1e30)
            return 0;
        matrix[i] = item.number;
    }
    pdf_type1_token close = cupidimage_pdf_type1_lex(source, pos, 0);
    return close.type == PDF_T1_OTHER && close.text[0] == ']';
}

static int t1_clear(pdf_type1_font *font, pdf_cff_slice source, char names[256][128], int plain,
                    size_t *private_start) {
    size_t pos = 0;
    int scope = 0, font_scope = 0, encoding = 0, found_type = 0, found_eexec = 0;
    while (pos < source.length) {
        size_t token_start = pos;
        pdf_type1_token token = cupidimage_pdf_type1_lex(source, &pos, 0);
        if (token.type == PDF_T1_ERROR)
            return 0;
        if (plain && token.type == PDF_T1_NAME && !strcmp(token.text, "Private") &&
            (scope == font_scope || scope == 0)) {
            *private_start = token_start;
            return found_type;
        }
        if (t1_word(token, "begin")) {
            if (++scope > 64)
                return 0;
            if (!font_scope)
                font_scope = scope;
        }
        if (t1_word(token, "end") && scope)
            scope--;
        if (t1_word(token, "eexec")) {
            found_eexec = 1;
            break;
        }
        if (scope != font_scope)
            continue;
        if (token.type == PDF_T1_NAME && !strcmp(token.text, "FontType")) {
            int type;
            if (!t1_int(cupidimage_pdf_type1_lex(source, &pos, 0), 1, 1, &type))
                return 0;
            found_type = 1;
        } else if (token.type == PDF_T1_NAME && !strcmp(token.text, "FontMatrix")) {
            if (!t1_matrix(source, &pos, font->matrix))
                return 0;
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
        } else if (t1_word(token, "def"))
            encoding = 0;
    }
    return found_type && found_eexec;
}

static int t1_subrs(pdf_type1_font *font, pdf_cff_slice source, size_t *pos,
                    const t1_syntax *syntax) {
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
                          const t1_syntax *syntax) {
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

static int t1_private(pdf_type1_font *font) {
    pdf_cff_slice source = {font->private_data + 4, font->private_length - 4};
    size_t pos = 0;
    t1_syntax syntax = {0};
    strcpy(syntax.readers[0], "RD");
    strcpy(syntax.readers[1], "-|");
    syntax.reader_count = 2;
    unsigned scope = 0, private_scope = 0, private_pending = 0, literal_depth = 0;
    while (pos < source.length) {
        pdf_type1_token token = cupidimage_pdf_type1_lex(source, &pos, 0);
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
            if (++scope > 64)
                return 0;
            if (private_pending) {
                private_scope = scope;
                private_pending = 0;
                cupidimage_pdf_ps_hints_init(&font->hints);
                font->unresolved_hints = 0;
                syntax.definer_count = 0;
            }
        } else if (!literal_depth && t1_word(token, "end") && scope) {
            if (scope == private_scope)
                private_scope = 0;
            scope--;
        }
        if (token.type == PDF_T1_NAME && !literal_depth) {
            unsigned field = cupidimage_pdf_type1_hint_field(token.text);
            if (!private_scope && !strcmp(token.text, "Private"))
                private_pending = 1;
            if (private_scope && scope == private_scope && field < PDF_PS_HINT_FIELDS) {
                cupidimage_pdf_type1_hint_read(&font->hints, &font->unresolved_hints, field, source,
                                               &pos, syntax.definers, syntax.definer_count);
            } else if (!strcmp(token.text, "lenIV")) {
                if (!t1_int(cupidimage_pdf_type1_lex(source, &pos, 0), -1, 255, &font->len_iv))
                    return 0;
            } else if (!strcmp(token.text, "Subrs")) {
                if (!t1_subrs(font, source, &pos, &syntax))
                    return 0;
            } else if (!strcmp(token.text, "CharStrings")) {
                if (!t1_charstrings(font, source, &pos, &syntax))
                    return 0;
            } else {
                size_t saved = pos;
                pdf_type1_token value = cupidimage_pdf_type1_lex(source, &pos, 0);
                for (unsigned i = 0;
                     private_scope && scope == private_scope && i < syntax.definer_count; i++)
                    if (!strcmp(syntax.definers[i], token.text)) {
                        memcpy(syntax.definers[i], syntax.definers[--syntax.definer_count], 128);
                        break;
                    }
                if (t1_reader_proc(value)) {
                    if (syntax.reader_count == 16)
                        return 0;
                    strcpy(syntax.readers[syntax.reader_count++], token.text);
                } else {
                    size_t after_value = pos;
                    pdf_type1_token defined = cupidimage_pdf_type1_lex(source, &after_value, 0);
                    for (unsigned i = 0;
                         i < 3 &&
                         (t1_word(defined, "executeonly") || t1_word(defined, "readonly") ||
                          t1_word(defined, "noaccess") || t1_word(defined, "bind"));
                         i++)
                        defined = cupidimage_pdf_type1_lex(source, &after_value, 0);
                    if (private_scope && scope == private_scope && t1_definer_proc(value) &&
                        t1_word(defined, "def")) {
                        unsigned i = 0;
                        while (i < syntax.definer_count && strcmp(syntax.definers[i], token.text))
                            i++;
                        if (i == 16)
                            return 0;
                        strcpy(syntax.definers[i], token.text);
                        if (i == syntax.definer_count)
                            syntax.definer_count++;
                    }
                    pos = saved;
                }
            }
        }
        if (t1_word(token, "closefile"))
            break;
    }
    return font->glyph_count != 0;
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
    if (!t1_clear(font, (pdf_cff_slice){data, clear_length}, names, !encrypted_length,
                  &private_start))
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
    if (!t1_private(font))
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
