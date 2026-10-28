#include "pdf_render_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int group_keyword(const pdf_object_view *view, const char *word) {
    size_t pos = 0, length = strlen(word);
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    return view->len - pos >= length && !memcmp(view->data + pos, word, length) &&
           (view->len - pos == length || cupidimage_pdf_is_ws(view->data[pos + length]) ||
            cupidimage_pdf_is_delim(view->data[pos + length]));
}

int cupidimage_pdf_dictionary_boolean(const pdf_render_state *st, const pdf_object_view *view,
                                      const char *key, int *value) {
    size_t pos;
    if (!cupidimage_pdf_page_value(view, key, &pos))
        return 1;
    pdf_object_view entry = {0};
    int ok = cupidimage_pdf_dictionary_value(st->document, st->document_size, view, key, &entry);
    if (ok && group_keyword(&entry, "true"))
        *value = 1;
    else if (ok && group_keyword(&entry, "false"))
        *value = 0;
    else
        ok = ok && group_keyword(&entry, "null");
    cupidimage_pdf_object_view_release(&entry);
    return ok;
}

static double group_initial_alpha(const pdf_transparency_group *group, size_t pixel) {
    if (group->isolated)
        return 0;
    if (group->parent) {
        double a0 = group_initial_alpha(group->parent, pixel);
        double ag = group->parent->knockout ? 0 : group->parent->alpha[pixel];
        return ag + a0 * (1 - ag);
    }
    return group->backdrop->rgba[pixel * 4 + 3] / 255.0;
}

static void group_initial_color(const pdf_transparency_group *group, size_t pixel, double out[3]) {
    if (!group->isolated && group->parent &&
        (group->parent->knockout ||
         (group->parent->lazy_backdrop && group->parent->shape[pixel] == 0))) {
        group_initial_color(group->parent, pixel, out);
        return;
    }
    for (unsigned i = 0; i < 3; i++)
        out[i] = group->isolated ? 0
                 : group->parent ? group->parent->color[pixel * 3 + i]
                                 : group->backdrop->rgba[pixel * 4 + i] / 255.0;
}

/* ISO 32000-1 11.4.8: shape controls replacement separately from opacity.
 * A knockout member uses the group's initial backdrop, not earlier members. */
void cupidimage_pdf_group_paint(pdf_transparency_group *group, size_t pixel, const double source[3],
                                double alpha, double shape, int blend_mode) {
    unsigned x = (unsigned)(pixel % group->image.width);
    unsigned y = (unsigned)(pixel / group->image.width);
    if (x < group->left)
        group->left = x;
    if (y < group->top)
        group->top = y;
    if (x + 1 > group->right)
        group->right = x + 1;
    if (y + 1 > group->bottom)
        group->bottom = y + 1;
    double a0 = group_initial_alpha(group, pixel), ag = group->alpha[pixel];
    double previous_alpha = ag + a0 * (1 - ag);
    double base_group_alpha = group->knockout ? 0 : ag;
    double ab = base_group_alpha + a0 * (1 - base_group_alpha);
    double result_group_alpha = (1 - shape) * ag + (shape - alpha) * base_group_alpha + alpha;
    double ar = result_group_alpha + a0 * (1 - result_group_alpha);
    double *color = group->color + pixel * 3, backdrop[3], blended[3], converted[3];
    if (group->lazy_backdrop && group->shape[pixel] == 0)
        group_initial_color(group, pixel, color);
    if (group->knockout)
        group_initial_color(group, pixel, backdrop);
    else
        memcpy(backdrop, color, sizeof(backdrop));
    if (group->gray) {
        double g = source[0] == source[1] && source[1] == source[2]
                       ? source[0]
                       : .3 * source[0] + .59 * source[1] + .11 * source[2];
        converted[0] = converted[1] = converted[2] = g;
        source = converted;
    }
    cupidimage_pdf_blend_colors(blend_mode, source, backdrop, blended);
    for (unsigned i = 0; i < 3; i++) {
        double value =
            ar > 0 ? ((1 - shape) * previous_alpha * color[i] + (shape - alpha) * ab * backdrop[i] +
                      alpha * ((1 - ab) * source[i] + ab * blended[i])) /
                         ar
                   : 0;
        color[i] = fmax(0, fmin(1, value));
        group->image.rgba[pixel * 4 + i] = cupidimage_pdf_clamp_u8(color[i]);
    }
    group->alpha[pixel] = fmax(0, fmin(1, result_group_alpha));
    group->shape[pixel] = shape + group->shape[pixel] * (1 - shape);
    group->image.rgba[pixel * 4 + 3] = cupidimage_pdf_clamp_u8(ar);
}

int cupidimage_pdf_group_surface(const pdf_render_state *parent, pdf_transparency_group *group,
                                 int isolated, const double background[3]) {
    if (background)
        for (unsigned i = 0; i < 3; i++)
            if (!isfinite(background[i]))
                return 0;
    size_t pixels = (size_t)parent->img->width * parent->img->height;
    const size_t limit = PDF_TRANSPARENCY_MEMORY_LIMIT;
    if (parent->img->width && pixels / parent->img->width != parent->img->height)
        return 0;
    size_t per_pixel = 4 + 5 * sizeof(double);
    if (parent->form_budget->transparency_bytes_live > limit ||
        pixels > (limit - parent->form_budget->transparency_bytes_live) / per_pixel)
        return 0;
    group->image.rgba = calloc(pixels, 4);
    if (!group->image.rgba)
        return 0;
    group->alpha = calloc(pixels, 5 * sizeof(*group->alpha));
    if (!group->alpha) {
        free(group->image.rgba);
        group->image.rgba = NULL;
        return 0;
    }
    group->color = group->alpha + pixels;
    group->shape = group->color + pixels * 3;
    group->isolated = isolated;
    group->gray = parent->group ? parent->group->gray : 0;
    group->parent = parent->group;
    group->backdrop = parent->img;
    if (!isolated && !group->lazy_backdrop) {
        for (size_t pixel = 0; pixel < pixels; pixel++) {
            group_initial_color(group, pixel, group->color + pixel * 3);
            for (unsigned i = 0; i < 3; i++)
                group->image.rgba[pixel * 4 + i] =
                    cupidimage_pdf_clamp_u8(group->color[pixel * 3 + i]);
            group->image.rgba[pixel * 4 + 3] =
                cupidimage_pdf_clamp_u8(group_initial_alpha(group, pixel));
        }
    }
    group->image.width = parent->img->width;
    group->image.height = parent->img->height;
    group->left = group->image.width;
    group->top = group->image.height;
    group->right = group->bottom = 0;
    group->bytes = pixels * per_pixel;
    parent->form_budget->transparency_bytes_live += group->bytes;
    if (background) {
        group->left = group->top = 0;
        group->right = group->image.width;
        group->bottom = group->image.height;
        for (size_t pixel = 0; pixel < pixels; pixel++) {
            group->alpha[pixel] = group->shape[pixel] = 1;
            for (unsigned i = 0; i < 3; i++) {
                group->color[pixel * 3 + i] = fmax(0, fmin(1, background[i]));
                group->image.rgba[pixel * 4 + i] =
                    cupidimage_pdf_clamp_u8(group->color[pixel * 3 + i]);
            }
            group->image.rgba[pixel * 4 + 3] = 255;
        }
    }
    return 1;
}

int cupidimage_pdf_group_space(const pdf_render_state *st, const pdf_object_view *attributes,
                               int required, int *gray) {
    size_t pos;
    if (cupidimage_pdf_page_value(attributes, "CS", &pos)) {
        pdf_object_view space = {0};
        int ok = cupidimage_pdf_dictionary_value(st->document, st->document_size, attributes, "CS",
                                                 &space);
        if (ok && group_keyword(&space, "null"))
            ok = !required;
        else if (ok) {
            pdf_image_colors colors = {0};
            pos = 0;
            ok = cupidimage_pdf_image_color_space(st, &space, &pos, 0, &colors);
            ok = ok && (colors.components == 1 || colors.components == 3) && !colors.pattern &&
                 !colors.indexed && !colors.calibrated && !colors.icc && !colors.tint;
            if (ok)
                *gray = colors.components == 1;
        }
        cupidimage_pdf_object_view_release(&space);
        if (!ok)
            return 0;
    } else if (required)
        return 0;
    return 1;
}

int cupidimage_pdf_group_begin(pdf_render_state *child, const pdf_render_state *parent,
                               const pdf_object_view *form, pdf_transparency_group *group) {
    size_t pos;
    if (!cupidimage_pdf_page_value(form, "Group", &pos))
        return 1;
    pdf_object_view attributes = {0};
    int isolated = 0, knockout = 0, ok = 0;
    if (!cupidimage_pdf_dictionary_value(parent->document, parent->document_size, form, "Group",
                                         &attributes))
        goto done;
    if (group_keyword(&attributes, "null")) {
        ok = 1;
        goto done;
    }
    pos = 0;
    cupidimage_pdf_skip_ws_and_comments(attributes.data, attributes.len, &pos);
    if (attributes.len - pos < 2 || memcmp(attributes.data + pos, "<<", 2) ||
        !cupidimage_pdf_entry_is_name(parent->document, parent->document_size, &attributes, "S",
                                      "Transparency") ||
        !cupidimage_pdf_dictionary_boolean(parent, &attributes, "I", &isolated) ||
        !cupidimage_pdf_dictionary_boolean(parent, &attributes, "K", &knockout))
        goto done;
    int gray = parent->group ? parent->group->gray : 0;
    if (!cupidimage_pdf_group_space(child, &attributes, 0, &gray) ||
        (!isolated && gray != (parent->group ? parent->group->gray : 0)))
        goto done;
    if (!cupidimage_pdf_group_surface(parent, group, isolated, NULL))
        goto done;
    group->gray = gray;
    group->knockout = knockout;
    child->img = &group->image;
    child->group = group;
    cupidimage_pdf_soft_mask_release(child, child->gs.soft_mask);
    child->gs.soft_mask = NULL;
    child->gs.fill_alpha = child->gs.stroke_alpha = 1;
    child->gs.blend_mode = 0;
    ok = 1;
done:
    cupidimage_pdf_object_view_release(&attributes);
    return ok;
}

/* A displayed page is an isolated group, even when I is false or omitted. */
int cupidimage_pdf_page_group_begin(pdf_render_state *st, pdf_transparency_group *group) {
    pdf_object_view page = {0}, attributes = {0};
    size_t pos;
    int ok = 0, gray = 0, ignored_isolation = 0, knockout = 0;
    if (!cupidimage_pdf_state_object(st, st->page_object, &page))
        goto done;
    if (cupidimage_pdf_page_value(&page, "Group", &pos)) {
        if (!cupidimage_pdf_dictionary_value(st->document, st->document_size, &page, "Group",
                                             &attributes))
            goto done;
        if (!group_keyword(&attributes, "null")) {
            if (!cupidimage_pdf_entry_is_name(st->document, st->document_size, &attributes, "S",
                                              "Transparency") ||
                !cupidimage_pdf_dictionary_boolean(st, &attributes, "I", &ignored_isolation) ||
                !cupidimage_pdf_dictionary_boolean(st, &attributes, "K", &knockout) ||
                !cupidimage_pdf_group_space(st, &attributes, 0, &gray))
                goto done;
        }
    }
    if (!cupidimage_pdf_group_surface(st, group, 1, NULL))
        goto done;
    group->gray = gray;
    group->knockout = knockout;
    st->img = &group->image;
    st->group = group;
    ok = 1;
done:
    cupidimage_pdf_object_view_release(&attributes);
    cupidimage_pdf_object_view_release(&page);
    return ok;
}

void cupidimage_pdf_group_source(const pdf_transparency_group *group, size_t pixel, double color[3],
                                 double *alpha, double *shape) {
    *alpha = group->alpha[pixel];
    *shape = group->shape[pixel];
    double backdrop[3], a0 = group_initial_alpha(group, pixel);
    group_initial_color(group, pixel, backdrop);
    for (unsigned i = 0; i < 3; i++) {
        double cn = group->color[pixel * 3 + i];
        /* Remove the initial backdrop once, retaining precision through recomposition. */
        color[i] =
            *alpha > 0 ? fmax(0, fmin(1, cn + (cn - backdrop[i]) * a0 * (1 - *alpha) / *alpha)) : 0;
    }
}

int cupidimage_pdf_group_finish(pdf_render_state *parent, const pdf_transparency_group *group) {
    if (!group->image.rgba || group->right <= group->left || group->bottom <= group->top)
        return 1;
    size_t pixels = (size_t)(group->right - group->left) * (group->bottom - group->top);
    if (pixels > parent->clip_budget->tests_left)
        return 0;
    parent->clip_budget->tests_left -= pixels;
    for (unsigned y = group->top; y < group->bottom; y++)
        for (unsigned x = group->left; x < group->right; x++) {
            size_t pixel = (size_t)y * group->image.width + x;
            double color[3], alpha, shape;
            cupidimage_pdf_group_source(group, pixel, color, &alpha, &shape);
            if (shape <= 0)
                continue;
            cupidimage_pdf_composite_color(
                parent, (int)x, (int)y, color, alpha * parent->gs.fill_alpha,
                shape * (parent->gs.alpha_is_shape ? parent->gs.fill_alpha : 1));
        }
    return !parent->image_failed;
}

void cupidimage_pdf_group_free(pdf_render_state *parent, pdf_transparency_group *group) {
    if (group->image.rgba) {
        parent->form_budget->transparency_bytes_live -= group->bytes;
        free(group->image.rgba);
        free(group->alpha);
        memset(group, 0, sizeof(*group));
    }
}
