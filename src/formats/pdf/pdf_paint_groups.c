#include "pdf_render_internal.h"
#include <string.h>

/* ISO 32000-1 11.7.4.4: a combined fill/stroke is one knockout object.
 * Full stroke shape permits direct stroke precedence. Partial shape does not. */
int cupidimage_pdf_fill_stroke_begin(pdf_render_state *st, pdf_paint_group *paint) {
    memset(paint, 0, sizeof(*paint));
    if (cupidimage_pdf_empty_color(&st->gs.fill_space, st->gs.fill_pattern) ||
        cupidimage_pdf_empty_color(&st->gs.stroke_space, st->gs.stroke_pattern) ||
        (!st->gs.stroke_space.pattern && (!st->gs.alpha_is_shape || st->gs.stroke_alpha == 1)))
        return 1;
    pdf_transparency_group *group = &paint->surface;
    group->lazy_backdrop = 1;
    if (!cupidimage_pdf_group_surface(st, group, 0, NULL)) {
        st->image_failed = 1;
        cupidimage_set_err(st->error, st->error_capacity,
                           "pdf: combined paint transparency group exceeds limits");
        return 0;
    }
    group->knockout = 1;
    paint->mask = st->gs.soft_mask;
    st->gs.soft_mask = NULL;
    st->group = group;
    st->img = &group->image;
    return 1;
}

int cupidimage_pdf_fill_stroke_end(pdf_render_state *st, pdf_paint_group *paint, int composite) {
    pdf_transparency_group *group = &paint->surface;
    if (!group->image.rgba)
        return !st->image_failed;
    st->group = (pdf_transparency_group *)group->parent;
    st->img = (cupidimage_image *)group->backdrop;
    st->gs.soft_mask = paint->mask;
    pdf_graphics_state saved = st->gs;
    /* The mask applies to the combined object, not separately to its members. */
    st->gs.fill_alpha = st->gs.stroke_alpha = 1;
    st->gs.blend_mode = 0;
    st->gs.clip = NULL;
    int ok = !composite || st->image_failed || cupidimage_pdf_group_finish(st, group);
    st->gs = saved;
    cupidimage_pdf_group_free(st, group);
    return ok && !st->image_failed;
}
