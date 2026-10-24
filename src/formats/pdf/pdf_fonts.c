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
            return &table->fonts[i];
        }
    }

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
    for (int i = 0; i < font->count; i++) {
        if (font->entries[i].src_code == src_code && font->entries[i].src_len == src_len) {
            font->entries[i].dst_codepoint = dst_codepoint;
            return 1;
        }
    }
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
                    return 1;
                }
                if (a == PDF_CMAP_TOK_WORD && strcmp(tok, "endbfchar") == 0) {
                    break;
                }
                if (a != PDF_CMAP_TOK_HEX) {
                    continue;
                }
                char src_hex[256];
                snprintf(src_hex, sizeof(src_hex), "%s", tok);

                pdf_cmap_tok_type b = pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                if (b != PDF_CMAP_TOK_HEX) {
                    continue;
                }

                uint32_t src_code = 0;
                uint8_t src_len = 0;
                uint8_t dst_len = 0;
                uint32_t dst = 0;
                uint32_t dummy = 0;
                if (pdf_cmap_hex_to_code(src_hex, &src_code, &src_len, &dummy) &&
                    pdf_cmap_hex_to_code(tok, &dummy, &dst_len, &dst)) {
                    (void)pdf_font_add_tounicode(font, src_code, src_len, dst);
                }
            }
            continue;
        }

        if (t == PDF_CMAP_TOK_WORD && strcmp(tok, "beginbfrange") == 0) {
            while (1) {
                pdf_cmap_tok_type a = pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                if (a == PDF_CMAP_TOK_EOF) {
                    return 1;
                }
                if (a == PDF_CMAP_TOK_WORD && strcmp(tok, "endbfrange") == 0) {
                    break;
                }
                if (a != PDF_CMAP_TOK_HEX) {
                    continue;
                }
                char start_hex[256];
                snprintf(start_hex, sizeof(start_hex), "%s", tok);

                pdf_cmap_tok_type b = pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                if (b != PDF_CMAP_TOK_HEX) {
                    continue;
                }
                char end_hex[256];
                snprintf(end_hex, sizeof(end_hex), "%s", tok);

                uint32_t start_code = 0, end_code = 0, dst_first = 0, dummy = 0;
                uint8_t start_len = 0, end_len = 0, dst_len = 0;
                if (!pdf_cmap_hex_to_code(start_hex, &start_code, &start_len, &dummy) ||
                    !pdf_cmap_hex_to_code(end_hex, &end_code, &end_len, &dummy) ||
                    start_len != end_len || end_code < start_code) {
                    continue;
                }

                pdf_cmap_tok_type c = pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                if (c == PDF_CMAP_TOK_HEX) {
                    if (!pdf_cmap_hex_to_code(tok, &dummy, &dst_len, &dst_first)) {
                        continue;
                    }
                    uint32_t count = end_code - start_code + 1u;
                    for (uint32_t i = 0; i < count; i++) {
                        (void)pdf_font_add_tounicode(font, start_code + i, start_len, dst_first + i);
                    }
                } else if (c == PDF_CMAP_TOK_LBRACK) {
                    uint32_t current = start_code;
                    while (current <= end_code) {
                        pdf_cmap_tok_type d = pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                        if (d == PDF_CMAP_TOK_EOF || d == PDF_CMAP_TOK_RBRACK) {
                            break;
                        }
                        if (d != PDF_CMAP_TOK_HEX) {
                            continue;
                        }
                        if (!pdf_cmap_hex_to_code(tok, &dummy, &dst_len, &dst_first)) {
                            continue;
                        }
                        (void)pdf_font_add_tounicode(font, current, start_len, dst_first);
                        current++;
                    }
                }
            }
            continue;
        }
    }

    return 1;
}

static int pdf_parse_font_tounicode(const unsigned char *data, size_t size, int font_obj,
                                    pdf_font_map *font, char *err, size_t errcap) {
    pdf_object_view font_view = {0};
    if (!cupidimage_pdf_get_object_content(data, size, font_obj, &font_view)) {
        cupidimage_set_err(err, errcap, "pdf: font object not found");
        return 0;
    }

    int tounicode_obj = 0;
    if (!cupidimage_pdf_extract_dict_ref(font_view.data, 0, font_view.len, "/ToUnicode", &tounicode_obj)) {
        cupidimage_pdf_object_view_release(&font_view);
        return 1;
    }
    cupidimage_pdf_object_view_release(&font_view);

    pdf_object_view cmap_obj = {0};
    if (!cupidimage_pdf_get_object_content(data, size, tounicode_obj, &cmap_obj)) {
        cupidimage_set_err(err, errcap, "pdf: ToUnicode object not found");
        return 0;
    }

    size_t stream_kw_pos = 0;
    const unsigned char *stream_ptr = NULL;
    size_t stream_len = 0;
    if (!cupidimage_pdf_extract_stream_from_object(cmap_obj.data, cmap_obj.len, &stream_ptr, &stream_len, &stream_kw_pos)) {
        cupidimage_pdf_object_view_release(&cmap_obj);
        return 1;
    }

    const unsigned char *decoded = stream_ptr;
    size_t decoded_len = stream_len;
    unsigned char *decoded_owned = NULL;
    if (cupidimage_pdf_buffer_contains(cmap_obj.data, 0, stream_kw_pos, "/FlateDecode") ||
        cupidimage_pdf_buffer_contains(cmap_obj.data, 0, stream_kw_pos, "/Fl")) {
        if (!cupidimage_pdf_inflate_auto(stream_ptr, stream_len, &decoded_owned, &decoded_len)) {
            cupidimage_pdf_object_view_release(&cmap_obj);
            cupidimage_set_err(err, errcap, "pdf: failed to decode ToUnicode stream");
            return 0;
        }
        decoded = decoded_owned;
    }

    int ok = pdf_parse_tounicode_stream(decoded, decoded_len, font);
    free(decoded_owned);
    cupidimage_pdf_object_view_release(&cmap_obj);
    if (!ok) {
        cupidimage_set_err(err, errcap, "pdf: failed to parse ToUnicode map");
        return 0;
    }
    return 1;
}

static int pdf_find_inline_dict(const unsigned char *data, size_t start, size_t end,
                                const char *key, size_t *dict_start, size_t *dict_end) {
    size_t key_len = strlen(key);
    if (!data || !key || !dict_start || !dict_end || key_len == 0 || end <= start) {
        return 0;
    }
    for (size_t pos = start; pos + key_len < end; pos++) {
        if (memcmp(&data[pos], key, key_len) != 0) {
            continue;
        }
        size_t p = pos + key_len;
        while (p < end && (data[p] == ' ' || data[p] == '\n' || data[p] == '\r' || data[p] == '\t')) p++;
        if (p + 1 >= end || data[p] != '<' || data[p + 1] != '<') {
            continue;
        }
        int depth = 1;
        size_t q = p + 2;
        while (q + 1 < end && depth > 0) {
            if (data[q] == '<' && data[q + 1] == '<') {
                depth++;
                q += 2;
                continue;
            }
            if (data[q] == '>' && data[q + 1] == '>') {
                depth--;
                q += 2;
                continue;
            }
            q++;
        }
        if (depth == 0) {
            *dict_start = p + 2;
            *dict_end = q - 2;
            return 1;
        }
    }
    return 0;
}

static int pdf_collect_fonts_from_dict(const unsigned char *data, size_t size,
                                       const unsigned char *dict_data, size_t dict_start, size_t dict_end,
                                       pdf_font_table *table, char *err, size_t errcap) {
    size_t pos = dict_start;
    while (pos < dict_end) {
        if (dict_data[pos] != '/') {
            pos++;
            continue;
        }
        pos++;
        char name[32];
        size_t n = 0;
        while (pos < dict_end &&
               !isspace((unsigned char)dict_data[pos]) &&
               dict_data[pos] != '/' && dict_data[pos] != '<' && dict_data[pos] != '>' &&
               dict_data[pos] != '[' && dict_data[pos] != ']' && dict_data[pos] != '(' && dict_data[pos] != ')') {
            if (n + 1 < sizeof(name)) {
                name[n++] = (char)dict_data[pos];
            }
            pos++;
        }
        name[n] = '\0';
        if (n == 0) {
            continue;
        }

        while (pos < dict_end && (dict_data[pos] == ' ' || dict_data[pos] == '\n' ||
                                  dict_data[pos] == '\r' || dict_data[pos] == '\t')) pos++;
        if (pos >= dict_end || dict_data[pos] < '0' || dict_data[pos] > '9') {
            continue;
        }

        size_t obj_num;
        if (!cupidimage_pdf_parse_decimal(dict_data, dict_end, &pos, INT_MAX, &obj_num)) {
            cupidimage_set_err(err, errcap, "pdf: invalid font object number");
            return 0;
        }
        while (pos < dict_end && (dict_data[pos] == ' ' || dict_data[pos] == '\n' ||
                                  dict_data[pos] == '\r' || dict_data[pos] == '\t')) pos++;
        if (pos >= dict_end || dict_data[pos] < '0' || dict_data[pos] > '9') {
            continue;
        }
        while (pos < dict_end && dict_data[pos] >= '0' && dict_data[pos] <= '9') {
            pos++;
        }
        while (pos < dict_end && (dict_data[pos] == ' ' || dict_data[pos] == '\n' ||
                                  dict_data[pos] == '\r' || dict_data[pos] == '\t')) pos++;
        if (pos >= dict_end || dict_data[pos] != 'R' || obj_num <= 0) {
            continue;
        }
        pos++;

        pdf_font_map *font = pdf_font_table_add(table, name);
        if (!font) {
            cupidimage_set_err(err, errcap, "pdf: out of memory");
            return 0;
        }
        if (!pdf_parse_font_tounicode(data, size, (int)obj_num, font, err, errcap)) {
            return 0;
        }
    }
    return 1;
}

int cupidimage_pdf_collect_page_fonts(const unsigned char *data, size_t size, int page_obj,
                                  pdf_font_table *table, char *err, size_t errcap) {
    if (!table) {
        cupidimage_set_err(err, errcap, "pdf: null font table");
        return 0;
    }

    pdf_object_view page = {0};
    if (!cupidimage_pdf_get_object_content(data, size, page_obj, &page)) {
        cupidimage_set_err(err, errcap, "pdf: page object not found");
        return 0;
    }

    pdf_object_view resources = {0};
    const unsigned char *res_data = page.data;
    size_t res_len = page.len;
    int resources_obj = 0;
    if (cupidimage_pdf_extract_dict_ref(page.data, 0, page.len, "/Resources", &resources_obj) && resources_obj > 0) {
        if (!cupidimage_pdf_get_object_content(data, size, resources_obj, &resources)) {
            cupidimage_pdf_object_view_release(&page);
            cupidimage_set_err(err, errcap, "pdf: resources object not found");
            return 0;
        }
        res_data = resources.data;
        res_len = resources.len;
    }

    int font_dict_obj = 0;
    if (cupidimage_pdf_extract_dict_ref(res_data, 0, res_len, "/Font", &font_dict_obj) && font_dict_obj > 0) {
        pdf_object_view font_dict = {0};
        if (!cupidimage_pdf_get_object_content(data, size, font_dict_obj, &font_dict)) {
            cupidimage_pdf_object_view_release(&resources);
            cupidimage_pdf_object_view_release(&page);
            cupidimage_set_err(err, errcap, "pdf: font dictionary object not found");
            return 0;
        }
        int ok = pdf_collect_fonts_from_dict(data, size, font_dict.data, 0, font_dict.len, table, err, errcap);
        cupidimage_pdf_object_view_release(&font_dict);
        cupidimage_pdf_object_view_release(&resources);
        cupidimage_pdf_object_view_release(&page);
        return ok;
    }

    size_t dict_start = 0;
    size_t dict_end = 0;
    int found = pdf_find_inline_dict(res_data, 0, res_len, "/Font", &dict_start, &dict_end);
    int ok = 1;
    if (found) {
        ok = pdf_collect_fonts_from_dict(data, size, res_data, dict_start, dict_end, table, err, errcap);
    }

    cupidimage_pdf_object_view_release(&resources);
    cupidimage_pdf_object_view_release(&page);
    return ok;
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
