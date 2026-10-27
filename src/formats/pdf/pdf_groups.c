#include "pdf_render_internal.h"
#include <stdlib.h>
#include <math.h>
#include <string.h>

static int group_keyword(const pdf_object_view *view, const char *word) {
    size_t pos = 0, length = strlen(word);
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    return view->len - pos >= length && !memcmp(view->data + pos, word, length) &&
           (view->len - pos == length || cupidimage_pdf_is_ws(view->data[pos + length]) ||
            cupidimage_pdf_is_delim(view->data[pos + length]));
}
static int group_boolean(const pdf_render_state *st, const pdf_object_view *view,
                         const char *key, int *value) {
    size_t pos;
    if (!cupidimage_pdf_page_value(view, key, &pos)) return 1;
    pdf_object_view entry = {0};
    int ok = cupidimage_pdf_dictionary_value(st->document, st->document_size, view, key, &entry);
    if (ok && group_keyword(&entry, "true")) *value = 1;
    else if (ok && group_keyword(&entry, "false")) *value = 0;
    else ok = ok && group_keyword(&entry, "null");
    cupidimage_pdf_object_view_release(&entry);
    return ok;
}
static double group_initial_alpha(const pdf_transparency_group *group, size_t pixel) {
    if (group->isolated) return 0;
    if (group->parent) {
        double a0 = group_initial_alpha(group->parent, pixel);
        double ag = group->parent->alpha[pixel];
        return ag + a0 * (1 - ag);
    }
    return group->backdrop->rgba[pixel * 4 + 3] / 255.0;
}
static void group_initial_color(const pdf_transparency_group *group, size_t pixel, double out[3]) {
    for (unsigned i = 0; i < 3; i++)
        out[i] = group->isolated ? 0 : group->parent ? group->parent->color[pixel * 3 + i]
                                                          : group->backdrop->rgba[pixel * 4 + i] / 255.0;
}
void cupidimage_pdf_group_paint(pdf_transparency_group *group, size_t pixel,
                                const double source[3], double alpha, int blend_mode) {
    double a0 = group_initial_alpha(group, pixel), ag = group->alpha[pixel];
    double ab = ag + a0 * (1 - ag), ar = alpha + ab * (1 - alpha);
    if (ar <= 0) return;
    double *color = group->color + pixel * 3, blended[3];
    cupidimage_pdf_blend_colors(blend_mode, source, color, blended);
    for (unsigned i = 0; i < 3; i++) {
        double value = (alpha * ((1 - ab) * source[i] + ab * blended[i]) +
                        ab * (1 - alpha) * color[i]) / ar;
        color[i] = fmax(0, fmin(1, value));
        group->image.rgba[pixel * 4 + i] = cupidimage_pdf_clamp_u8(color[i]);
    }
    group->alpha[pixel] = fmin(1, alpha + ag * (1 - alpha));
    group->image.rgba[pixel * 4 + 3] = cupidimage_pdf_clamp_u8(ar);
}
int cupidimage_pdf_group_begin(pdf_render_state *child, const pdf_render_state *parent,
                               const pdf_object_view *form, pdf_transparency_group *group) {
    size_t pos;
    if (!cupidimage_pdf_page_value(form, "Group", &pos)) return 1;
    pdf_object_view attributes = {0}, space = {0};
    int isolated = 0, knockout = 0, ok = 0;
    if (!cupidimage_pdf_dictionary_value(parent->document, parent->document_size, form, "Group", &attributes))
        goto done;
    if (group_keyword(&attributes, "null")) { ok = 1; goto done; }
    pos = 0;
    cupidimage_pdf_skip_ws_and_comments(attributes.data, attributes.len, &pos);
    if (attributes.len - pos < 2 || memcmp(attributes.data + pos, "<<", 2) ||
        !cupidimage_pdf_entry_is_name(parent->document, parent->document_size, &attributes, "S", "Transparency") ||
        !group_boolean(parent, &attributes, "I", &isolated) ||
        !group_boolean(parent, &attributes, "K", &knockout) || knockout)
        goto done;
    if (cupidimage_pdf_page_value(&attributes, "CS", &pos)) {
        if (!cupidimage_pdf_dictionary_value(parent->document, parent->document_size, &attributes, "CS", &space))
            goto done;
        if (!group_keyword(&space, "null")) {
            pdf_image_colors colors = {0};
            pos = 0;
            char name[128];
            int valid;
            if (cupidimage_pdf_read_name(&space, &pos, name))
                valid = cupidimage_pdf_color_space(child, name, 0, &colors);
            else {
                pos = 0;
                valid = cupidimage_pdf_image_color_space(child, &space, &pos, 0, &colors);
            }
            if (!valid || colors.components != 3 || colors.pattern || colors.indexed) goto done;
        }
    }
    pdf_object_view default_rgb = {0};
    int remapped = cupidimage_pdf_find_resource(child, "ColorSpace", "DefaultRGB", &default_rgb, NULL);
    cupidimage_pdf_object_view_release(&default_rgb);
    if (remapped) goto done;
    size_t pixels = (size_t)parent->img->width * parent->img->height;
    const size_t limit = PDF_GROUP_MEMORY_LIMIT;
    if (parent->img->width && pixels / parent->img->width != parent->img->height) goto done;
    size_t per_pixel = 4 + 4 * sizeof(double);
    if (parent->form_budget->group_bytes_live > limit ||
        pixels > (limit - parent->form_budget->group_bytes_live) / per_pixel)
        goto done;
    group->image.rgba = calloc(pixels, 4);
    if (!group->image.rgba) goto done;
    group->alpha = calloc(pixels, 4 * sizeof(*group->alpha));
    if (!group->alpha) { free(group->image.rgba); group->image.rgba = NULL; goto done; }
    group->color = group->alpha + pixels;
    group->isolated = isolated;
    group->parent = parent->group;
    group->backdrop = parent->img;
    if (!isolated) {
        memcpy(group->image.rgba, parent->img->rgba, pixels * 4);
        for (size_t i = 0; i < pixels; i++) group_initial_color(group, i, group->color + i * 3);
    }
    group->image.width = parent->img->width;
    group->image.height = parent->img->height;
    group->bytes = pixels * per_pixel;
    parent->form_budget->group_bytes_live += group->bytes;
    child->img = &group->image;
    child->group = group;
    child->gs.fill_alpha = child->gs.stroke_alpha = 1;
    child->gs.blend_mode = 0;
    ok = 1;
done:
    cupidimage_pdf_object_view_release(&space);
    cupidimage_pdf_object_view_release(&attributes);
    return ok;
}
int cupidimage_pdf_group_finish(pdf_render_state *parent, const pdf_transparency_group *group) {
    if (!group->image.rgba) return 1;
    size_t pixels = (size_t)group->image.width * group->image.height;
    if (pixels > parent->clip_budget->tests_left) return 0;
    parent->clip_budget->tests_left -= pixels;
    for (unsigned y = 0; y < group->image.height; y++)
        for (unsigned x = 0; x < group->image.width; x++) {
            size_t pixel = (size_t)y * group->image.width + x;
            double alpha = group->alpha[pixel];
            if (alpha <= 0) continue;
            double color[3], backdrop[3], a0 = group_initial_alpha(group, pixel);
            group_initial_color(group, pixel, backdrop);
            for (unsigned i = 0; i < 3; i++) {
                double cn = group->color[pixel * 3 + i];
                /* Remove the initial backdrop once, retaining precision through recomposition. */
                color[i] = fmax(0, fmin(1, cn + (cn - backdrop[i]) * a0 * (1 - alpha) / alpha));
            }
            cupidimage_pdf_composite_color(parent, (int)x, (int)y, color,
                                           alpha * parent->gs.fill_alpha);
        }
    return !parent->image_failed;
}
void cupidimage_pdf_group_free(pdf_render_state *parent, pdf_transparency_group *group) {
    if (group->image.rgba) {
        parent->form_budget->group_bytes_live -= group->bytes;
        free(group->image.rgba);
        free(group->alpha);
        memset(group, 0, sizeof(*group));
    }
}
