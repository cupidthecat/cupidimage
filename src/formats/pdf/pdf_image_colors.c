#include "pdf_render_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int pdf_color_hex(unsigned char c) {
    return c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : -1;
}

static int pdf_color_name(const pdf_object_view *view, size_t *pos, char name[128]) {
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    if (*pos == view->len || view->data[(*pos)++] != '/') return 0;
    size_t used = 0;
    while (*pos < view->len && !cupidimage_pdf_is_ws(view->data[*pos]) && !cupidimage_pdf_is_delim(view->data[*pos])) {
        unsigned char c = view->data[(*pos)++];
        if (c == '#') {
            if (view->len-*pos < 2) return 0;
            int a = pdf_color_hex(view->data[*pos]), b = pdf_color_hex(view->data[*pos+1]);
            if (a < 0 || b < 0) return 0;
            c = (unsigned char)(a*16+b); *pos += 2;
        }
        if (!c || used == 127) return 0;
        name[used++] = (char)c;
    }
    name[used] = 0;
    return used != 0;
}

static int pdf_color_reference(const pdf_object_view *view, size_t *pos, int *object) {
    if (!cupidimage_pdf_page_reference(view, *pos, object)) return 0;
    double ignored;
    cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &ignored);
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &ignored);
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    (*pos)++;
    return 1;
}

/* Palette strings are bounded by 256 entries of at most four components. */
static int pdf_palette_string(const pdf_object_view *view, size_t *pos, unsigned char *out, size_t *size) {
    if (*pos >= view->len) return 0;
    int hex = view->data[*pos] == '<', depth = 1, high = -1;
    if (!hex && view->data[*pos] != '(') return 0;
    (*pos)++; *size = 0;
    while (*pos < view->len) {
        unsigned char c = view->data[(*pos)++];
        if (hex) {
            if (c == '>') {
                if (high >= 0) {
                    if (*size == 1024) return 0;
                    out[(*size)++] = (unsigned char)(high*16);
                }
                return 1;
            }
            if (cupidimage_pdf_is_ws(c)) continue;
            int value = pdf_color_hex(c);
            if (value < 0) return 0;
            if (high < 0) { high = value; continue; }
            c = (unsigned char)(high*16+value); high = -1;
        } else {
            if (c == '(') depth++;
            if (c == ')' && --depth == 0) return 1;
            /* Only a closing parenthesis decrements nesting. */
            if (c == '\\') {
                if (*pos == view->len) return 0;
                c = view->data[(*pos)++];
                if (c == '\n' || c == '\r') {
                    if (c == '\r' && *pos < view->len && view->data[*pos] == '\n') (*pos)++;
                    continue;
                }
                if (c >= '0' && c <= '7') {
                    unsigned value = c-'0';
                    for (int n = 0; n < 2 && *pos < view->len && view->data[*pos] >= '0' && view->data[*pos] <= '7'; n++) value = value*8 + view->data[(*pos)++]-'0';
                    c = (unsigned char)value;
                } else if (c == 'n') c = '\n';
                else if (c == 'r') c = '\r';
                else if (c == 't') c = '\t';
                else if (c == 'b') c = '\b';
                else if (c == 'f') c = '\f';
            } else if (c == '\r') {
                if (*pos < view->len && view->data[*pos] == '\n') (*pos)++;
                c = '\n';
            }
        }
        if (*size == 1024) return 0;
        out[(*size)++] = c;
    }
    return 0;
}

static int pdf_image_palette(const pdf_render_state *st, const pdf_object_view *view, size_t *pos,
                             unsigned char *palette, size_t expected, unsigned depth) {
    if (depth >= 16) return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    int object;
    if (pdf_color_reference(view, pos, &object)) {
        pdf_object_view indirect = {0};
        if (!cupidimage_pdf_get_object_content(st->document, st->document_size, object, &indirect)) return 0;
        size_t p = 0, length = 0;
        cupidimage_pdf_skip_ws_and_comments(indirect.data, indirect.len, &p);
        int ok;
        if (indirect.len-p >= 2 && !memcmp(indirect.data+p, "<<", 2)) {
            const unsigned char *raw;
            unsigned char *decoded = NULL;
            ok = cupidimage_pdf_object_stream(st->document, st->document_size, &indirect, &raw, &length) &&
                 cupidimage_pdf_decode_stream(&indirect, raw, length, &decoded, &length) && length == expected;
            if (ok) memcpy(palette, decoded, expected);
            free(decoded);
        } else ok = pdf_image_palette(st, &indirect, &p, palette, expected, depth+1);
        cupidimage_pdf_object_view_release(&indirect);
        return ok;
    }
    size_t length = 0;
    return pdf_palette_string(view, pos, palette, &length) && length == expected;
}

int cupidimage_pdf_image_color_space(const pdf_render_state *st, const pdf_object_view *view,
                                     size_t *pos, unsigned depth, pdf_image_colors *colors) {
    if (depth >= 16) return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    int object;
    if (pdf_color_reference(view, pos, &object)) {
        pdf_object_view indirect = {0};
        if (!cupidimage_pdf_get_object_content(st->document, st->document_size, object, &indirect)) return 0;
        size_t p = 0;
        int ok = cupidimage_pdf_image_color_space(st, &indirect, &p, depth+1, colors);
        cupidimage_pdf_object_view_release(&indirect);
        return ok;
    }
    if (*pos < view->len && view->data[*pos] == '[') {
        (*pos)++;
        char family[128];
        if (!pdf_color_name(view, pos, family) || strcmp(family, "Indexed")) return 0;
        if (!cupidimage_pdf_image_color_space(st, view, pos, depth+1, colors) || colors->indexed) return 0;
        double high;
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
        if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &high) || high < 0 || high > 255 || floor(high) != high) return 0;
        colors->high = (int)high; colors->indexed = 1;
        if (!pdf_image_palette(st, view, pos, colors->palette, (size_t)(colors->high+1)*colors->components, 0)) return 0;
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
        if (*pos == view->len || view->data[(*pos)++] != ']') return 0;
        return 1;
    }
    char name[128];
    if (!pdf_color_name(view, pos, name)) return 0;
    colors->components = !strcmp(name, "DeviceGray") ? 1 : !strcmp(name, "DeviceRGB") ? 3 : !strcmp(name, "DeviceCMYK") ? 4 : 0;
    if (colors->components) return 1;
    return 0;
}
