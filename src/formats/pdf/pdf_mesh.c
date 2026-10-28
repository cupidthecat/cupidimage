#include "pdf_mesh_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    double x, y, value[PDF_COLOR_COMPONENTS];
} mesh_vertex;

uint32_t cupidimage_pdf_mesh_bits(const unsigned char *record, unsigned *pos, unsigned count) {
    uint32_t value = 0;
    for (unsigned i = 0; i < count; i++, (*pos)++)
        value = (value << 1) | ((record[*pos / 8] >> (7 - *pos % 8)) & 1u);
    return value;
}

double cupidimage_pdf_mesh_decode(uint32_t value, unsigned bits, const double *range) {
    double fraction = value / (double)((UINT64_C(1) << bits) - 1);
    return range[0] + fraction * (range[1] - range[0]);
}

static unsigned mesh_vertex_read(const pdf_mesh_context *mesh, const unsigned char *record,
                                 mesh_vertex *v) {
    unsigned pos = 0, flag = mesh->flag_bits
                                 ? cupidimage_pdf_mesh_bits(record, &pos, mesh->flag_bits) & 3u
                                 : 0;
    double x =
        cupidimage_pdf_mesh_decode(cupidimage_pdf_mesh_bits(record, &pos, mesh->coordinate_bits),
                                   mesh->coordinate_bits, mesh->decode);
    double y =
        cupidimage_pdf_mesh_decode(cupidimage_pdf_mesh_bits(record, &pos, mesh->coordinate_bits),
                                   mesh->coordinate_bits, mesh->decode + 2);
    const float *m = mesh->state->gs.ctm;
    v->x = (m[0] * x + m[2] * y + m[4] - mesh->state->page_x0_pts) * (96.0 / 72.0);
    v->y = (mesh->state->page_top_pts - (m[1] * x + m[3] * y + m[5])) * (96.0 / 72.0);
    for (int c = 0; c < mesh->samples; c++)
        v->value[c] =
            cupidimage_pdf_mesh_decode(cupidimage_pdf_mesh_bits(record, &pos, mesh->component_bits),
                                       mesh->component_bits, mesh->decode + 4 + c * 2);
    if (mesh->colors.indexed) {
        unsigned index = (unsigned)floor(fmin(mesh->colors.high, fmax(0, v->value[0])) + .5);
        for (int c = 0; c < mesh->colors.components; c++)
            v->value[c] = cupidimage_pdf_color_palette_value(
                &mesh->colors, c, mesh->colors.palette[index * mesh->colors.components + c]);
    }
    return flag;
}

int cupidimage_pdf_mesh_inside_box(const pdf_mesh_context *mesh, unsigned x, unsigned y) {
    if (!mesh->has_box)
        return 1;
    double px = x + .5, py = y + .5;
    const double *m = mesh->inverse;
    double u = m[0] * px + m[2] * py + m[4], v = m[1] * px + m[3] * py + m[5];
    return u >= mesh->box[0] && u < mesh->box[2] && v >= mesh->box[1] && v < mesh->box[3];
}

void cupidimage_pdf_mesh_color(const pdf_image_colors *colors, const double *values,
                               pdf_mesh_sample *sample, int lookup) {
    if (colors->indexed && lookup) {
        unsigned index = (unsigned)floor(fmin(colors->high, fmax(0, values[0])) + .5);
        for (int c = 0; c < colors->components; c++)
            sample->values[c] = cupidimage_pdf_color_palette_value(
                colors, c, colors->palette[index * colors->components + c]);
    } else {
        for (int c = 0; c < colors->components; c++)
            sample->values[c] = values[c];
    }
    sample->painted = 1;
}

static double mesh_edge(const mesh_vertex *a, const mesh_vertex *b, double x, double y) {
    return (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
}

static int mesh_triangle(pdf_mesh_context *mesh, const mesh_vertex *a, const mesh_vertex *b,
                         const mesh_vertex *c) {
    double area = mesh_edge(a, b, c->x, c->y);
    if (!isfinite(area))
        return 0;
    if (!area)
        return 1;
    unsigned width = mesh->state->img->width, height = mesh->state->img->height;
    unsigned x0 = (unsigned)fmax(0, fmin(width, floor(fmin(a->x, fmin(b->x, c->x)))));
    unsigned x1 = (unsigned)fmax(0, fmin(width, ceil(fmax(a->x, fmax(b->x, c->x)))));
    unsigned y0 = (unsigned)fmax(0, fmin(height, floor(fmin(a->y, fmin(b->y, c->y)))));
    unsigned y1 = (unsigned)fmax(0, fmin(height, ceil(fmax(a->y, fmax(b->y, c->y)))));
    size_t tests = (size_t)(x1 - x0) * (y1 - y0);
    if (tests > mesh->state->form_budget->shade_evaluations_left)
        return 0;
    mesh->state->form_budget->shade_evaluations_left -= tests;
    for (unsigned y = y0; y < y1; y++)
        for (unsigned x = x0; x < x1; x++) {
            double w[3] = {mesh_edge(b, c, x + .5, y + .5) / area,
                           mesh_edge(c, a, x + .5, y + .5) / area,
                           mesh_edge(a, b, x + .5, y + .5) / area};
            if (!isfinite(w[0]) || !isfinite(w[1]) || !isfinite(w[2]))
                return 0;
            if (w[0] < 0 || w[1] < 0 || w[2] < 0 || !cupidimage_pdf_mesh_inside_box(mesh, x, y))
                continue;
            double values[PDF_COLOR_COMPONENTS] = {0};
            for (int i = 0; i < mesh->interpolate; i++)
                values[i] = a->value[i] + w[1] * (b->value[i] - a->value[i]) +
                            w[2] * (c->value[i] - a->value[i]);
            if (mesh->function) {
                double low = fmin(mesh->decode[4], mesh->decode[5]),
                       high = fmax(mesh->decode[4], mesh->decode[5]);
                double input[2] = {fmin(high, fmax(low, values[0])), 0};
                if (!cupidimage_pdf_evaluate_function(
                        mesh->function, input, values,
                        &mesh->state->form_budget->shade_evaluations_left))
                    return 0;
            }
            pdf_mesh_sample *pixel = mesh->pixels + (size_t)y * width + x;
            cupidimage_pdf_mesh_color(&mesh->colors, values, pixel, 0);
        }
    return 1;
}

static int mesh_optional(const pdf_render_state *st, const pdf_object_view *view, const char *key,
                         int *present) {
    size_t pos;
    *present = 0;
    if (!cupidimage_pdf_page_value(view, key, &pos))
        return 1;
    pdf_object_view value = {0};
    if (!cupidimage_pdf_dictionary_value(st->document, st->document_size, view, key, &value))
        return 0;
    pos = 0;
    cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &pos);
    *present = !(value.len - pos >= 4 && !memcmp(value.data + pos, "null", 4) &&
                 (value.len - pos == 4 || cupidimage_pdf_is_ws(value.data[pos + 4]) ||
                  cupidimage_pdf_is_delim(value.data[pos + 4])));
    cupidimage_pdf_object_view_release(&value);
    return 1;
}

static int mesh_width(int value, int coordinate) {
    return value == 1 || value == 2 || value == 4 || value == 8 || value == 12 || value == 16 ||
           (coordinate && (value == 24 || value == 32));
}

int cupidimage_pdf_paint_mesh(pdf_render_state *st, const pdf_object_view *view, int type,
                              int background) {
    if (type < 4 || type > 7)
        return 0;
    pdf_mesh_context mesh = {0};
    mesh.state = st;
    unsigned char *decoded = NULL;
    mesh_vertex *rows = NULL;
    size_t reserved = 0, length = 0;
    int ok = 0, columns = 0, use_function = 0;
    const unsigned char *raw;
    if (!cupidimage_pdf_shading_colors(st, view, &mesh.colors) || mesh.colors.pattern ||
        !cupidimage_pdf_image_integer(st, view, "BitsPerCoordinate", &mesh.coordinate_bits) ||
        !mesh_width(mesh.coordinate_bits, 1) ||
        !cupidimage_pdf_image_integer(st, view, "BitsPerComponent", &mesh.component_bits) ||
        !mesh_width(mesh.component_bits, 0))
        goto done;
    if (type != 5) {
        if (!cupidimage_pdf_image_integer(st, view, "BitsPerFlag", &mesh.flag_bits) ||
            (mesh.flag_bits != 2 && mesh.flag_bits != 4 && mesh.flag_bits != 8))
            goto done;
    } else if (!cupidimage_pdf_image_integer(st, view, "VerticesPerRow", &columns) || columns < 2)
        goto done;
    if (!mesh_optional(st, view, "Function", &use_function))
        goto done;
    mesh.samples = use_function || mesh.colors.indexed ? 1 : mesh.colors.components;
    mesh.interpolate = use_function ? 1 : mesh.colors.components;
    if (use_function && mesh.colors.indexed)
        goto done;
    if (!cupidimage_pdf_function_array(st, view, "Decode", mesh.decode, 4 + 2 * mesh.samples))
        goto done;
    if (use_function) {
        mesh.function = calloc(1, sizeof(*mesh.function));
        if (!mesh.function ||
            !cupidimage_pdf_load_function(st, view, "Function", 1, mesh.colors.components,
                                          mesh.function) ||
            fmin(mesh.decode[4], mesh.decode[5]) < mesh.function->nodes[0].domain[0] ||
            fmax(mesh.decode[4], mesh.decode[5]) > mesh.function->nodes[0].domain[1])
            goto done;
    }
    if (!mesh_optional(st, view, "BBox", &mesh.has_box))
        goto done;
    if (mesh.has_box && (!cupidimage_pdf_function_array(st, view, "BBox", mesh.box, 4) ||
                         mesh.box[0] >= mesh.box[2] || mesh.box[1] >= mesh.box[3]))
        goto done;
    if (!cupidimage_pdf_object_stream(st->document, st->document_size, view, &raw, &length) ||
        !cupidimage_pdf_decode_stream(view, raw, length, &decoded, &length) ||
        length > st->form_budget->bytes_left)
        goto done;
    st->form_budget->bytes_left -= length;
    size_t record =
        (mesh.flag_bits + 2 * mesh.coordinate_bits + mesh.samples * mesh.component_bits + 7u) / 8;
    size_t vertices = type <= 5 ? length / record : 0;
    if ((type <= 5 && length % record) || (type == 5 && vertices % (unsigned)columns) ||
        vertices > st->form_budget->shade_evaluations_left)
        goto done;
    st->form_budget->shade_evaluations_left -= vertices;
    const float *m = st->gs.ctm;
    double det = (double)m[0] * m[3] - (double)m[1] * m[2];
    if (!isfinite(det))
        goto done;
    if (det) {
        mesh.inverse[0] = .75 * m[3] / det;
        mesh.inverse[1] = -.75 * m[1] / det;
        mesh.inverse[2] = .75 * m[2] / det;
        mesh.inverse[3] = -.75 * m[0] / det;
        double tx = st->page_x0_pts - m[4], ty = st->page_top_pts - m[5];
        mesh.inverse[4] = (m[3] * tx - m[2] * ty) / det;
        mesh.inverse[5] = (m[0] * ty - m[1] * tx) / det;
    }
    size_t pixels = (size_t)st->img->width * st->img->height;
    size_t available =
        st->form_budget->transparency_bytes_live <= PDF_TRANSPARENCY_MEMORY_LIMIT
            ? PDF_TRANSPARENCY_MEMORY_LIMIT - st->form_budget->transparency_bytes_live
            : 0;
    size_t per_pixel = sizeof(*mesh.pixels) + (size_t)mesh.colors.components * sizeof(double);
    if (pixels > available / per_pixel)
        goto done;
    reserved = pixels * per_pixel;
    if (type == 5 && vertices >= (size_t)columns) {
        if ((size_t)columns > (available - reserved) / (2 * sizeof(*rows))) {
            reserved = 0;
            goto done;
        }
        reserved += (size_t)columns * 2 * sizeof(*rows);
    }
    st->form_budget->transparency_bytes_live += reserved;
    mesh.pixels = calloc(pixels, per_pixel);
    if (!mesh.pixels)
        goto done;
    double *sample_values = (double *)(mesh.pixels + pixels);
    for (size_t pixel = 0; pixel < pixels; pixel++)
        mesh.pixels[pixel].values = sample_values + pixel * mesh.colors.components;
    if (type == 5 && vertices >= (size_t)columns) {
        rows = malloc((size_t)columns * 2 * sizeof(*rows));
        if (!rows)
            goto done;
    }
    int has_background = 0;
    if (background && !mesh_optional(st, view, "Background", &has_background))
        goto done;
    int paint_background = has_background && det != 0;
    double background_values[PDF_COLOR_COMPONENTS] = {0};
    pdf_mesh_sample background_sample = {.values = background_values};
    if (paint_background) {
        double values[PDF_COLOR_COMPONENTS];
        if (!cupidimage_pdf_function_array(st, view, "Background", values,
                                           mesh.colors.indexed ? 1 : mesh.colors.components) ||
            pixels > st->form_budget->shade_evaluations_left)
            goto done;
        st->form_budget->shade_evaluations_left -= pixels;
        cupidimage_pdf_mesh_color(&mesh.colors, values, &background_sample, 1);
    }
    if (type == 4) {
        mesh_vertex triangle[3];
        int have_triangle = 0;
        for (size_t i = 0; i < vertices;) {
            mesh_vertex next;
            unsigned flag = mesh_vertex_read(&mesh, decoded + i++ * record, &next);
            if (!flag) {
                if (vertices - i < 2)
                    goto done;
                triangle[0] = next;
                mesh_vertex_read(&mesh, decoded + i++ * record, triangle + 1);
                mesh_vertex_read(&mesh, decoded + i++ * record, triangle + 2);
            } else {
                if (!have_triangle || flag == 3)
                    goto done;
                if (flag == 1)
                    triangle[0] = triangle[1];
                triangle[1] = triangle[2];
                triangle[2] = next;
            }
            have_triangle = 1;
            if (det && !mesh_triangle(&mesh, triangle, triangle + 1, triangle + 2))
                goto done;
        }
    } else if (type >= 6) {
        if (!cupidimage_pdf_paint_patches(&mesh, decoded, length, type, det != 0))
            goto done;
    } else if (rows) {
        mesh_vertex *previous = rows, *current = rows + columns;
        for (size_t i = 0; i < vertices; i++) {
            unsigned column = (unsigned)(i % (unsigned)columns);
            mesh_vertex_read(&mesh, decoded + i * record, current + column);
            if (column + 1 == (unsigned)columns) {
                if (i >= (size_t)columns && det)
                    for (int j = 0; j < columns - 1; j++)
                        if (!mesh_triangle(&mesh, previous + j, previous + j + 1, current + j) ||
                            !mesh_triangle(&mesh, previous + j + 1, current + j, current + j + 1))
                            goto done;
                mesh_vertex *swap = previous;
                previous = current;
                current = swap;
            }
        }
    }
    unsigned paints = paint_background ? 2 : 1;
    if (pixels > st->clip_budget->tests_left / paints)
        goto done;
    st->clip_budget->tests_left -= pixels * paints;
    for (unsigned y = 0; y < st->img->height; y++)
        for (unsigned x = 0; x < st->img->width; x++) {
            const pdf_mesh_sample *p = mesh.pixels + (size_t)y * st->img->width + x;
            if (paint_background && cupidimage_pdf_mesh_inside_box(&mesh, x, y))
                cupidimage_pdf_composite_space(st, (int)x, (int)y, background_sample.values,
                                               &mesh.colors, st->gs.fill_alpha,
                                               st->gs.alpha_is_shape ? st->gs.fill_alpha : 1);
            if (p->painted)
                cupidimage_pdf_composite_space(st, (int)x, (int)y, p->values, &mesh.colors,
                                               st->gs.fill_alpha,
                                               st->gs.alpha_is_shape ? st->gs.fill_alpha : 1);
        }
    ok = !st->image_failed;
done:
    st->form_budget->transparency_bytes_live -= reserved;
    free(mesh.pixels);
    free(rows);
    free(decoded);
    if (mesh.function) {
        cupidimage_pdf_free_function(mesh.function);
        free(mesh.function);
    }
    return ok;
}
