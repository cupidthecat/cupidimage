#include "pdf_render_internal.h"
#include <stdlib.h>

int cupidimage_pdf_text_group_begin(pdf_render_state *st) {
    if (!st->gs.text_knockout || st->text_group)
        return 1;
    if (!st->form_budget->calls_left)
        goto fail;
    st->form_budget->calls_left--;
    pdf_transparency_group *group = calloc(1, sizeof(*group));
    if (!group)
        goto fail;
    /* Text usually touches a small part of the page. Initialize its backdrop
     * only where a glyph contributes shape, retaining the same group equations. */
    group->lazy_backdrop = 1;
    if (!cupidimage_pdf_group_surface(st, group, 0, NULL)) {
        free(group);
        goto fail;
    }
    group->knockout = 1;
    st->text_group = group;
    st->group = group;
    st->img = &group->image;
    return 1;
fail:
    cupidimage_set_err(st->error, st->error_capacity,
                       "pdf: text transparency group exceeds limits");
    return 0;
}

/* Implicit text/glyph groups do not reset or save the caller's paint state. */
int cupidimage_pdf_composite_implicit_group(pdf_render_state *st,
                                            const pdf_transparency_group *group) {
    pdf_graphics_state saved = st->gs;
    st->gs.fill_alpha = st->gs.stroke_alpha = 1;
    st->gs.blend_mode = st->gs.alpha_is_shape = 0;
    st->gs.soft_mask = NULL;
    st->gs.clip = NULL;
    int ok = cupidimage_pdf_group_finish(st, group);
    st->gs = saved;
    return ok;
}

int cupidimage_pdf_text_group_end(pdf_render_state *st) {
    pdf_transparency_group *group = st->text_group;
    if (!group)
        return 1;
    st->text_group = NULL;
    st->group = (pdf_transparency_group *)group->parent;
    st->img = (cupidimage_image *)group->backdrop;
    int ok = cupidimage_pdf_composite_implicit_group(st, group);
    cupidimage_pdf_group_free(st, group);
    free(group);
    return ok;
}

void cupidimage_pdf_text_group_discard(pdf_render_state *st) {
    pdf_transparency_group *group = st->text_group;
    if (!group)
        return;
    st->text_group = NULL;
    st->group = (pdf_transparency_group *)group->parent;
    st->img = (cupidimage_image *)group->backdrop;
    cupidimage_pdf_group_free(st, group);
    free(group);
}

int cupidimage_pdf_type3_stencil_finish(pdf_render_state *st, const pdf_transparency_group *group) {
    if (group->right <= group->left || group->bottom <= group->top)
        return 1;
    size_t pixels = (size_t)(group->right - group->left) * (group->bottom - group->top);
    if (pixels > st->clip_budget->tests_left)
        return 0;
    st->clip_budget->tests_left -= pixels;
    for (unsigned y = group->top; y < group->bottom; y++)
        for (unsigned x = group->left; x < group->right; x++) {
            double shape = group->shape[(size_t)y * group->image.width + x];
            if (shape > 0)
                cupidimage_pdf_paint_pixel(st, (int)x, (int)y, 0, shape);
        }
    return !st->image_failed;
}
