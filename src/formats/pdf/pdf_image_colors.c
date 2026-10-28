#include "pdf_render_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int pdf_color_hex(unsigned char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}

static int pdf_color_name(const pdf_object_view *view, size_t *pos, char name[128]) {
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    if (*pos == view->len || view->data[(*pos)++] != '/')
        return 0;
    size_t used = 0;
    while (*pos < view->len && !cupidimage_pdf_is_ws(view->data[*pos]) &&
           !cupidimage_pdf_is_delim(view->data[*pos])) {
        unsigned char c = view->data[(*pos)++];
        if (c == '#') {
            if (view->len - *pos < 2)
                return 0;
            int a = pdf_color_hex(view->data[*pos]), b = pdf_color_hex(view->data[*pos + 1]);
            if (a < 0 || b < 0)
                return 0;
            c = (unsigned char)(a * 16 + b);
            *pos += 2;
        }
        if (!c || used == 127)
            return 0;
        name[used++] = (char)c;
    }
    name[used] = 0;
    return used != 0;
}

static int pdf_color_reference(const pdf_object_view *view, size_t *pos, pdf_object_id *object) {
    if (!cupidimage_pdf_page_reference(view, *pos, object))
        return 0;
    double ignored;
    cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &ignored);
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &ignored);
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    (*pos)++;
    return 1;
}

/* Palette strings contain at most 256 entries of bounded component count. */
static int pdf_palette_string(const pdf_object_view *view, size_t *pos, unsigned char *out,
                              size_t *size) {
    if (*pos >= view->len)
        return 0;
    int hex = view->data[*pos] == '<', depth = 1, high = -1;
    if (!hex && view->data[*pos] != '(')
        return 0;
    (*pos)++;
    *size = 0;
    while (*pos < view->len) {
        unsigned char c = view->data[(*pos)++];
        if (hex) {
            if (c == '>') {
                if (high >= 0) {
                    if (*size == 256 * PDF_COLOR_COMPONENTS)
                        return 0;
                    out[(*size)++] = (unsigned char)(high * 16);
                }
                return 1;
            }
            if (cupidimage_pdf_is_ws(c))
                continue;
            int value = pdf_color_hex(c);
            if (value < 0)
                return 0;
            if (high < 0) {
                high = value;
                continue;
            }
            c = (unsigned char)(high * 16 + value);
            high = -1;
        } else {
            if (c == '(')
                depth++;
            if (c == ')' && --depth == 0)
                return 1;
            /* Only a closing parenthesis decrements nesting. */
            if (c == '\\') {
                if (*pos == view->len)
                    return 0;
                c = view->data[(*pos)++];
                if (c == '\n' || c == '\r') {
                    if (c == '\r' && *pos < view->len && view->data[*pos] == '\n')
                        (*pos)++;
                    continue;
                }
                if (c >= '0' && c <= '7') {
                    unsigned value = c - '0';
                    for (int n = 0; n < 2 && *pos < view->len && view->data[*pos] >= '0' &&
                                    view->data[*pos] <= '7';
                         n++)
                        value = value * 8 + view->data[(*pos)++] - '0';
                    c = (unsigned char)value;
                } else if (c == 'n')
                    c = '\n';
                else if (c == 'r')
                    c = '\r';
                else if (c == 't')
                    c = '\t';
                else if (c == 'b')
                    c = '\b';
                else if (c == 'f')
                    c = '\f';
            } else if (c == '\r') {
                if (*pos < view->len && view->data[*pos] == '\n')
                    (*pos)++;
                c = '\n';
            }
        }
        if (*size == 256 * PDF_COLOR_COMPONENTS)
            return 0;
        out[(*size)++] = c;
    }
    return 0;
}

static int pdf_image_palette(const pdf_render_state *st, const pdf_object_view *view, size_t *pos,
                             unsigned char *palette, size_t expected, unsigned depth) {
    if (depth >= 16)
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    pdf_object_id object;
    if (pdf_color_reference(view, pos, &object)) {
        pdf_object_view indirect = {0};
        if (!cupidimage_pdf_state_object(st, object, &indirect))
            return 0;
        size_t p = 0, length = 0;
        cupidimage_pdf_skip_ws_and_comments(indirect.data, indirect.len, &p);
        int ok;
        if (indirect.len - p >= 2 && !memcmp(indirect.data + p, "<<", 2)) {
            const unsigned char *raw;
            unsigned char *decoded = NULL;
            ok = cupidimage_pdf_object_stream(st->document, st->document_size, &indirect, &raw,
                                              &length) &&
                 cupidimage_pdf_decode_stream(&indirect, raw, length, &decoded, &length) &&
                 length == expected;
            if (ok)
                memcpy(palette, decoded, expected);
            free(decoded);
        } else
            ok = pdf_image_palette(st, &indirect, &p, palette, expected, depth + 1);
        cupidimage_pdf_object_view_release(&indirect);
        return ok;
    }
    size_t length = 0;
    return pdf_palette_string(view, pos, palette, &length) && length == expected;
}

/* Defaults replace a selected device space once; the replacement is not remapped again. */
static int pdf_device_color_space(const pdf_render_state *st, int components, unsigned depth,
                                  pdf_image_colors *colors, int defaults) {
    if (depth >= 16)
        return 0;
    const char *key = components == 1   ? "DefaultGray"
                      : components == 3 ? "DefaultRGB"
                                        : "DefaultCMYK";
    pdf_object_view resource = {0}, value = {0};
    int status =
        defaults ? cupidimage_pdf_find_resource_checked(st, "ColorSpace", key, &resource, NULL) : 0;
    if (status < 0)
        return 0;
    if (status > 0) {
        int ok =
            cupidimage_pdf_resolve_value(st->document, st->document_size, &resource, 0, &value, 0);
        size_t pos = 0;
        cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &pos);
        int absent = ok && value.len - pos >= 4 && !memcmp(value.data + pos, "null", 4) &&
                     (value.len - pos == 4 || cupidimage_pdf_is_ws(value.data[pos + 4]) ||
                      cupidimage_pdf_is_delim(value.data[pos + 4]));
        if (!absent)
            ok = ok && cupidimage_pdf_color_space_value(st, &value, &pos, depth + 1, colors, 0) &&
                 colors->components == components && !colors->indexed && !colors->pattern &&
                 colors->calibrated != 3;
        cupidimage_pdf_object_view_release(&value);
        cupidimage_pdf_object_view_release(&resource);
        if (!absent)
            return ok;
    }
    colors->components = components;
    colors->calibrated = 0;
    colors->icc = NULL;
    colors->tint = NULL;
    return 1;
}

int cupidimage_pdf_color_space_value(const pdf_render_state *st, const pdf_object_view *view,
                                     size_t *pos, unsigned depth, pdf_image_colors *colors,
                                     int defaults) {
    if (depth >= 16)
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    pdf_object_id object;
    if (pdf_color_reference(view, pos, &object)) {
        pdf_object_view indirect = {0};
        if (!cupidimage_pdf_state_object(st, object, &indirect))
            return 0;
        size_t p = 0;
        int ok = cupidimage_pdf_color_space_value(st, &indirect, &p, depth + 1, colors, defaults);
        cupidimage_pdf_object_view_release(&indirect);
        return ok;
    }
    if (*pos < view->len && view->data[*pos] == '[') {
        size_t start = (*pos)++;
        char family[128];
        if (!pdf_color_name(view, pos, family))
            return 0;
        if (!strcmp(family, "Separation") || !strcmp(family, "DeviceN"))
            return cupidimage_pdf_tint_space(st, view, start, pos, depth + 1, colors, defaults,
                                             !strcmp(family, "DeviceN"));
        if (!strcmp(family, "ICCBased")) {
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
            pdf_object_id profile;
            if (!pdf_color_reference(view, pos, &profile) ||
                !cupidimage_pdf_icc_space(st, profile, colors))
                return 0;
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
            return *pos < view->len && view->data[(*pos)++] == ']';
        }
        if (!strcmp(family, "CalGray") || !strcmp(family, "CalRGB") || !strcmp(family, "Lab")) {
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
            int kind = !strcmp(family, "CalGray") ? 1 : !strcmp(family, "CalRGB") ? 2 : 3;
            if (!cupidimage_pdf_calibrated_space(st, view, pos, kind, colors))
                return 0;
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
            return *pos < view->len && view->data[(*pos)++] == ']';
        }
        int device_components = !strcmp(family, "DeviceGray")   ? 1
                                : !strcmp(family, "DeviceRGB")  ? 3
                                : !strcmp(family, "DeviceCMYK") ? 4
                                                                : 0;
        if (device_components) {
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
            if (*pos == view->len || view->data[(*pos)++] != ']')
                return 0;
            return pdf_device_color_space(st, device_components, depth, colors, defaults);
        }
        if (!strcmp(family, "Pattern")) {
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
            if (*pos < view->len && view->data[*pos] == ']') {
                (*pos)++;
                colors->pattern = 1;
                colors->components = 0;
                return 1;
            }
            if (!cupidimage_pdf_color_space_value(st, view, pos, depth + 1, colors, defaults) ||
                colors->pattern)
                return 0;
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
            if (*pos == view->len || view->data[(*pos)++] != ']')
                return 0;
            colors->pattern = 1;
            return 1;
        }
        if (strcmp(family, "Indexed"))
            return 0;
        if (!cupidimage_pdf_color_space_value(st, view, pos, depth + 1, colors, defaults) ||
            colors->indexed || colors->pattern)
            return 0;
        double high;
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
        if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &high) || high < 0 ||
            high > 255 || floor(high) != high)
            return 0;
        colors->high = (int)high;
        colors->indexed = 1;
        unsigned char palette[256 * PDF_COLOR_COMPONENTS];
        size_t bytes = (size_t)colors->components * (colors->high + 1);
        if (!pdf_image_palette(st, view, pos, palette, bytes, 0) ||
            !(colors->palette = cupidimage_pdf_palette_intern(st->palettes, palette, bytes)))
            return 0;
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
        if (*pos == view->len || view->data[(*pos)++] != ']')
            return 0;
        return 1;
    }
    char name[128];
    if (!pdf_color_name(view, pos, name))
        return 0;
    if (!strcmp(name, "Pattern")) {
        colors->pattern = 1;
        colors->components = 0;
        return 1;
    }
    colors->components = !strcmp(name, "DeviceGray")   ? 1
                         : !strcmp(name, "DeviceRGB")  ? 3
                         : !strcmp(name, "DeviceCMYK") ? 4
                                                       : 0;
    if (colors->components)
        return pdf_device_color_space(st, colors->components, depth, colors, defaults);
    return 0;
}

int cupidimage_pdf_image_color_space(const pdf_render_state *st, const pdf_object_view *view,
                                     size_t *pos, unsigned depth, pdf_image_colors *colors) {
    return cupidimage_pdf_color_space_value(st, view, pos, depth, colors, 1);
}

int cupidimage_pdf_color_space(const pdf_render_state *st, const char *name, unsigned depth,
                               pdf_image_colors *colors) {
    if (!strcmp(name, "Pattern")) {
        colors->pattern = 1;
        colors->components = 0;
        return 1;
    }
    int components = !strcmp(name, "DeviceGray")   ? 1
                     : !strcmp(name, "DeviceRGB")  ? 3
                     : !strcmp(name, "DeviceCMYK") ? 4
                                                   : 0;
    if (components)
        return pdf_device_color_space(st, components, depth, colors, 1);
    if (depth >= 16)
        return 0;
    pdf_object_view value = {0};
    size_t pos = 0;
    int ok = cupidimage_pdf_find_resource(st, "ColorSpace", name, &value, NULL) &&
             cupidimage_pdf_image_color_space(st, &value, &pos, depth + 1, colors);
    cupidimage_pdf_object_view_release(&value);
    return ok;
}

/* ISO 32000-1 10.3: convert directly to the destination device space. */
void cupidimage_pdf_device_color(const double values[4], int components, int gray, double out[3]) {
    double c[4] = {0};
    for (int i = 0; i < components; i++)
        c[i] = fmin(1, fmax(0, values[i]));
    if (components == 1) {
        out[0] = out[1] = out[2] = c[0];
    } else if (gray) {
        double value = .3 * c[0] + .59 * c[1] + .11 * c[2];
        if (components == 4)
            value = 1 - fmin(1, value + c[3]);
        out[0] = out[1] = out[2] = value;
    } else {
        for (int i = 0; i < 3; i++)
            out[i] = components == 4 ? 1 - fmin(1, c[i] + c[3]) : c[i];
    }
}
