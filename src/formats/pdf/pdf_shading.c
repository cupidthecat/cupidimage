#include "pdf_functions.h"
#include <math.h>
#include <string.h>

int cupidimage_pdf_shading_colors(const pdf_render_state *st, const pdf_object_view *view,
                                  pdf_image_colors *colors) {
    pdf_object_view value = {0};
    size_t pos = 0;
    int ok = cupidimage_pdf_dictionary_value(st->document, st->document_size, view, "ColorSpace",
                                             &value);
    if (ok)
        ok = cupidimage_pdf_image_color_space(st, &value, &pos, 0, colors);
    cupidimage_pdf_object_view_release(&value);
    return ok;
}

static int pdf_shading_extend(const pdf_object_view *view, int extend[2]) {
    size_t pos;
    if (!cupidimage_pdf_page_value(view, "Extend", &pos))
        return 1;
    if (pos == view->len || view->data[pos++] != '[')
        return 0;
    for (int i = 0; i < 2; i++) {
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
        const char *word =
            view->len - pos >= 4 && !memcmp(view->data + pos, "true", 4) ? "true" : "false";
        size_t length = strlen(word);
        if (view->len - pos < length || memcmp(view->data + pos, word, length))
            return 0;
        pos += length;
        extend[i] = length == 4;
        if (pos < view->len && !cupidimage_pdf_is_ws(view->data[pos]) &&
            !cupidimage_pdf_is_delim(view->data[pos]))
            return 0;
    }
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    return pos < view->len && view->data[pos] == ']';
}

static int pdf_radial_parameter(const double coords[6], const int extend[2], double x, double y,
                                double *parameter) {
    double dx = coords[3] - coords[0], dy = coords[4] - coords[1], dr = coords[5] - coords[2];
    double px = x - coords[0], py = y - coords[1];
    double a = dx * dx + dy * dy - dr * dr, b = -2 * (px * dx + py * dy + coords[2] * dr),
           c = px * px + py * py - coords[2] * coords[2];
    double roots[2];
    int count = 0;
    if (!isfinite(a) || !isfinite(b) || !isfinite(c))
        return -1;
    if (a == 0) {
        if (b == 0)
            return 0;
        roots[count++] = -c / b;
    } else {
        double discriminant = b * b - 4 * a * c;
        if (!isfinite(discriminant))
            return -1;
        if (discriminant < 0)
            return 0;
        double q = -.5 * (b + copysign(sqrt(discriminant), b));
        if (q == 0)
            roots[count++] = -b / (2 * a);
        else {
            roots[count++] = q / a;
            roots[count++] = c / q;
        }
    }
    int found = 0;
    for (int i = 0; i < count; i++) {
        double t = roots[i];
        if (!isfinite(t) || coords[2] + t * dr < 0 || (t < 0 && !extend[0]) ||
            (t > 1 && !extend[1]))
            continue;
        if (!found || t > *parameter) {
            *parameter = t;
            found = 1;
        }
    }
    return found;
}

static int pdf_paint_shading_loaded(pdf_render_state *st, const pdf_object_view *view,
                                    pdf_function_tree *function, int background) {
    int type;
    size_t pos;
    pdf_image_colors colors = {0};
    double coords[6] = {0}, domain[4] = {0, 1, 0, 1}, box[4], matrix[6] = {1, 0, 0, 1, 0, 0};
    int extend[2] = {0}, has_box = cupidimage_pdf_page_value(view, "BBox", &pos);
    if (!cupidimage_pdf_image_integer(st, view, "ShadingType", &type) || type < 1 || type > 3 ||
        !cupidimage_pdf_shading_colors(st, view, &colors) || colors.indexed || colors.pattern)
        return 0;
    int inputs = type == 1 ? 2 : 1;
    if (!cupidimage_pdf_load_function(st, view, "Function", inputs, colors.components, function))
        return 0;
    if (type != 1 &&
        (!cupidimage_pdf_function_array(st, view, "Coords", coords, type == 2 ? 4 : 6) ||
         !pdf_shading_extend(view, extend)))
        return 0;
    if (cupidimage_pdf_page_value(view, "Domain", &pos) &&
        !cupidimage_pdf_function_array(st, view, "Domain", domain, inputs * 2))
        return 0;
    for (int axis = 0; axis < inputs; axis++) {
        if (fmin(domain[axis * 2], domain[axis * 2 + 1]) < function->nodes[0].domain[axis * 2] ||
            fmax(domain[axis * 2], domain[axis * 2 + 1]) > function->nodes[0].domain[axis * 2 + 1])
            return 0;
        if (type == 1 && domain[axis * 2] > domain[axis * 2 + 1])
            return 0;
    }
    if (has_box && (!cupidimage_pdf_function_array(st, view, "BBox", box, 4) || box[0] >= box[2] ||
                    box[1] >= box[3]))
        return 0;
    if (type == 3 && (coords[2] < 0 || coords[5] < 0))
        return 0;
    if (type == 1 && cupidimage_pdf_page_value(view, "Matrix", &pos) &&
        !cupidimage_pdf_function_array(st, view, "Matrix", matrix, 6))
        return 0;
    double shading_det = matrix[0] * matrix[3] - matrix[1] * matrix[2];
    if (!isfinite(shading_det))
        return 0;
    int empty = type == 1 && (shading_det == 0 || domain[0] == domain[1] || domain[2] == domain[3]);
    const float *m = st->gs.ctm;
    double det = (double)m[0] * m[3] - (double)m[1] * m[2];
    if (!isfinite(det))
        return 0;
    if (det == 0)
        return 1;
    double dx = coords[2] - coords[0], dy = coords[3] - coords[1], length = dx * dx + dy * dy;
    if (type == 2 && !isfinite(length))
        return 0;
    if (type == 2 && length == 0)
        empty = 1;
    if (type == 3 && coords[2] == 0 && coords[5] == 0)
        empty = 1;
    double background_values[PDF_COLOR_COMPONENTS] = {0};
    background = background && cupidimage_pdf_page_value(view, "Background", &pos);
    if (background) {
        if (!cupidimage_pdf_function_array(st, view, "Background", background_values,
                                           colors.components))
            return 0;
    }
    if (empty && !background)
        return 1;
    size_t pixels = (size_t)st->img->width * st->img->height;
    if (!empty && pixels > st->form_budget->shade_evaluations_left)
        return 0;
    for (uint32_t y = 0; y < st->img->height; y++)
        for (uint32_t x = 0; x < st->img->width; x++) {
            double tx = st->page_x0_pts + (x + .5) * (72.0 / 96.0) - m[4];
            double ty = st->page_top_pts - (y + .5) * (72.0 / 96.0) - m[5];
            double ux = (m[3] * tx - m[2] * ty) / det, uy = (m[0] * ty - m[1] * tx) / det;
            if (has_box && (ux < box[0] || ux >= box[2] || uy < box[1] || uy >= box[3]))
                continue;
            if (background)
                cupidimage_pdf_composite_space(st, (int)x, (int)y, background_values, &colors,
                                               st->gs.fill_alpha,
                                               st->gs.alpha_is_shape ? st->gs.fill_alpha : 1);
            if (empty)
                continue;
            double input[2] = {0}, values[PDF_COLOR_COMPONENTS];
            if (type == 1) {
                double mx = ux - matrix[4], my = uy - matrix[5];
                input[0] = (matrix[3] * mx - matrix[2] * my) / shading_det;
                input[1] = (matrix[0] * my - matrix[1] * mx) / shading_det;
                if (input[0] < domain[0] || input[0] > domain[1] || input[1] < domain[2] ||
                    input[1] > domain[3])
                    continue;
            } else {
                double t = 0;
                if (type == 2)
                    t = ((ux - coords[0]) * dx + (uy - coords[1]) * dy) / length;
                else {
                    int found = pdf_radial_parameter(coords, extend, ux, uy, &t);
                    if (found < 0)
                        return 0;
                    if (!found)
                        continue;
                }
                if (!isfinite(t))
                    return 0;
                if ((t < 0 && !extend[0]) || (t > 1 && !extend[1]))
                    continue;
                t = fmin(1, fmax(0, t));
                input[0] = domain[0] + t * (domain[1] - domain[0]);
            }
            if (!cupidimage_pdf_evaluate_function(function, input, values,
                                                  &st->form_budget->shade_evaluations_left))
                return 0;
            cupidimage_pdf_composite_space(st, (int)x, (int)y, values, &colors, st->gs.fill_alpha,
                                           st->gs.alpha_is_shape ? st->gs.fill_alpha : 1);
        }
    return 1;
}

int cupidimage_pdf_paint_shading_view(pdf_render_state *st, const pdf_object_view *view,
                                      int background) {
    int type;
    if (!cupidimage_pdf_image_integer(st, view, "ShadingType", &type))
        return 0;
    if (type >= 4 && type <= 7)
        return cupidimage_pdf_paint_mesh(st, view, type, background);
    pdf_function_tree function = {0};
    int ok = pdf_paint_shading_loaded(st, view, &function, background);
    cupidimage_pdf_free_function(&function);
    return ok;
}

int cupidimage_pdf_draw_shading(pdf_render_state *st, const char *name) {
    if (!st->form_budget->calls_left)
        return 0;
    st->form_budget->calls_left--;
    pdf_object_view shading = {0};
    int ok = cupidimage_pdf_find_resource(st, "Shading", name, &shading, NULL) &&
             cupidimage_pdf_paint_shading_view(st, &shading, 0);
    cupidimage_pdf_object_view_release(&shading);
    return ok;
}
