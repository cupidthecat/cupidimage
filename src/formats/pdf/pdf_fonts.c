#include "cupidimage.h"
#include "core/internal.h"

#include <ctype.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pdf_internal.h"

void cupidimage_pdf_font_table_init(pdf_font_table *table) {
    if (!table) return;
    table->fonts = NULL;
    table->count = 0;
    table->cap = 0;
}

void cupidimage_pdf_font_table_free(pdf_font_table *table) {
    if (!table) return;
    for (int i = 0; i < table->count; i++) {
        free(table->fonts[i].entries);
        table->fonts[i].entries = NULL;
        table->fonts[i].count = 0;
        table->fonts[i].cap = 0;
        table->fonts[i].max_src_len = 1;
    }
    free(table->fonts);
    table->fonts = NULL;
    table->count = 0;
    table->cap = 0;
}

static pdf_font_map *pdf_font_table_add(pdf_font_table *table, const char *name) {
    if (!table || !name || !*name) {
        return NULL;
    }
    for (int i = 0; i < table->count; i++) {
        if (strcmp(table->fonts[i].name, name) == 0) {
            free(table->fonts[i].entries);
            table->fonts[i].entries = NULL;
            table->fonts[i].count = table->fonts[i].cap = 0;
            table->fonts[i].max_src_len = 1;
            table->fonts[i].has_widths = table->fonts[i].has_missing_width = 0;
            table->fonts[i].missing_width = 0;
            return &table->fonts[i];
        }
    }

    if (table->count >= 256) return NULL;
    if (table->count >= table->cap) {
        int new_cap = (table->cap > 0) ? table->cap * 2 : 8;
        pdf_font_map *new_fonts = (pdf_font_map *)realloc(table->fonts, (size_t)new_cap * sizeof(pdf_font_map));
        if (!new_fonts) {
            return NULL;
        }
        table->fonts = new_fonts;
        table->cap = new_cap;
    }

    pdf_font_map *font = &table->fonts[table->count++];
    memset(font, 0, sizeof(*font));
    snprintf(font->name, sizeof(font->name), "%s", name);
    font->max_src_len = 1;
    return font;
}

const pdf_font_map *cupidimage_pdf_font_table_find(const pdf_font_table *table, const char *name) {
    if (!table || !name || !*name) {
        return NULL;
    }
    for (int i = 0; i < table->count; i++) {
        if (strcmp(table->fonts[i].name, name) == 0) {
            return &table->fonts[i];
        }
    }
    return NULL;
}

static int pdf_font_add_tounicode(pdf_font_map *font, uint32_t src_code, uint8_t src_len,
                                  uint32_t dst_codepoint) {
    if (!font || src_len == 0) {
        return 0;
    }
    if (font->count >= 65536) return 0;
    if (font->count >= font->cap) {
        int new_cap = (font->cap > 0) ? font->cap * 2 : 256;
        pdf_tounicode_entry *new_entries = (pdf_tounicode_entry *)realloc(
            font->entries, (size_t)new_cap * sizeof(pdf_tounicode_entry));
        if (!new_entries) {
            return 0;
        }
        font->entries = new_entries;
        font->cap = new_cap;
    }
    font->entries[font->count].order = (uint32_t)font->count;
    font->entries[font->count].src_code = src_code;
    font->entries[font->count].src_len = src_len;
    font->entries[font->count].dst_codepoint = dst_codepoint;
    font->count++;
    if ((int)src_len > font->max_src_len) {
        font->max_src_len = (int)src_len;
    }
    return 1;
}

typedef enum {
    PDF_CMAP_TOK_EOF = 0,
    PDF_CMAP_TOK_WORD = 1,
    PDF_CMAP_TOK_HEX = 2,
    PDF_CMAP_TOK_LBRACK = 3,
    PDF_CMAP_TOK_RBRACK = 4
} pdf_cmap_tok_type;

static pdf_cmap_tok_type pdf_cmap_next_token(const unsigned char *data, size_t len, size_t *pos,
                                             char *tok, size_t tok_cap) {
    if (!data || !pos || !tok || tok_cap == 0) {
        return PDF_CMAP_TOK_EOF;
    }
    while (*pos < len) {
        unsigned char c = data[*pos];
        if (c == '%') {
            while (*pos < len && data[*pos] != '\n' && data[*pos] != '\r') {
                (*pos)++;
            }
            continue;
        }
        if (isspace(c)) {
            (*pos)++;
            continue;
        }
        break;
    }
    if (*pos >= len) {
        return PDF_CMAP_TOK_EOF;
    }

    unsigned char c = data[*pos];
    if (c == '[') {
        (*pos)++;
        tok[0] = '[';
        tok[1] = '\0';
        return PDF_CMAP_TOK_LBRACK;
    }
    if (c == ']') {
        (*pos)++;
        tok[0] = ']';
        tok[1] = '\0';
        return PDF_CMAP_TOK_RBRACK;
    }
    if (c == '<') {
        (*pos)++;
        size_t n = 0;
        while (*pos < len && data[*pos] != '>') {
            if (n + 1 < tok_cap) {
                tok[n++] = (char)data[*pos];
            }
            (*pos)++;
        }
        if (*pos < len && data[*pos] == '>') {
            (*pos)++;
        }
        tok[n] = '\0';
        return PDF_CMAP_TOK_HEX;
    }

    size_t n = 0;
    while (*pos < len) {
        c = data[*pos];
        if (isspace(c) || c == '[' || c == ']' || c == '<' || c == '>' || c == '%') {
            break;
        }
        if (n + 1 < tok_cap) {
            tok[n++] = (char)c;
        }
        (*pos)++;
    }
    tok[n] = '\0';
    return (n > 0) ? PDF_CMAP_TOK_WORD : PDF_CMAP_TOK_EOF;
}

static int pdf_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int pdf_cmap_hex_to_code(const char *hex, uint32_t *code, uint8_t *code_len, uint32_t *first_u16) {
    if (!hex || !*hex || !code || !code_len || !first_u16) {
        return 0;
    }
    size_t nhex = strlen(hex);
    if ((nhex & 1u) != 0u) {
        return 0;
    }
    size_t nbytes = nhex / 2u;
    if (nbytes == 0 || nbytes > 4u) {
        return 0;
    }

    uint8_t bytes[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < nbytes; i++) {
        int hi = pdf_hex_value(hex[i * 2u]);
        int lo = pdf_hex_value(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            return 0;
        }
        bytes[i] = (uint8_t)((hi << 4) | lo);
    }

    uint32_t v = 0;
    for (size_t i = 0; i < nbytes; i++) {
        v = (v << 8u) | bytes[i];
    }
    *code = v;
    *code_len = (uint8_t)nbytes;

    uint32_t u = 0;
    if (nbytes >= 2u) {
        u = ((uint32_t)bytes[0] << 8u) | bytes[1];
    } else {
        u = (uint32_t)bytes[0];
    }
    *first_u16 = u;
    return 1;
}

static int pdf_parse_tounicode_stream(const unsigned char *stream, size_t stream_len, pdf_font_map *font) {
    if (!stream || !font) {
        return 0;
    }

    size_t pos = 0;
    char tok[256];
    while (1) {
        pdf_cmap_tok_type t = pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
        if (t == PDF_CMAP_TOK_EOF) {
            break;
        }

        if (t == PDF_CMAP_TOK_WORD && strcmp(tok, "beginbfchar") == 0) {
            while (1) {
                pdf_cmap_tok_type a = pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                if (a == PDF_CMAP_TOK_EOF) {
                    return 0;
                }
                if (a == PDF_CMAP_TOK_WORD && strcmp(tok, "endbfchar") == 0) {
                    break;
                }
                if (a != PDF_CMAP_TOK_HEX) {
                    return 0;
                }
                char src_hex[256];
                snprintf(src_hex, sizeof(src_hex), "%s", tok);

                pdf_cmap_tok_type b = pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                if (b != PDF_CMAP_TOK_HEX) {
                    return 0;
                }

                uint32_t src_code = 0;
                uint8_t src_len = 0;
                uint8_t dst_len = 0;
                uint32_t dst = 0;
                uint32_t dummy = 0;
                if (pdf_cmap_hex_to_code(src_hex, &src_code, &src_len, &dummy) &&
                    pdf_cmap_hex_to_code(tok, &dummy, &dst_len, &dst)) {
                    if (!pdf_font_add_tounicode(font, src_code, src_len, dst)) return 0;
                } else return 0;
            }
            continue;
        }

        if (t == PDF_CMAP_TOK_WORD && strcmp(tok, "beginbfrange") == 0) {
            while (1) {
                pdf_cmap_tok_type a = pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                if (a == PDF_CMAP_TOK_EOF) {
                    return 0;
                }
                if (a == PDF_CMAP_TOK_WORD && strcmp(tok, "endbfrange") == 0) {
                    break;
                }
                if (a != PDF_CMAP_TOK_HEX) {
                    return 0;
                }
                char start_hex[256];
                snprintf(start_hex, sizeof(start_hex), "%s", tok);

                pdf_cmap_tok_type b = pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                if (b != PDF_CMAP_TOK_HEX) {
                    return 0;
                }
                char end_hex[256];
                snprintf(end_hex, sizeof(end_hex), "%s", tok);

                uint32_t start_code = 0, end_code = 0, dst_first = 0, dummy = 0;
                uint8_t start_len = 0, end_len = 0, dst_len = 0;
                if (!pdf_cmap_hex_to_code(start_hex, &start_code, &start_len, &dummy) ||
                    !pdf_cmap_hex_to_code(end_hex, &end_code, &end_len, &dummy) ||
                    start_len != end_len || end_code < start_code) {
                    return 0;
                }

                pdf_cmap_tok_type c = pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                if (c == PDF_CMAP_TOK_HEX) {
                    if (!pdf_cmap_hex_to_code(tok, &dummy, &dst_len, &dst_first)) {
                        return 0;
                    }
                    uint64_t count64 = (uint64_t)end_code - start_code + 1;
                    if (count64 > 65536u - (unsigned)font->count || count64-1 > UINT32_MAX-dst_first) return 0;
                    uint32_t count = (uint32_t)count64;
                    for (uint32_t i = 0; i < count; i++) {
                        if (!pdf_font_add_tounicode(font, start_code + i, start_len, dst_first + i)) return 0;
                    }
                } else if (c == PDF_CMAP_TOK_LBRACK) {
                    if ((uint64_t)end_code-start_code+1 > 65536u-(unsigned)font->count) return 0;
                    uint64_t current = start_code;
                    while (current <= end_code) {
                        pdf_cmap_tok_type d = pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                        if (d != PDF_CMAP_TOK_HEX || !pdf_cmap_hex_to_code(tok, &dummy, &dst_len, &dst_first)) return 0;
                        if (!pdf_font_add_tounicode(font, (uint32_t)current, start_len, dst_first)) return 0;
                        current++;
                    }
                    if (pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok)) != PDF_CMAP_TOK_RBRACK) return 0;
                } else return 0;
            }
            continue;
        }
    }

    return 1;
}

static int pdf_font_entry_compare(const void *left, const void *right) {
    const pdf_tounicode_entry *a = (const pdf_tounicode_entry *)left;
    const pdf_tounicode_entry *b = (const pdf_tounicode_entry *)right;
    if (a->src_len != b->src_len) return a->src_len < b->src_len ? -1 : 1;
    if (a->src_code != b->src_code) return a->src_code < b->src_code ? -1 : 1;
    return a->order < b->order ? -1 : a->order > b->order;
}

/* Sort once rather than searching the whole map for every insertion. Preserve
 * last-definition wins when the same source code appears more than once. */
static void pdf_font_sort_map(pdf_font_map *font) {
    if (!font->count) return;
    qsort(font->entries, (size_t)font->count, sizeof(*font->entries), pdf_font_entry_compare);
    int used = 0;
    for (int i = 0; i < font->count; i++) {
        if (used && font->entries[used-1].src_len == font->entries[i].src_len &&
            font->entries[used-1].src_code == font->entries[i].src_code) font->entries[used-1] = font->entries[i];
        else font->entries[used++] = font->entries[i];
    }
    font->count = used;
}

static int pdf_parse_font_tounicode(const unsigned char *data, size_t size,
                                    const pdf_object_view *font_view, pdf_font_map *font,
                                    char *err, size_t errcap) {
    int tounicode_obj = 0;
    size_t value_pos;
    if (!cupidimage_pdf_page_value(font_view, "ToUnicode", &value_pos)) {
        return 1;
    }
    if (!cupidimage_pdf_page_reference(font_view, value_pos, &tounicode_obj)) {
        cupidimage_set_err(err, errcap, "pdf: invalid ToUnicode reference");
        return 0;
    }

    pdf_object_view cmap_obj = {0};
    if (!cupidimage_pdf_get_object_content(data, size, tounicode_obj, &cmap_obj)) {
        cupidimage_set_err(err, errcap, "pdf: ToUnicode object not found");
        return 0;
    }

    const unsigned char *stream_ptr = NULL;
    size_t stream_len = 0, decoded_len = 0;
    unsigned char *decoded_owned = NULL;
    if (!cupidimage_pdf_object_stream(data, size, &cmap_obj, &stream_ptr, &stream_len) ||
        !cupidimage_pdf_decode_stream(&cmap_obj, stream_ptr, stream_len, &decoded_owned, &decoded_len)) {
        cupidimage_pdf_object_view_release(&cmap_obj);
        cupidimage_set_err(err, errcap, "pdf: invalid or unsupported ToUnicode stream");
        return 0;
    }
    const unsigned char *decoded = decoded_owned;

    int ok = pdf_parse_tounicode_stream(decoded, decoded_len, font);
    if (ok) pdf_font_sort_map(font);
    free(decoded_owned);
    cupidimage_pdf_object_view_release(&cmap_obj);
    if (!ok) {
        cupidimage_set_err(err, errcap, "pdf: failed to parse ToUnicode map");
        return 0;
    }
    return 1;
}

/* Resolve only the named outer dictionary entry, preserving resource scope. */
static int pdf_font_dictionary(const unsigned char *data, size_t size,
                               const pdf_object_view *parent, const char *key,
                               pdf_object_view *out) {
    size_t pos;
    int object;
    if (!cupidimage_pdf_page_value(parent, key, &pos)) return 0;
    if (cupidimage_pdf_page_reference(parent, pos, &object)) {
        if (!cupidimage_pdf_get_object_content(data, size, object, out)) return 0;
        pos = 0;
    } else { out->data = parent->data; out->len = parent->len; }
    cupidimage_pdf_skip_ws_and_comments(out->data, out->len, &pos);
    if (out->len-pos < 2 || memcmp(out->data+pos, "<<", 2)) goto fail;
    size_t start = pos;
    pos += 2;
    unsigned depth = 1;
    while (pos < out->len) {
        cupidimage_pdf_skip_ws_and_comments(out->data, out->len, &pos);
        if (pos == out->len) break;
        unsigned char c = out->data[pos++];
        if (c == '(') {
            unsigned nesting = 1;
            while (pos < out->len && nesting) {
                c = out->data[pos++];
                if (c == '\\' && pos < out->len) pos++;
                else if (c == '(') nesting++;
                else if (c == ')') nesting--;
            }
            if (nesting) break;
        } else if (c == '<') {
            if (pos < out->len && out->data[pos] == '<') { pos++; if (++depth > 64) break; }
            else {
                while (pos < out->len && out->data[pos] != '>') pos++;
                if (pos == out->len) break;
                pos++;
            }
        } else if (c == '>' && pos < out->len && out->data[pos] == '>') {
            pos++;
            if (--depth == 0) { out->data += start; out->len = pos-start; return 1; }
        }
    }
fail:
    cupidimage_pdf_object_view_release(out);
    return 0;
}

/* Simple-font widths use character codes before ToUnicode mapping. */
static int pdf_parse_font_widths(const unsigned char *data, size_t size,
                                 const pdf_object_view *view, pdf_font_map *font) {
    size_t pos;
    if (cupidimage_pdf_page_value(view,"Subtype",&pos) && view->len-pos>=6 &&
        (!memcmp(view->data+pos,"/Type0",6) || !memcmp(view->data+pos,"/Type3",6)) &&
        (view->len-pos==6 || cupidimage_pdf_is_ws(view->data[pos+6]) || cupidimage_pdf_is_delim(view->data[pos+6]))) return 1;
    if (cupidimage_pdf_page_value(view,"FontDescriptor",&pos)) {
        pdf_object_view descriptor={0};
        if (!pdf_font_dictionary(data,size,view,"FontDescriptor",&descriptor)) return 0;
        if (cupidimage_pdf_page_value(&descriptor,"MissingWidth",&pos)) {
            double width;
            int valid=cupidimage_pdf_parse_pdf_number(descriptor.data,descriptor.len,&pos,&width) &&
                isfinite(width) && fabs(width)<=1e6 &&
                (pos==descriptor.len || cupidimage_pdf_is_ws(descriptor.data[pos]) || cupidimage_pdf_is_delim(descriptor.data[pos]));
            if (!valid) { cupidimage_pdf_object_view_release(&descriptor); return 0; }
            font->missing_width=width/1000.0;
            font->has_missing_width=1;
        }
        cupidimage_pdf_object_view_release(&descriptor);
    }
    if (!cupidimage_pdf_page_value(view,"Widths",&pos)) return 1;
    int first,last;
    if (!cupidimage_pdf_dictionary_integer(data,size,view,"FirstChar",&first) ||
        !cupidimage_pdf_dictionary_integer(data,size,view,"LastChar",&last) ||
        first<0 || last<first || last>255) return 0;
    pdf_object_view array={0};
    int object;
    if (cupidimage_pdf_page_reference(view,pos,&object)) {
        if (!cupidimage_pdf_get_object_content(data,size,object,&array)) return 0;
        pos=0;
    } else { array.data=view->data; array.len=view->len; }
    cupidimage_pdf_skip_ws_and_comments(array.data,array.len,&pos);
    int valid=pos<array.len && array.data[pos++]=='[';
    for (int code=first;valid && code<=last;code++) {
        double width;
        cupidimage_pdf_skip_ws_and_comments(array.data,array.len,&pos);
        valid=cupidimage_pdf_parse_pdf_number(array.data,array.len,&pos,&width) &&
              isfinite(width) && fabs(width)<=1e6;
        if (valid) font->widths[code]=width/1000.0;
    }
    cupidimage_pdf_skip_ws_and_comments(array.data,array.len,&pos);
    valid=valid && pos<array.len && array.data[pos]==']';
    cupidimage_pdf_object_view_release(&array);
    if (!valid) return 0;
    font->first_char=first; font->last_char=last; font->has_widths=1;
    return 1;
}

static int pdf_collect_fonts_from_dict(const unsigned char *data, size_t size,
                                       const unsigned char *dict_data, size_t dict_start, size_t dict_end,
                                       pdf_font_table *table, char *err, size_t errcap) {
    pdf_object_view dictionary = {dict_data, dict_end+2, NULL};
    size_t pos = dict_start;
    while (pos < dict_end) {
        cupidimage_pdf_skip_ws_and_comments(dict_data, dict_end, &pos);
        if (pos == dict_end) break;
        if (dict_data[pos++] != '/') return 0;
        char name[32];
        size_t n = 0;
        while (pos < dict_end && !cupidimage_pdf_is_ws(dict_data[pos]) && !cupidimage_pdf_is_delim(dict_data[pos])) {
            if (n+1 == sizeof(name)) return 0;
            name[n++] = (char)dict_data[pos++];
        }
        if (!n) return 0;
        name[n] = 0;
        cupidimage_pdf_skip_ws_and_comments(dict_data, dict_end, &pos);
        pdf_object_view font_view = {0};
        if (!pdf_font_dictionary(data, size, &dictionary, name, &font_view)) return 0;
        int object;
        if (cupidimage_pdf_page_reference(&dictionary, pos, &object)) {
            double ignored;
            cupidimage_pdf_parse_pdf_number(dict_data, dict_end, &pos, &ignored);
            cupidimage_pdf_skip_ws_and_comments(dict_data, dict_end, &pos);
            cupidimage_pdf_parse_pdf_number(dict_data, dict_end, &pos, &ignored);
            cupidimage_pdf_skip_ws_and_comments(dict_data, dict_end, &pos);
            pos++;
        } else {
            /* Inline dictionaries borrow this resource dictionary's storage. */
            if (font_view.data != dict_data+pos || font_view.len > dict_end-pos) {
                cupidimage_pdf_object_view_release(&font_view); return 0;
            }
            pos += font_view.len;
        }
        pdf_font_map *font = pdf_font_table_add(table, name);
        int ok = font && pdf_parse_font_widths(data, size, &font_view, font) &&
                 pdf_parse_font_tounicode(data, size, &font_view, font, err, errcap);
        cupidimage_pdf_object_view_release(&font_view);
        if (!ok) return 0;
    }
    return 1;
}

int cupidimage_pdf_collect_page_fonts(const unsigned char *data, size_t size, int page_obj,
                                  pdf_font_table *table, char *err, size_t errcap) {
    if (!table) {
        cupidimage_set_err(err, errcap, "pdf: null font table");
        return 0;
    }

    for (unsigned depth = 0; page_obj > 0 && depth < 64; depth++) {
        pdf_object_view page = {0}, resources = {0}, fonts = {0};
        if (!cupidimage_pdf_get_object_content(data, size, page_obj, &page)) return 0;
        size_t pos;
        int parent = 0;
        if (cupidimage_pdf_page_value(&page, "Resources", &pos)) {
            int ok = pdf_font_dictionary(data, size, &page, "Resources", &resources);
            if (ok && cupidimage_pdf_page_value(&resources, "Font", &pos)) {
                ok = pdf_font_dictionary(data, size, &resources, "Font", &fonts) &&
                     pdf_collect_fonts_from_dict(data, size, fonts.data, 2, fonts.len-2, table, err, errcap);
            }
            cupidimage_pdf_object_view_release(&fonts);
            cupidimage_pdf_object_view_release(&resources);
            cupidimage_pdf_object_view_release(&page);
            return ok;
        }
        if (cupidimage_pdf_page_value(&page, "Parent", &pos)) cupidimage_pdf_page_reference(&page, pos, &parent);
        cupidimage_pdf_object_view_release(&page);
        page_obj = parent;
    }
    return page_obj == 0;
}

const uint8_t cupidimage_pdf_font8x8_basic[128][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x7E,0x81,0xA5,0x81,0xBD,0x99,0x81,0x7E},
    {0x7E,0xFF,0xDB,0xFF,0xC3,0xE7,0xFF,0x7E},
    {0x6C,0xFE,0xFE,0xFE,0x7C,0x38,0x10,0x00},
    {0x10,0x38,0x7C,0xFE,0x7C,0x38,0x10,0x00},
    {0x38,0x7C,0x38,0xFE,0xFE,0xD6,0x10,0x38},
    {0x10,0x38,0x7C,0xFE,0xFE,0x7C,0x10,0x38},
    {0x00,0x00,0x18,0x3C,0x3C,0x18,0x00,0x00},
    {0xFF,0xFF,0xE7,0xC3,0xC3,0xE7,0xFF,0xFF},
    {0x00,0x3C,0x66,0x42,0x42,0x66,0x3C,0x00},
    {0xFF,0xC3,0x99,0xBD,0xBD,0x99,0xC3,0xFF},
    {0x0F,0x07,0x0F,0x7D,0xCC,0xCC,0xCC,0x78},
    {0x3C,0x66,0x66,0x66,0x3C,0x18,0x7E,0x18},
    {0x3F,0x33,0x3F,0x30,0x30,0x70,0xF0,0xE0},
    {0x7F,0x63,0x7F,0x63,0x63,0x67,0xE6,0xC0},
    {0x99,0x5A,0x3C,0xE7,0xE7,0x3C,0x5A,0x99},
    {0x80,0xE0,0xF8,0xFE,0xF8,0xE0,0x80,0x00},
    {0x02,0x0E,0x3E,0xFE,0x3E,0x0E,0x02,0x00},
    {0x18,0x3C,0x7E,0x18,0x18,0x7E,0x3C,0x18},
    {0x66,0x66,0x66,0x66,0x66,0x00,0x66,0x00},
    {0x7F,0xDB,0xDB,0x7B,0x1B,0x1B,0x1B,0x00},
    {0x3E,0x63,0x38,0x6C,0x6C,0x38,0xCC,0x7F},
    {0x00,0x00,0x00,0x00,0x7E,0x7E,0x7E,0x00},
    {0x18,0x3C,0x7E,0x18,0x7E,0x3C,0x18,0xFF},
    {0x18,0x3C,0x7E,0x18,0x18,0x18,0x18,0x00},
    {0x18,0x18,0x18,0x18,0x7E,0x3C,0x18,0x00},
    {0x00,0x18,0x0C,0xFE,0x0C,0x18,0x00,0x00},
    {0x00,0x30,0x60,0xFE,0x60,0x30,0x00,0x00},
    {0x00,0x00,0xC0,0xC0,0xC0,0xFE,0x00,0x00},
    {0x00,0x24,0x66,0xFF,0x66,0x24,0x00,0x00},
    {0x00,0x18,0x3C,0x7E,0xFF,0xFF,0x00,0x00},
    {0x00,0xFF,0xFF,0x7E,0x3C,0x18,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    {0x36,0x36,0x12,0x00,0x00,0x00,0x00,0x00},
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00},
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00},
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00},
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00},
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06},
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00},
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00},
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00},
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00},
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00},
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00},
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00},
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06},
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00},
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00},
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00},
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00},
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00},
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00},
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00},
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00},
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00},
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00},
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00},
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00},
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00},
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00},
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00},
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00},
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00},
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00},
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00},
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00},
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00},
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00},
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00},
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00},
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00},
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00},
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F},
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00},
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x18,0x00,0x1C,0x18,0x18,0x18,0x1B,0x0E},
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00},
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00},
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00},
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00},
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F},
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78},
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00},
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00},
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00},
    {0x00,0x00,0x63,0x63,0x6B,0x7F,0x36,0x00},
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F},
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00},
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00},
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};
