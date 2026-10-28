#include "pdf_render_internal.h"
#include <math.h>
#include <string.h>

static const char *pdf_blend_names[] = {"Normal",    "Multiply",   "Screen",     "Overlay",
                                        "Darken",    "Lighten",    "ColorDodge", "ColorBurn",
                                        "HardLight", "SoftLight",  "Difference", "Exclusion",
                                        "Hue",       "Saturation", "Color",      "Luminosity"};

static int pdf_blend_name(const pdf_object_view *view, size_t *pos) {
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    size_t start = *pos;
    char name[128];
    if (!cupidimage_pdf_read_name(view, pos, name)) {
        if (start < view->len && view->data[start] == '/' && *pos == start + 1)
            return -1;
        return -2;
    }
    if (!strcmp(name, "Compatible"))
        return 0;
    for (int i = 0; i < 16; i++)
        if (!strcmp(name, pdf_blend_names[i]))
            return i;
    return -1;
}

static int pdf_blend_element(const pdf_render_state *st, const pdf_object_view *view, size_t *pos) {
    pdf_object_id object;
    if (!cupidimage_pdf_page_reference(view, *pos, &object))
        return pdf_blend_name(view, pos);
    pdf_object_view value = {0};
    if (!cupidimage_pdf_resolve_value(st->document, st->document_size, view, *pos, &value, 0))
        return -2;
    size_t at = 0;
    int mode = pdf_blend_name(&value, &at);
    cupidimage_pdf_object_view_release(&value);
    double ignored;
    for (unsigned i = 0; i < 2; i++) {
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
        if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &ignored))
            return -2;
    }
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    if (*pos == view->len || view->data[(*pos)++] != 'R')
        return -2;
    return mode;
}

static int pdf_read_blend_mode(const pdf_render_state *st, const pdf_object_view *view, int *mode) {
    pdf_object_view value = {0};
    int present =
        cupidimage_pdf_optional_value(st->document, st->document_size, view, "BM", &value);
    if (present <= 0)
        return present == 0;
    size_t pos = 0;
    int ok = 0, chosen = -1;
    cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &pos);
    if (pos < value.len && value.data[pos] == '[') {
        pos++;
        for (unsigned i = 0; i <= 64; i++) {
            cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &pos);
            if (pos < value.len && value.data[pos] == ']') {
                ok = 1;
                if (chosen < 0)
                    chosen = 0;
                break;
            }
            if (i == 64)
                break;
            int candidate = pdf_blend_element(st, &value, &pos);
            if (candidate == -2)
                break;
            if (chosen < 0 && candidate >= 0)
                chosen = candidate;
        }
    } else {
        chosen = pdf_blend_name(&value, &pos);
        ok = chosen >= 0;
    }
    if (ok)
        *mode = chosen;
    cupidimage_pdf_object_view_release(&value);
    return ok;
}

int cupidimage_pdf_read_number(const pdf_render_state *st, const pdf_object_view *view, size_t pos,
                               unsigned depth, double *value) {
    if (depth == 16)
        return 0;
    pdf_object_id object;
    if (cupidimage_pdf_page_reference(view, pos, &object)) {
        pdf_object_view indirect = {0};
        if (!cupidimage_pdf_state_object(st, object, &indirect))
            return 0;
        int ok = cupidimage_pdf_read_number(st, &indirect, 0, depth + 1, value);
        cupidimage_pdf_object_view_release(&indirect);
        return ok;
    }
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    return cupidimage_pdf_parse_pdf_number(view->data, view->len, &pos, value) &&
           (pos == view->len || cupidimage_pdf_is_ws(view->data[pos]) ||
            cupidimage_pdf_is_delim(view->data[pos]));
}

int cupidimage_pdf_graphics_number(const pdf_render_state *st, const pdf_object_view *view,
                                   const char *key, double minimum, double maximum,
                                   double *number) {
    pdf_object_view value = {0};
    int present = cupidimage_pdf_optional_value(st->document, st->document_size, view, key, &value);
    if (present <= 0)
        return present == 0;
    double parsed;
    int ok = cupidimage_pdf_read_number(st, &value, 0, 0, &parsed) && isfinite(parsed) &&
             parsed >= minimum && parsed <= maximum;
    cupidimage_pdf_object_view_release(&value);
    if (ok)
        *number = parsed;
    return ok;
}

static int pdf_apply_graphics_view(pdf_render_state *st, const pdf_object_view *view, int shading) {
    pdf_graphics_state next = st->gs;
    size_t pos;
    double value;
    if (!shading && cupidimage_pdf_page_value(view, "Font", &pos)) {
        pdf_object_id object;
        double font_size;
        if (!cupidimage_pdf_graphics_font(st->document, st->document_size, view, &object,
                                          &font_size))
            return 0;
        if (object) {
            next.current_font = cupidimage_pdf_font_table_object(st->font_table, object);
            if (!next.current_font)
                return 0;
            next.font_size_pts = font_size;
        }
    }
    if (!cupidimage_pdf_graphics_number(st, view, "ca", 0, 1, &next.fill_alpha))
        return 0;
    if (!shading) {
        value = next.line_width_pts;
        if (!cupidimage_pdf_graphics_number(st, view, "CA", 0, 1, &next.stroke_alpha) ||
            !cupidimage_pdf_graphics_number(st, view, "LW", 0, 1e6, &value))
            return 0;
        next.line_width_pts = (float)value;
    }
    if (!cupidimage_pdf_dictionary_boolean(st, view, "AIS", &next.alpha_is_shape) ||
        (!shading && !cupidimage_pdf_dictionary_boolean(st, view, "TK", &next.text_knockout)) ||
        (st->in_text && next.text_knockout != st->gs.text_knockout))
        return 0;
    if (!shading && !cupidimage_pdf_apply_stroke_view(st, view, &next))
        return 0;
    if (!cupidimage_pdf_read_intent(st, view, "RI", &next.rendering_intent) ||
        !pdf_read_blend_mode(st, view, &next.blend_mode))
        return 0;
    if (st->glyph_uncolored) {
        next.fill_alpha = st->gs.fill_alpha;
        next.stroke_alpha = st->gs.stroke_alpha;
        next.blend_mode = st->gs.blend_mode;
        next.alpha_is_shape = st->gs.alpha_is_shape;
    }
    /* Cached cells are backdrop-independent only with Normal blending. */
    if (st->pattern_depth && !st->mask_rendering && next.blend_mode != 0 && !st->pattern_deferred) {
        if (st->pattern_needs_backdrop)
            *st->pattern_needs_backdrop = 1;
        return 0;
    }
    if (!st->glyph_uncolored && cupidimage_pdf_page_value(view, "SMask", &pos)) {
        pdf_soft_mask *mask = NULL;
        int replace;
        if (!cupidimage_pdf_load_soft_mask(st, &next, view, &mask, &replace))
            return 0;
        if (replace) {
            cupidimage_pdf_soft_mask_release(st, st->gs.soft_mask);
            next.soft_mask = mask;
        }
    }
    st->gs = next;
    return 1;
}

int cupidimage_pdf_apply_graphics_view(pdf_render_state *st, const pdf_object_view *view) {
    return pdf_apply_graphics_view(st, view, 0);
}

int cupidimage_pdf_apply_shading_graphics_view(pdf_render_state *st, const pdf_object_view *view) {
    return pdf_apply_graphics_view(st, view, 1);
}

int cupidimage_pdf_apply_extgstate(pdf_render_state *st, const char *name) {
    pdf_object_view state = {0};
    int ok = cupidimage_pdf_find_resource(st, "ExtGState", name, &state, NULL);
    size_t pos = 0;
    cupidimage_pdf_skip_ws_and_comments(state.data, state.len, &pos);
    ok = ok && state.len - pos >= 2 && state.data[pos] == '<' && state.data[pos + 1] == '<' &&
         cupidimage_pdf_apply_graphics_view(st, &state);
    cupidimage_pdf_object_view_release(&state);
    return ok;
}

static double pdf_luminance(const double c[3]) {
    return .3 * c[0] + .59 * c[1] + .11 * c[2];
}

static double pdf_saturation(const double c[3]) {
    return fmax(c[0], fmax(c[1], c[2])) - fmin(c[0], fmin(c[1], c[2]));
}

static void pdf_set_luminance(double c[3], double target) {
    double delta = target - pdf_luminance(c);
    for (int i = 0; i < 3; i++)
        c[i] += delta;
    double low = fmin(c[0], fmin(c[1], c[2])), high = fmax(c[0], fmax(c[1], c[2]));
    if (low < 0)
        for (int i = 0; i < 3; i++)
            c[i] = target + (c[i] - target) * target / (target - low);
    if (high > 1)
        for (int i = 0; i < 3; i++)
            c[i] = target + (c[i] - target) * (1 - target) / (high - target);
}

static void pdf_set_saturation(double c[3], double target) {
    int order[3] = {0, 1, 2};
    for (int i = 0; i < 2; i++)
        for (int j = i + 1; j < 3; j++)
            if (c[order[i]] > c[order[j]]) {
                int swap = order[i];
                order[i] = order[j];
                order[j] = swap;
            }
    int low = order[0], mid = order[1], high = order[2];
    if (c[high] > c[low]) {
        c[mid] = (c[mid] - c[low]) * target / (c[high] - c[low]);
        c[high] = target;
    } else
        c[mid] = c[high] = 0;
    c[low] = 0;
}

/* ISO 32000-1, 11.3.5: blend source and backdrop in the output RGB space. */
void cupidimage_pdf_blend_rgb(int mode, const uint8_t source[3], const uint8_t backdrop[3],
                              double result[3]) {
    double s[3], b[3];
    for (int i = 0; i < 3; i++) {
        s[i] = source[i] / 255.0;
        b[i] = backdrop[i] / 255.0;
    }
    cupidimage_pdf_blend_colors(mode, s, b, result);
}

void cupidimage_pdf_blend_colors(int mode, const double source[3], const double backdrop[3],
                                 double result[3]) {
    double s[3], b[3];
    for (int i = 0; i < 3; i++) {
        s[i] = source[i];
        b[i] = backdrop[i];
        result[i] = s[i];
    }
    if (mode >= 12) {
        if (mode == 12) {
            pdf_set_saturation(result, pdf_saturation(b));
            pdf_set_luminance(result, pdf_luminance(b));
        } else if (mode == 13) {
            memcpy(result, b, sizeof(b));
            pdf_set_saturation(result, pdf_saturation(s));
            pdf_set_luminance(result, pdf_luminance(b));
        } else if (mode == 14)
            pdf_set_luminance(result, pdf_luminance(b));
        else if (mode == 15) {
            memcpy(result, b, sizeof(b));
            pdf_set_luminance(result, pdf_luminance(s));
        }
        return;
    }
    for (int i = 0; i < 3; i++) {
        double a = s[i], c = b[i], v = a;
        switch (mode) {
            case 1:
                v = a * c;
                break;
            case 2:
                v = a + c - a * c;
                break;
            case 3:
                v = c <= .5 ? 2 * a * c : 1 - 2 * (1 - a) * (1 - c);
                break;
            case 4:
                v = fmin(a, c);
                break;
            case 5:
                v = fmax(a, c);
                break;
            case 6:
                v = a == 1 ? 1 : fmin(1, c / (1 - a));
                break;
            case 7:
                v = a == 0 ? 0 : 1 - fmin(1, (1 - c) / a);
                break;
            case 8:
                v = a <= .5 ? 2 * a * c : 1 - 2 * (1 - a) * (1 - c);
                break;
            case 9:
                v = a <= .5 ? c - (1 - 2 * a) * c * (1 - c)
                            : c + (2 * a - 1) *
                                      ((c <= .25 ? ((16 * c - 12) * c + 4) * c : sqrt(c)) - c);
                break;
            case 10:
                v = fabs(c - a);
                break;
            case 11:
                v = c + a - 2 * c * a;
                break;
        }
        result[i] = v;
    }
}
