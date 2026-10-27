#include "pdf_render_internal.h"
#include <stdlib.h>
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
        !group_boolean(parent, &attributes, "K", &knockout) || !isolated || knockout)
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
    const size_t limit = 64u * 1024u * 1024u;
    if (parent->img->width && pixels / parent->img->width != parent->img->height) goto done;
    if (parent->form_budget->group_bytes_live > limit ||
        pixels > (limit - parent->form_budget->group_bytes_live) / 4)
        goto done;
    group->image.rgba = calloc(pixels, 4);
    if (!group->image.rgba) goto done;
    group->image.width = parent->img->width;
    group->image.height = parent->img->height;
    group->bytes = pixels * 4;
    parent->form_budget->group_bytes_live += group->bytes;
    child->img = &group->image;
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
    size_t pixels = group->bytes / 4;
    if (pixels > parent->clip_budget->tests_left) return 0;
    parent->clip_budget->tests_left -= pixels;
    for (unsigned y = 0; y < group->image.height; y++)
        for (unsigned x = 0; x < group->image.width; x++) {
            const unsigned char *p = group->image.rgba + ((size_t)y * group->image.width + x) * 4;
            if (p[3])
                cupidimage_pdf_composite_pixel(parent, (int)x, (int)y, p[0], p[1], p[2],
                                               p[3] / 255.0 * parent->gs.fill_alpha);
        }
    return !parent->image_failed;
}
void cupidimage_pdf_group_free(pdf_render_state *parent, pdf_transparency_group *group) {
    if (group->image.rgba) {
        parent->form_budget->group_bytes_live -= group->bytes;
        free(group->image.rgba);
        memset(group, 0, sizeof(*group));
    }
}
