#include "pdf_render_internal.h"
#include <math.h>
#include <string.h>

static const char *pdf_blend_names[] = {
    "Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten",
    "ColorDodge", "ColorBurn", "HardLight", "SoftLight", "Difference", "Exclusion",
    "Hue", "Saturation", "Color", "Luminosity"
};

static int pdf_blend_name(const pdf_object_view *view, size_t *pos) {
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    if (*pos == view->len || view->data[(*pos)++] != '/') return -1;
    size_t start = *pos;
    while (*pos < view->len && !cupidimage_pdf_is_ws(view->data[*pos]) && !cupidimage_pdf_is_delim(view->data[*pos])) (*pos)++;
    size_t length = *pos-start;
    if (length == 10 && !memcmp(view->data+start, "Compatible", length)) return 0;
    for (int i = 0; i < 16; i++) if (strlen(pdf_blend_names[i]) == length && !memcmp(view->data+start, pdf_blend_names[i], length)) return i;
    return -1;
}

static int pdf_graphics_number(const pdf_render_state *st, const pdf_object_view *view,
                               size_t pos, unsigned depth, double *value) {
    if (depth == 16) return 0;
    int object;
    if (cupidimage_pdf_page_reference(view, pos, &object)) {
        pdf_object_view indirect = {0};
        if (!cupidimage_pdf_get_object_content(st->document, st->document_size, object, &indirect)) return 0;
        int ok = pdf_graphics_number(st, &indirect, 0, depth+1, value);
        cupidimage_pdf_object_view_release(&indirect); return ok;
    }
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    return cupidimage_pdf_parse_pdf_number(view->data, view->len, &pos, value) &&
           (pos == view->len || cupidimage_pdf_is_ws(view->data[pos]) || cupidimage_pdf_is_delim(view->data[pos]));
}

static int pdf_apply_graphics_dictionary(pdf_render_state *st, const pdf_object_view *view) {
    pdf_graphics_state next = st->gs;
    size_t pos;
    double value;
    if (cupidimage_pdf_page_value(view, "ca", &pos)) {
        if (!pdf_graphics_number(st, view, pos, 0, &value) || value < 0 || value > 1) return 0;
        next.fill_alpha = value;
    }
    if (cupidimage_pdf_page_value(view, "CA", &pos)) {
        if (!pdf_graphics_number(st, view, pos, 0, &value) || value < 0 || value > 1) return 0;
        next.stroke_alpha = value;
    }
    if (cupidimage_pdf_page_value(view, "LW", &pos)) {
        if (!pdf_graphics_number(st, view, pos, 0, &value) || value < 0 || value > 1e6) return 0;
        next.line_width_pts = (float)value;
    }
    if (cupidimage_pdf_page_value(view, "SMask", &pos) && !cupidimage_pdf_image_name_is(view, "SMask", "None")) return 0;
    if (cupidimage_pdf_page_value(view, "BM", &pos)) {
        int mode = -1;
        if (pos < view->len && view->data[pos] == '[') {
            pos++;
            for (unsigned i = 0; i < 64; i++) {
                cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
                if (pos < view->len && view->data[pos] == ']') { if (mode < 0) mode = 0; break; }
                if (pos == view->len || view->data[pos] != '/') return 0;
                int candidate = pdf_blend_name(view, &pos);
                if (mode < 0 && candidate >= 0) mode = candidate;
                if (i == 63) return 0;
            }
        } else mode = pdf_blend_name(view, &pos);
        if (mode < 0) return 0;
        next.blend_mode = mode;
    }
    st->gs = next;
    return 1;
}

int cupidimage_pdf_apply_extgstate(pdf_render_state *st, const char *name) {
    int object = st->page_object;
    for (unsigned depth = 0; object > 0 && depth < 64; depth++) {
        pdf_object_view node = {0}, resources = {0}, states = {0}, state = {0};
        if (!cupidimage_pdf_get_object_content(st->document, st->document_size, object, &node)) return 0;
        size_t pos;
        int parent = 0;
        if (cupidimage_pdf_page_value(&node, "Resources", &pos)) {
            int ok = cupidimage_pdf_image_dictionary(st, &node, "Resources", &resources) &&
                     cupidimage_pdf_image_dictionary(st, &resources, "ExtGState", &states) &&
                     cupidimage_pdf_image_dictionary(st, &states, name, &state) &&
                     pdf_apply_graphics_dictionary(st, &state);
            cupidimage_pdf_object_view_release(&state);
            cupidimage_pdf_object_view_release(&states);
            cupidimage_pdf_object_view_release(&resources);
            cupidimage_pdf_object_view_release(&node);
            return ok;
        }
        if (cupidimage_pdf_page_value(&node, "Parent", &pos)) cupidimage_pdf_page_reference(&node, pos, &parent);
        cupidimage_pdf_object_view_release(&node); object = parent;
    }
    return 0;
}

static double pdf_luminance(const double c[3]) { return .3*c[0] + .59*c[1] + .11*c[2]; }
static double pdf_saturation(const double c[3]) { return fmax(c[0], fmax(c[1], c[2])) - fmin(c[0], fmin(c[1], c[2])); }
static void pdf_set_luminance(double c[3], double target) {
    double delta = target - pdf_luminance(c);
    for (int i = 0; i < 3; i++) c[i] += delta;
    double low = fmin(c[0], fmin(c[1], c[2])), high = fmax(c[0], fmax(c[1], c[2]));
    if (low < 0) for (int i = 0; i < 3; i++) c[i] = target + (c[i]-target)*target/(target-low);
    if (high > 1) for (int i = 0; i < 3; i++) c[i] = target + (c[i]-target)*(1-target)/(high-target);
}
static void pdf_set_saturation(double c[3], double target) {
    int order[3] = {0,1,2};
    for (int i = 0; i < 2; i++) for (int j = i+1; j < 3; j++) if (c[order[i]] > c[order[j]]) { int swap=order[i]; order[i]=order[j]; order[j]=swap; }
    int low=order[0], mid=order[1], high=order[2];
    if (c[high] > c[low]) {
        c[mid] = (c[mid]-c[low])*target/(c[high]-c[low]); c[high] = target;
    } else c[mid] = c[high] = 0;
    c[low] = 0;
}

/* ISO 32000-1, 11.3.5: blend source and backdrop in the output RGB space. */
void cupidimage_pdf_blend_rgb(int mode, const uint8_t source[3], const uint8_t backdrop[3], double result[3]) {
    double s[3], b[3];
    for (int i = 0; i < 3; i++) { s[i]=source[i]/255.0; b[i]=backdrop[i]/255.0; result[i]=s[i]; }
    if (mode >= 12) {
        if (mode == 12) { pdf_set_saturation(result, pdf_saturation(b)); pdf_set_luminance(result, pdf_luminance(b)); }
        else if (mode == 13) { memcpy(result, b, sizeof(b)); pdf_set_saturation(result, pdf_saturation(s)); pdf_set_luminance(result, pdf_luminance(b)); }
        else if (mode == 14) pdf_set_luminance(result, pdf_luminance(b));
        else if (mode == 15) { memcpy(result, b, sizeof(b)); pdf_set_luminance(result, pdf_luminance(s)); }
        return;
    }
    for (int i = 0; i < 3; i++) {
        double a=s[i], c=b[i], v=a;
        switch (mode) {
            case 1: v=a*c; break;
            case 2: v=a+c-a*c; break;
            case 3: v=c <= .5 ? 2*a*c : 1-2*(1-a)*(1-c); break;
            case 4: v=fmin(a,c); break;
            case 5: v=fmax(a,c); break;
            case 6: v=a == 1 ? 1 : fmin(1,c/(1-a)); break;
            case 7: v=a == 0 ? 0 : 1-fmin(1,(1-c)/a); break;
            case 8: v=a <= .5 ? 2*a*c : 1-2*(1-a)*(1-c); break;
            case 9: v=a <= .5 ? c-(1-2*a)*c*(1-c) : c+(2*a-1)*((c <= .25 ? ((16*c-12)*c+4)*c : sqrt(c))-c); break;
            case 10: v=fabs(c-a); break;
            case 11: v=c+a-2*c*a; break;
        }
        result[i]=v;
    }
}
