#include "pdf_render_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PATTERN_BYTES (64u * 1024u * 1024u)

static int pattern_array(const pdf_render_state *st, const pdf_object_view *view, const char *key,
                         double *numbers, int count, int required) {
    size_t pos;
    pdf_object_id object;
    if (!cupidimage_pdf_page_value(view, key, &pos))
        return !required;
    pdf_object_view array = {0};
    const pdf_object_view *source = view;
    if (cupidimage_pdf_page_reference(view, pos, &object)) {
        if (!cupidimage_pdf_state_object(st, object, &array))
            return 0;
        source = &array;
        pos = 0;
    }
    cupidimage_pdf_skip_ws_and_comments(source->data, source->len, &pos);
    int ok = pos < source->len && source->data[pos++] == '[';
    for (int i = 0; ok && i < count; i++) {
        cupidimage_pdf_skip_ws_and_comments(source->data, source->len, &pos);
        ok = cupidimage_pdf_parse_pdf_number(source->data, source->len, &pos, &numbers[i]) &&
             isfinite(numbers[i]) && fabs(numbers[i]) <= 1e30;
    }
    cupidimage_pdf_skip_ws_and_comments(source->data, source->len, &pos);
    ok = ok && pos < source->len && source->data[pos] == ']';
    cupidimage_pdf_object_view_release(&array);
    return ok;
}

static void pattern_free(pdf_pattern *pattern) {
    if (!pattern)
        return;
    free(pattern->tile.rgba);
    free(pattern->shape);
    free(pattern->program);
    free(pattern->initial);
    cupidimage_pdf_object_view_release(&pattern->definition);
    if (pattern->surface) {
        free(pattern->surface->image.rgba);
        free(pattern->surface->alpha);
        free(pattern->surface);
    }
    free(pattern);
}

static int pattern_retain(pdf_render_state *st, pdf_pattern *pattern, const pdf_object_view *view,
                          unsigned char *program, size_t length) {
    size_t available = PATTERN_BYTES - st->patterns->bytes_used;
    if (!view->data && view->len)
        return 0;
    uintptr_t start = (uintptr_t)st->document, data = (uintptr_t)view->data;
    int in_document = st->document && data >= start && data - start <= st->document_size;
    if (in_document && view->len > st->document_size - (data - start))
        return 0;
    /* A resource override can lend storage without transferring its ownership. */
    size_t owned = in_document ? 0 : view->len;
    if (length > available || owned > available - length ||
        sizeof(pdf_graphics_state) > available - length - owned)
        return 0;
    pdf_graphics_state *initial = malloc(sizeof(*initial));
    unsigned char *copy = owned ? malloc(owned) : NULL;
    if (!initial || (owned && !copy)) {
        free(initial);
        free(copy);
        return 0;
    }
    *initial = st->initial_gs;
    initial->clip = NULL;
    initial->soft_mask = NULL;
    pattern->initial = initial;
    pattern->definition = *view;
    pattern->definition.owned = copy;
    if (copy) {
        memcpy(copy, view->data, owned);
        pattern->definition.data = copy;
    }
    pattern->program = program;
    pattern->program_length = length;
    pattern->definition_bytes = length + owned + sizeof(*initial);
    st->patterns->bytes_used += pattern->definition_bytes;
    return 1;
}

void cupidimage_pdf_patterns_free(pdf_pattern_store *store) {
    while (store->head) {
        pdf_pattern *next = store->head->next;
        pattern_free(store->head);
        store->head = next;
    }
    store->bytes_used = 0;
    store->count = 0;
}

void cupidimage_pdf_pattern_surface_release(pdf_render_state *st, pdf_pattern *pattern) {
    if (!pattern || !pattern->surface)
        return;
    st->patterns->bytes_used -= pattern->surface_bytes;
    free(pattern->surface->image.rgba);
    free(pattern->surface->alpha);
    free(pattern->surface);
    pattern->surface = NULL;
    pattern->surface_bytes = 0;
}

void cupidimage_pdf_patterns_end_paint(pdf_render_state *st) {
    pdf_pattern *patterns[2] = {st->gs.fill_pattern, st->gs.stroke_pattern};
    for (unsigned i = 0; i < 2; i++)
        if (patterns[i] && patterns[i]->serial == st->paint_serial)
            cupidimage_pdf_pattern_surface_release(st, patterns[i]);
}

int cupidimage_pdf_pattern_sample(pdf_render_state *st, pdf_pattern *pattern, double x, double y,
                                  double rgb[3], double *shape, double *alpha) {
    size_t pixel;
    if (pattern->deferred) {
        if (st->pattern_depth && !st->pattern_deferred) {
            if (st->pattern_needs_backdrop)
                *st->pattern_needs_backdrop = 1;
            return 0;
        }
        if (pattern->serial != st->paint_serial || !pattern->surface) {
            if (!cupidimage_pdf_render_pattern_page(st, pattern))
                return 0;
            pattern->serial = st->paint_serial;
        }
        double sx = floor((x - st->page_x0_pts) * (96.0 / 72.0));
        double sy = floor((st->page_top_pts - y) * (96.0 / 72.0));
        if (!isfinite(sx) || !isfinite(sy) || sx < 0 || sy < 0 || sx >= st->img->width ||
            sy >= st->img->height)
            return 0;
        pixel = (size_t)sy * st->img->width + (size_t)sx;
        memcpy(rgb, pattern->surface->color + pixel * 3, 3 * sizeof(double));
        *shape = pattern->surface->shape[pixel];
        *alpha = pattern->surface->alpha[pixel];
        return 1;
    }
    int gray = st->group ? st->group->gray : 0;
    if (pattern->color && pattern->gray != gray &&
        !cupidimage_pdf_refresh_pattern_cache(st, pattern, gray))
        return 0;
    if (pattern->shading) {
        double sx = floor((x - pattern->page_x0) * (96.0 / 72.0));
        double sy = floor((pattern->page_top - y) * (96.0 / 72.0));
        if (!isfinite(sx) || !isfinite(sy))
            return 0;
        if (sx < 0 || sy < 0 || sx >= pattern->tile.width || sy >= pattern->tile.height) {
            memset(rgb, 0, 3 * sizeof(double));
            *shape = *alpha = 0;
            return 1;
        }
        pixel = (size_t)sy * pattern->tile.width + (size_t)sx;
    } else {
        const double *m = pattern->inverse;
        double u = m[0] * x + m[2] * y + m[4], v = m[1] * x + m[3] * y + m[5];
        if (!isfinite(u) || !isfinite(v))
            return 0;
        u = fmod(u, pattern->xstep);
        v = fmod(v, pattern->ystep);
        if (u < 0)
            u += pattern->xstep;
        if (v < 0)
            v += pattern->ystep;
        unsigned sx = (unsigned)fmin(pattern->tile.width - 1,
                                     floor(u / pattern->xstep * pattern->tile.width));
        unsigned sy = (unsigned)fmin(pattern->tile.height - 1,
                                     floor((1 - v / pattern->ystep) * pattern->tile.height));
        pixel = (size_t)sy * pattern->tile.width + sx;
    }
    for (unsigned i = 0; i < 3; i++)
        rgb[i] = pattern->color ? pattern->color[pixel * 3 + i]
                                : pattern->tile.rgba[pixel * 4 + i] / 255.0;
    *shape = pattern->shape[pixel];
    *alpha = pattern->alpha[pixel];
    return 1;
}

static int pattern_select_shading(pdf_render_state *st, const pdf_object_view *view, int stroke,
                                  pdf_object_id object) {
    const pdf_image_colors *space = stroke ? &st->gs.stroke_space : &st->gs.fill_space;
    if (space->components != 0)
        return 0;
    /* Shading colours depend on the immutable resource and initial transform,
       not the caller's current paint colour or alpha. Bound the lookup work. */
    if (object > 0) {
        unsigned searched = 0;
        for (pdf_pattern *cached = st->patterns->head; cached && searched++ < 256;
             cached = cached->next) {
            if (cached->shading && cached->initial &&
                cached->initial->alpha_is_shape == st->initial_gs.alpha_is_shape &&
                cached->initial->rendering_intent == st->initial_gs.rendering_intent &&
                cached->resource_object == object && cached->tile.width == st->img->width &&
                cached->tile.height == st->img->height && cached->page_x0 == st->page_x0_pts &&
                cached->page_top == st->page_top_pts &&
                !memcmp(cached->initial_ctm, st->initial_gs.ctm, sizeof(cached->initial_ctm))) {
                if (stroke)
                    st->gs.stroke_pattern = cached;
                else
                    st->gs.fill_pattern = cached;
                return 1;
            }
        }
    }
    double matrix[6] = {1, 0, 0, 1, 0, 0};
    if (!pattern_array(st, view, "Matrix", matrix, 6, 0))
        return 0;
    size_t pixels = (size_t)st->img->width * st->img->height;
    size_t available = PATTERN_BYTES - st->patterns->bytes_used;
    if (available < sizeof(pdf_pattern) ||
        pixels > (available - sizeof(pdf_pattern)) / (4 + 5 * sizeof(double)))
        return 0;
    size_t bytes = pixels * (4 + 5 * sizeof(double)) + sizeof(pdf_pattern);
    pdf_pattern *pattern = calloc(1, sizeof(*pattern));
    if (!pattern)
        return 0;
    pattern->tile.width = st->img->width;
    pattern->tile.height = st->img->height;
    pattern->tile.rgba = calloc(pixels, 4);
    pattern->shape = calloc(pixels, 5 * sizeof(double));
    pattern->alpha = pattern->shape ? pattern->shape + pixels : NULL;
    pattern->color = pattern->alpha ? pattern->alpha + pixels : NULL;
    pattern->gray = st->group ? st->group->gray : 0;
    if (!pattern->tile.rgba || !pattern->shape) {
        pattern_free(pattern);
        return 0;
    }
    st->patterns->bytes_used += bytes;
    memcpy(pattern->matrix, matrix, sizeof(pattern->matrix));
    int ok = pattern_retain(st, pattern, view, NULL, 0);
    if (ok) {
        bytes += pattern->definition_bytes;
        int needs_backdrop = 0;
        int *previous = st->pattern_needs_backdrop;
        st->pattern_needs_backdrop = &needs_backdrop;
        ok = cupidimage_pdf_render_shading_pattern(st, view, matrix, pattern, NULL);
        st->pattern_needs_backdrop = previous;
        if (!ok && needs_backdrop) {
            size_t cell_bytes = pixels * (4 + 5 * sizeof(double));
            free(pattern->tile.rgba);
            free(pattern->shape);
            pattern->tile.rgba = NULL;
            pattern->shape = pattern->alpha = pattern->color = NULL;
            st->patterns->bytes_used -= cell_bytes;
            bytes -= cell_bytes;
            pattern->deferred = 1;
            ok = 1;
        }
    }
    if (!ok) {
        st->patterns->bytes_used -= bytes;
        pattern_free(pattern);
        return 0;
    }
    pattern->shading = 1;
    pattern->resource_object = object;
    memcpy(pattern->initial_ctm, st->initial_gs.ctm, sizeof(pattern->initial_ctm));
    pattern->page_x0 = st->page_x0_pts;
    pattern->page_top = st->page_top_pts;
    pattern->next = st->patterns->head;
    st->patterns->head = pattern;
    st->patterns->count++;
    if (stroke)
        st->gs.stroke_pattern = pattern;
    else
        st->gs.fill_pattern = pattern;
    return 1;
}

int cupidimage_pdf_select_pattern(pdf_render_state *st, const char *name, int stroke) {
    if (!st->patterns || st->pattern_depth >= 8 || st->patterns->count >= 65536)
        return 0;
    pdf_object_view view = {0}, resources = {0};
    unsigned char *decoded = NULL;
    pdf_pattern *pattern = NULL;
    size_t reserved = 0;
    int type, paint, tiling, ok = 0, needs_backdrop = 0;
    int *previous_backdrop = st->pattern_needs_backdrop;
    double bbox[4], matrix[6] = {1, 0, 0, 1, 0, 0}, step_x, step_y;
    size_t pos, length;
    const unsigned char *stream;
    pdf_object_id resource_object = 0;
    if (!cupidimage_pdf_find_resource(st, "Pattern", name, &view, &resource_object) ||
        !cupidimage_pdf_image_integer(st, &view, "PatternType", &type))
        goto done;
    if (type == 2) {
        ok = pattern_select_shading(st, &view, stroke, resource_object);
        goto done;
    }
    if (type != 1 || !cupidimage_pdf_image_integer(st, &view, "PaintType", &paint) ||
        (paint != 1 && paint != 2) ||
        !cupidimage_pdf_image_integer(st, &view, "TilingType", &tiling) || tiling < 1 ||
        tiling > 3 || !pattern_array(st, &view, "BBox", bbox, 4, 1) || bbox[0] > bbox[2] ||
        bbox[1] > bbox[3] || !pattern_array(st, &view, "Matrix", matrix, 6, 0) ||
        !cupidimage_pdf_page_value(&view, "XStep", &pos) ||
        !cupidimage_pdf_read_number(st, &view, pos, 0, &step_x) ||
        !cupidimage_pdf_page_value(&view, "YStep", &pos) ||
        !cupidimage_pdf_read_number(st, &view, pos, 0, &step_y) || !isfinite(step_x) ||
        !isfinite(step_y) || step_x == 0 || step_y == 0 ||
        !cupidimage_pdf_image_dictionary(st, &view, "Resources", &resources))
        goto done;
    const pdf_image_colors *space = stroke ? &st->gs.stroke_space : &st->gs.fill_space;
    if ((paint == 1 && space->components != 0) || (paint == 2 && space->components == 0))
        goto done;
    step_x = fabs(step_x);
    step_y = fabs(step_y);
    const float *p = st->initial_gs.ctm;
    double m[6] = {
        p[0] * matrix[0] + p[2] * matrix[1],        p[1] * matrix[0] + p[3] * matrix[1],
        p[0] * matrix[2] + p[2] * matrix[3],        p[1] * matrix[2] + p[3] * matrix[3],
        p[0] * matrix[4] + p[2] * matrix[5] + p[4], p[1] * matrix[4] + p[3] * matrix[5] + p[5]};
    const double ratio = 96.0 / 72.0;
    double original_det = m[0] * m[3] - m[1] * m[2];
    if (!isfinite(original_det) || original_det == 0)
        goto done;
    if (tiling != 2) {
        double ax = round(m[0] * step_x * ratio), ay = round(m[1] * step_x * ratio);
        double bx = round(m[2] * step_y * ratio), by = round(m[3] * step_y * ratio);
        if (ax == 0 && ay == 0) {
            if (fabs(m[0]) >= fabs(m[1]))
                ax = m[0] < 0 ? -1 : 1;
            else
                ay = m[1] < 0 ? -1 : 1;
        }
        if (bx == 0 && by == 0) {
            if (fabs(m[2]) >= fabs(m[3]))
                bx = m[2] < 0 ? -1 : 1;
            else
                by = m[3] < 0 ? -1 : 1;
        }
        m[0] = ax / (step_x * ratio);
        m[1] = ay / (step_x * ratio);
        m[2] = bx / (step_y * ratio);
        m[3] = by / (step_y * ratio);
    }
    double det = m[0] * m[3] - m[1] * m[2];
    if (!isfinite(det) || det == 0)
        goto done;
    double width = ceil(step_x * hypot(m[0], m[1]) * ratio),
           height = ceil(step_y * hypot(m[2], m[3]) * ratio);
    width = fmax(1, width);
    height = fmax(1, height);
    if (!isfinite(width) || !isfinite(height) || width > 4096 || height > 4096 ||
        width * height > 4u * 1024u * 1024u)
        goto done;
    size_t pixels = (size_t)width * (size_t)height;
    size_t bytes = pixels * (4 + (paint == 1 ? 5 : 2) * sizeof(double));
    if (bytes + sizeof(pdf_pattern) > PATTERN_BYTES - st->patterns->bytes_used)
        goto done;
    pattern = calloc(1, sizeof(*pattern));
    if (!pattern)
        goto done;
    pattern->tile.width = (uint32_t)width;
    pattern->tile.height = (uint32_t)height;
    pattern->tile.rgba = calloc(pixels, 4);
    pattern->shape = calloc(pixels, (paint == 1 ? 5 : 2) * sizeof(double));
    pattern->alpha = pattern->shape ? pattern->shape + pixels : NULL;
    pattern->color = pattern->alpha && paint == 1 ? pattern->alpha + pixels : NULL;
    pattern->gray = st->group ? st->group->gray : 0;
    if (!pattern->tile.rgba || !pattern->shape)
        goto done;
    reserved = bytes + sizeof(pdf_pattern);
    st->patterns->bytes_used += reserved;
    pattern->uncolored = paint == 2;
    pattern->base_space = *space;
    pattern->base_space.pattern = pattern->base_space.indexed = 0;
    memcpy(pattern->base, stroke ? st->gs.stroke_color : st->gs.fill_color, sizeof(pattern->base));
    memcpy(pattern->matrix, m, sizeof(pattern->matrix));
    memcpy(pattern->bounds, bbox, sizeof(pattern->bounds));
    pattern->xstep = step_x;
    pattern->ystep = step_y;
    double *inv = pattern->inverse;
    inv[0] = m[3] / det;
    inv[1] = -m[1] / det;
    inv[2] = -m[2] / det;
    inv[3] = m[0] / det;
    inv[4] = -(inv[0] * m[4] + inv[2] * m[5]);
    inv[5] = -(inv[1] * m[4] + inv[3] * m[5]);
    for (int i = 0; i < 6; i++)
        if (!isfinite(inv[i]))
            goto done;
    if (!cupidimage_pdf_object_stream(st->document, st->document_size, &view, &stream, &length) ||
        !cupidimage_pdf_decode_stream(&view, stream, length, &decoded, &length))
        goto done;
    if (bbox[0] == bbox[2] || bbox[1] == bbox[3])
        goto ready;
    double first_x = floor(-bbox[2] / step_x) + 1, last_x = ceil((step_x - bbox[0]) / step_x) - 1;
    double first_y = floor(-bbox[3] / step_y) + 1, last_y = ceil((step_y - bbox[1]) / step_y) - 1;
    if (!isfinite(first_x) || !isfinite(last_x) || !isfinite(first_y) || !isfinite(last_y) ||
        fabs(first_x) > 1e9 || fabs(last_x) > 1e9 || fabs(first_y) > 1e9 || fabs(last_y) > 1e9)
        goto done;
    double copies_x = fmax(0, last_x - first_x + 1), copies_y = fmax(0, last_y - first_y + 1);
    if (copies_x * copies_y > 4096)
        goto done;
    double sx = width / (step_x * ratio), sy = height / (step_y * ratio);
    if (!isfinite(sx) || !isfinite(sy) || sx <= 0 || sy <= 0 || sx > 1e30 || sy > 1e30 ||
        (float)sx == 0 || (float)sy == 0)
        goto done;
    st->pattern_needs_backdrop = &needs_backdrop;
    for (int y = (int)first_y; copies_x > 0 && y <= (int)last_y; y++)
        for (int x = (int)first_x; x <= (int)last_x; x++) {
            double ox = x * step_x, oy = y * step_y;
            if (fabs(ox * sx) > 1e30 || fabs(oy * sy) > 1e30 ||
                !cupidimage_pdf_render_pattern_cell(st, &view, decoded, length, pattern, sx, sy, ox,
                                                    oy, bbox, paint == 2)) {
                if (needs_backdrop)
                    goto ready;
                goto done;
            }
        }
ready:
    if (needs_backdrop) {
        free(pattern->tile.rgba);
        free(pattern->shape);
        pattern->tile.rgba = NULL;
        pattern->shape = pattern->alpha = pattern->color = NULL;
        st->patterns->bytes_used -= bytes;
        reserved -= bytes;
        pattern->deferred = 1;
    }
    if (needs_backdrop || paint == 1) {
        if (!pattern_retain(st, pattern, &view, decoded, length))
            goto done;
        decoded = NULL;
        reserved += pattern->definition_bytes;
    }
    if (st->patterns->count >= 65536)
        goto done;
    st->patterns->count++;
    pattern->next = st->patterns->head;
    st->patterns->head = pattern;
    if (stroke)
        st->gs.stroke_pattern = pattern;
    else
        st->gs.fill_pattern = pattern;
    pattern = NULL;
    reserved = 0;
    ok = 1;
done:
    st->pattern_needs_backdrop = previous_backdrop;
    free(decoded);
    cupidimage_pdf_object_view_release(&resources);
    cupidimage_pdf_object_view_release(&view);
    pattern_free(pattern);
    st->patterns->bytes_used -= reserved;
    return ok;
}
