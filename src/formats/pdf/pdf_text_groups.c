#include "pdf_render_internal.h"
#include <stdlib.h>

int cupidimage_pdf_text_group_begin(pdf_render_state *st)
{
    if (!st->gs.text_knockout)
        return 1;
    if (!st->form_budget->calls_left)
        return 0;
    st->form_budget->calls_left--;
    pdf_transparency_group *group = calloc(1, sizeof(*group));
    if (!group)
        return 0;
    if (!cupidimage_pdf_group_surface(st, group, 0, NULL)) {
        free(group);
        return 0;
    }
    group->knockout = 1;
    st->text_group = group;
    st->group = group;
    st->img = &group->image;
    return 1;
}

/* Implicit text/glyph groups do not reset or save the caller's paint state. */
int cupidimage_pdf_composite_implicit_group(pdf_render_state *st,
                                            const pdf_transparency_group *group)
{
    pdf_graphics_state saved = st->gs;
    st->gs.fill_alpha = st->gs.stroke_alpha = 1;
    st->gs.blend_mode = st->gs.alpha_is_shape = 0;
    st->gs.soft_mask = NULL;
    st->gs.clip = NULL;
    int ok = cupidimage_pdf_group_finish(st, group);
    st->gs = saved;
    return ok;
}

int cupidimage_pdf_text_group_end(pdf_render_state *st)
{
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

void cupidimage_pdf_text_group_discard(pdf_render_state *st)
{
    pdf_transparency_group *group = st->text_group;
    if (!group)
        return;
    st->text_group = NULL;
    st->group = (pdf_transparency_group *)group->parent;
    st->img = (cupidimage_image *)group->backdrop;
    cupidimage_pdf_group_free(st, group);
    free(group);
}

int cupidimage_pdf_type3_stencil_finish(pdf_render_state *st, const pdf_transparency_group *group)
{
    size_t pixels = (size_t)group->image.width * group->image.height;
    if (pixels > st->clip_budget->tests_left)
        return 0;
    st->clip_budget->tests_left -= pixels;
    for (unsigned y = 0; y < group->image.height; y++)
        for (unsigned x = 0; x < group->image.width; x++) {
            double shape = group->shape[(size_t)y * group->image.width + x];
            if (shape > 0)
                cupidimage_pdf_paint_pixel(st, (int)x, (int)y, 0, shape);
        }
    return !st->image_failed;
}
