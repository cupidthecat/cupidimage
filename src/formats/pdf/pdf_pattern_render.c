#include "pdf_render_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int cupidimage_pdf_render_pattern_cell(pdf_render_state *parent, const pdf_object_view *pattern,
                                       const unsigned char *stream, size_t length,
                                       pdf_pattern *cache, double sx, double sy, double ox,
                                       double oy, const double bbox[4], int uncolored) {
    cupidimage_image *image = &cache->tile;
    double *shape = cache->shape;
    pdf_transparency_group group = {0};
    group.image = *image;
    group.alpha = cache->alpha;
    group.shape = cache->shape;
    group.color = cache->color;
    group.gray = cache->gray;
    group.isolated = 1;
    if (!parent->form_budget->calls_left || length > parent->form_budget->bytes_left)
        return 0;
    parent->form_budget->calls_left--;
    parent->form_budget->bytes_left -= length;
    pdf_render_state *child = malloc(sizeof(*child));
    if (!child)
        return 0;
    pdf_font_table fonts;
    cupidimage_pdf_font_table_init(&fonts);
    pdf_clip_budget clip_budget = {0, 256u * 1024u * 1024u, 4096};
    *child = *parent;
    child->text_group = NULL;
    child->in_text = 0;
    child->gs = parent->initial_gs;
    child->gs.clip = NULL;
    child->gs.soft_mask = NULL;
    child->gs_stack_len = 0;
    child->text_clip = NULL;
    child->text_winding = NULL;
    child->text_winding_bytes = 0;
    child->clip_budget = &clip_budget;
    child->pending_clip = 0;
    child->image_failed = 0;
    child->img = image;
    child->shape_plane = shape;
    child->alpha_plane = shape + (size_t)image->width * image->height;
    child->group = cache->color ? &group : NULL;
    child->mask_rendering = 0;
    child->page_x0_pts = 0;
    child->page_top_pts = image->height * (72.0f / 96.0f);
    child->in_text = 0;
    child->glyph_active = 0;
    child->glyph_uncolored = 0;
    child->pattern_uncolored = uncolored;
    child->pattern_deferred = 0;
    child->gs.fill_alpha = child->gs.stroke_alpha = 1;
    child->gs.blend_mode = 0;
    child->pattern_depth = parent->pattern_depth + 1;
    child->resource_override = pattern;
    child->gs.ctm[0] = (float)sx;
    child->gs.ctm[1] = child->gs.ctm[2] = 0;
    child->gs.ctm[3] = (float)sy;
    child->gs.ctm[4] = (float)(ox * sx);
    child->gs.ctm[5] = (float)(oy * sy);
    if (uncolored) {
        memset(child->gs.fill_color, 0, sizeof(child->gs.fill_color));
        memset(child->gs.stroke_color, 0, sizeof(child->gs.stroke_color));
        memset(&child->gs.fill_space, 0, sizeof(child->gs.fill_space));
        child->gs.fill_space.components = 3;
        child->gs.stroke_space = child->gs.fill_space;
        child->gs.fill_pattern = child->gs.stroke_pattern = NULL;
    }
    child->form_depth = 1;
    child->form_objects[0] = 0;
    memcpy(child->form_clips[0].box, bbox, sizeof(child->form_clips[0].box));
    double *inv = child->form_clips[0].inverse;
    inv[0] = 1 / sx;
    inv[1] = inv[2] = 0;
    inv[3] = 1 / sy;
    inv[4] = -ox;
    inv[5] = -oy;
    cupidimage_pdf_path_clear(&child->path);
    int ok = cupidimage_pdf_collect_resource_fonts(parent->document, parent->document_size, pattern,
                                                   &fonts, parent->error, parent->error_capacity);
    child->font_table = &fonts;
    child->initial_gs = child->gs;
    child->initial_gs.soft_mask = NULL;
    if (ok)
        ok = cupidimage_pdf_render_stream(child, stream, length);
    cupidimage_pdf_render_state_release(child);
    free(child);
    cupidimage_pdf_font_table_free(&fonts);
    return ok;
}

static int pdf_pattern_capture_surface(pdf_render_state *parent, pdf_pattern *pattern,
                                       pdf_transparency_group *group) {
    const size_t limit = 64u * 1024u * 1024u;
    size_t pixels = (size_t)group->image.width * group->image.height;
    size_t bytes = group->bytes + sizeof(*group);
    if (pixels > parent->clip_budget->tests_left || parent->patterns->bytes_used > limit ||
        bytes > limit - parent->patterns->bytes_used)
        return 0;
    parent->clip_budget->tests_left -= pixels;
    for (size_t pixel = 0; pixel < pixels; pixel++) {
        double color[3], alpha, shape;
        cupidimage_pdf_group_source(group, pixel, color, &alpha, &shape);
        memcpy(group->color + pixel * 3, color, sizeof(color));
    }
    parent->patterns->bytes_used += bytes;
    parent->form_budget->transparency_bytes_live -= group->bytes;
    pattern->surface_bytes = bytes;
    pattern->surface = group;
    group->parent = NULL;
    group->backdrop = NULL;
    group->isolated = 1;
    return 1;
}

static int pdf_pattern_shading_surface(pdf_render_state *parent, pdf_pattern *pattern) {
    pdf_transparency_group *group = calloc(1, sizeof(*group));
    pdf_render_state *context = malloc(sizeof(*context));
    int ok = group && context;
    if (ok)
        ok = cupidimage_pdf_group_surface(parent, group, 0, NULL);
    if (ok) {
        group->knockout = 1;
        *context = *parent;
        context->initial_gs = *pattern->initial;
        ok = cupidimage_pdf_render_shading_pattern(context, &pattern->definition, pattern->matrix,
                                                   pattern, group);
    }
    if (ok)
        ok = pdf_pattern_capture_surface(parent, pattern, group);
    if (ok)
        group = NULL;
    if (group) {
        cupidimage_pdf_group_free(parent, group);
        free(group);
    }
    free(context);
    return ok;
}

int cupidimage_pdf_render_pattern_page(pdf_render_state *parent, pdf_pattern *pattern) {
    const size_t cache_limit = 64u * 1024u * 1024u;
    cupidimage_pdf_pattern_surface_release(parent, pattern);
    if (parent->pattern_depth >= 8)
        return 0;
    size_t pixels = (size_t)parent->img->width * parent->img->height;
    if (parent->patterns->bytes_used > cache_limit ||
        cache_limit - parent->patterns->bytes_used < sizeof(pdf_transparency_group) ||
        pixels > (cache_limit - parent->patterns->bytes_used - sizeof(pdf_transparency_group)) /
                     (4 + 5 * sizeof(double)))
        return 0;
    if (pattern->shading)
        return pdf_pattern_shading_surface(parent, pattern);
    const double *inverse = pattern->inverse;
    double low[2] = {INFINITY, INFINITY}, high[2] = {-INFINITY, -INFINITY};
    for (unsigned corner = 0; corner < 4; corner++) {
        double x = parent->page_x0_pts + (corner & 1 ? parent->img->width * .75 : 0);
        double y = parent->page_top_pts - (corner & 2 ? parent->img->height * .75 : 0);
        double point[2] = {inverse[0] * x + inverse[2] * y + inverse[4],
                           inverse[1] * x + inverse[3] * y + inverse[5]};
        for (unsigned axis = 0; axis < 2; axis++) {
            low[axis] = fmin(low[axis], point[axis]);
            high[axis] = fmax(high[axis], point[axis]);
        }
    }
    double first[2], last[2], step[2] = {pattern->xstep, pattern->ystep};
    double copies = 1;
    for (unsigned axis = 0; axis < 2; axis++) {
        first[axis] = floor((low[axis] - pattern->bounds[axis + 2]) / step[axis]) + 1;
        last[axis] = ceil((high[axis] - pattern->bounds[axis]) / step[axis]) - 1;
        if (!isfinite(first[axis]) || !isfinite(last[axis]) || fabs(first[axis]) > 1e9 ||
            fabs(last[axis]) > 1e9)
            return 0;
        copies *= fmax(0, last[axis] - first[axis] + 1);
    }
    if (copies > parent->form_budget->calls_left ||
        copies * pattern->program_length > parent->form_budget->bytes_left)
        return 0;
    pdf_transparency_group *group = calloc(1, sizeof(*group));
    pdf_render_state *child = malloc(sizeof(*child));
    pdf_font_table fonts;
    cupidimage_pdf_font_table_init(&fonts);
    int ok = group && child;
    if (ok)
        ok = cupidimage_pdf_group_surface(parent, group, 0, NULL);
    if (ok)
        ok = cupidimage_pdf_collect_resource_fonts(parent->document, parent->document_size,
                                                   &pattern->definition, &fonts, parent->error,
                                                   parent->error_capacity);
    for (int row = (int)first[1]; ok && copies && row <= (int)last[1]; row++)
        for (int column = (int)first[0]; ok && column <= (int)last[0]; column++) {
            parent->form_budget->calls_left--;
            parent->form_budget->bytes_left -= pattern->program_length;
            *child = *parent;
            child->text_group = NULL;
            child->in_text = 0;
            child->gs = *pattern->initial;
            child->gs.clip = NULL;
            child->gs.soft_mask = NULL;
            child->gs.fill_alpha = child->gs.stroke_alpha = 1;
            child->gs.blend_mode = 0;
            child->gs_stack_len = 0;
            child->text_clip = NULL;
            child->text_winding = NULL;
            child->text_winding_bytes = 0;
            child->pending_clip = 0;
            child->image_failed = 0;
            child->img = &group->image;
            child->group = group;
            child->shape_plane = child->alpha_plane = NULL;
            child->mask_rendering = 0;
            child->glyph_active = child->glyph_uncolored = 0;
            child->pattern_uncolored = pattern->uncolored;
            child->pattern_deferred = 1;
            child->pattern_needs_backdrop = NULL;
            child->pattern_depth = parent->pattern_depth + 1;
            child->resource_override = &pattern->definition;
            child->font_table = &fonts;
            double m[6];
            memcpy(m, pattern->matrix, sizeof(m));
            double tx = column * pattern->xstep, ty = row * pattern->ystep;
            m[4] += m[0] * tx + m[2] * ty;
            m[5] += m[1] * tx + m[3] * ty;
            for (unsigned i = 0; i < 6; i++) {
                if (!isfinite(m[i]) || fabs(m[i]) > 1e30)
                    ok = 0;
                else
                    child->gs.ctm[i] = (float)m[i];
            }
            if (pattern->uncolored) {
                memcpy(child->gs.fill_color, pattern->base, sizeof(pattern->base));
                memcpy(child->gs.stroke_color, pattern->base, sizeof(pattern->base));
                child->gs.fill_space = pattern->base_space;
                child->gs.stroke_space = child->gs.fill_space;
                child->gs.fill_pattern = child->gs.stroke_pattern = NULL;
            }
            child->form_depth = 1;
            child->form_objects[0] = 0;
            memcpy(child->form_clips[0].box, pattern->bounds, sizeof(pattern->bounds));
            double *clip = child->form_clips[0].inverse;
            memcpy(clip, pattern->inverse, 4 * sizeof(double));
            clip[4] = pattern->inverse[4] - tx;
            clip[5] = pattern->inverse[5] - ty;
            cupidimage_pdf_path_clear(&child->path);
            child->initial_gs = child->gs;
            if (ok)
                ok = cupidimage_pdf_render_stream(child, pattern->program, pattern->program_length);
            cupidimage_pdf_render_state_release(child);
        }
    if (ok)
        ok = pdf_pattern_capture_surface(parent, pattern, group);
    if (ok)
        group = NULL;
    if (group) {
        cupidimage_pdf_group_free(parent, group);
        free(group);
    }
    free(child);
    cupidimage_pdf_font_table_free(&fonts);
    return ok;
}

int cupidimage_pdf_render_shading_pattern(pdf_render_state *parent, const pdf_object_view *pattern,
                                          const double matrix[6], pdf_pattern *cache,
                                          pdf_transparency_group *live) {
    cupidimage_image *image = live ? &live->image : &cache->tile;
    double *shape = live ? live->shape : cache->shape;
    pdf_transparency_group group = {0};
    group.image = *image;
    group.alpha = cache->alpha;
    group.shape = cache->shape;
    group.color = cache->color;
    group.gray = cache->gray;
    group.isolated = group.knockout = 1;
    if (!parent->form_budget->calls_left)
        return 0;
    parent->form_budget->calls_left--;
    pdf_object_view shading = {0}, state = {0};
    size_t pos;
    pdf_render_state *child = malloc(sizeof(*child));
    if (!child)
        return 0;
    *child = *parent;
    child->text_group = NULL;
    child->in_text = 0;
    child->gs = parent->initial_gs;
    child->gs.clip = NULL;
    child->gs.soft_mask = NULL;
    child->gs.fill_alpha = child->gs.stroke_alpha = 1;
    child->gs.blend_mode = 0;
    child->img = image;
    child->shape_plane = shape;
    child->alpha_plane = live ? live->alpha : cache->alpha;
    child->group = live ? live : &group;
    child->pattern_deferred = live != NULL;
    child->mask_rendering = 0;
    child->form_depth = 0;
    child->gs_stack_len = 0;
    child->text_clip = NULL;
    child->text_winding = NULL;
    child->text_winding_bytes = 0;
    child->glyph_active = child->glyph_uncolored = child->pattern_uncolored = 0;
    child->pattern_depth = parent->pattern_depth + 1;
    child->image_failed = 0;
    double m[6];
    for (int i = 0; i < 6; i++)
        m[i] = child->gs.ctm[i];
    cupidimage_pdf_matrix_concat_double(m, matrix);
    int ok = 1;
    for (int i = 0; i < 6; i++) {
        if (!isfinite(m[i]) || fabs(m[i]) > 1e30)
            ok = 0;
        else
            child->gs.ctm[i] = (float)m[i];
    }
    double det = m[0] * m[3] - m[1] * m[2];
    if (!isfinite(det) || det == 0)
        ok = 0;
    if (ok && cupidimage_pdf_page_value(pattern, "ExtGState", &pos))
        ok = cupidimage_pdf_image_dictionary(parent, pattern, "ExtGState", &state) &&
             cupidimage_pdf_apply_shading_graphics_view(child, &state);
    if (ok)
        ok = cupidimage_pdf_image_dictionary(parent, pattern, "Shading", &shading) &&
             cupidimage_pdf_paint_shading_view(child, &shading, 1);
    cupidimage_pdf_object_view_release(&state);
    cupidimage_pdf_object_view_release(&shading);
    cupidimage_pdf_render_state_release(child);
    free(child);
    return ok;
}

int cupidimage_pdf_refresh_pattern_cache(pdf_render_state *st, pdf_pattern *pattern, int gray) {
    if (!pattern->initial || !pattern->color)
        return 0;
    pdf_render_state *context = malloc(sizeof(*context));
    if (!context)
        return 0;
    *context = *st;
    context->initial_gs = *pattern->initial;
    size_t pixels = (size_t)pattern->tile.width * pattern->tile.height;
    memset(pattern->tile.rgba, 0, pixels * 4);
    memset(pattern->shape, 0, pixels * 5 * sizeof(double));
    pattern->gray = gray;
    int ok = 1;
    if (pattern->shading) {
        ok = cupidimage_pdf_render_shading_pattern(context, &pattern->definition, pattern->matrix,
                                                   pattern, NULL);
    } else if (pattern->bounds[0] != pattern->bounds[2] &&
               pattern->bounds[1] != pattern->bounds[3]) {
        double first_x = floor(-pattern->bounds[2] / pattern->xstep) + 1;
        double last_x = ceil((pattern->xstep - pattern->bounds[0]) / pattern->xstep) - 1;
        double first_y = floor(-pattern->bounds[3] / pattern->ystep) + 1;
        double last_y = ceil((pattern->ystep - pattern->bounds[1]) / pattern->ystep) - 1;
        double sx = pattern->tile.width / (pattern->xstep * (96.0 / 72.0));
        double sy = pattern->tile.height / (pattern->ystep * (96.0 / 72.0));
        for (int y = (int)first_y; ok && y <= (int)last_y; y++)
            for (int x = (int)first_x; ok && x <= (int)last_x; x++)
                ok = cupidimage_pdf_render_pattern_cell(
                    context, &pattern->definition, pattern->program, pattern->program_length,
                    pattern, sx, sy, x * pattern->xstep, y * pattern->ystep, pattern->bounds, 0);
    }
    free(context);
    return ok;
}
