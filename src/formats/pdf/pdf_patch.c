#include "pdf_mesh_internal.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    double x, y;
} patch_point;

typedef struct {
    patch_point p[4][4];
    double u0, u1, v0, v1;
} patch_cell;

typedef struct {
    pdf_mesh_context *mesh;
    uint32_t *stamp, serial;
    double *parameters;
    double colors[4][PDF_COLOR_COMPONENTS];
} patch_target;

static patch_point patch_mid(patch_point a, patch_point b) {
    patch_point p = {(a.x + b.x) * .5, (a.y + b.y) * .5};
    return p;
}

static double patch_cross(patch_point a, patch_point b) {
    return a.x * b.y - a.y * b.x;
}

static patch_point patch_sub(patch_point a, patch_point b) {
    patch_point p = {a.x - b.x, a.y - b.y};
    return p;
}

static int patch_charge(patch_target *target, size_t work) {
    size_t *left = &target->mesh->state->form_budget->shade_evaluations_left;
    if (work > *left)
        return 0;
    *left -= work;
    return 1;
}

static patch_point patch_bilinear(const patch_cell *cell, double u, double v) {
    double w[4] = {(1 - u) * (1 - v), (1 - u) * v, u * v, u * (1 - v)};
    patch_point p = {0, 0};
    const patch_point corners[4] = {cell->p[0][0], cell->p[3][0], cell->p[3][3], cell->p[0][3]};
    for (unsigned i = 0; i < 4; i++) {
        p.x += w[i] * corners[i].x;
        p.y += w[i] * corners[i].y;
    }
    return p;
}

static void patch_curve_split(const patch_point p[4], patch_point low[4], patch_point high[4]) {
    patch_point a = patch_mid(p[0], p[1]), b = patch_mid(p[1], p[2]), c = patch_mid(p[2], p[3]);
    patch_point d = patch_mid(a, b), e = patch_mid(b, c), f = patch_mid(d, e);
    low[0] = p[0];
    low[1] = a;
    low[2] = d;
    low[3] = f;
    high[0] = f;
    high[1] = e;
    high[2] = c;
    high[3] = p[3];
}

static void patch_split(const patch_cell *cell, int vertical, patch_cell *low, patch_cell *high) {
    *low = *cell;
    *high = *cell;
    for (unsigned i = 0; i < 4; i++) {
        patch_point p[4], a[4], b[4];
        for (unsigned j = 0; j < 4; j++)
            p[j] = vertical ? cell->p[j][i] : cell->p[i][j];
        patch_curve_split(p, a, b);
        for (unsigned j = 0; j < 4; j++) {
            if (vertical) {
                low->p[j][i] = a[j];
                high->p[j][i] = b[j];
            } else {
                low->p[i][j] = a[j];
                high->p[i][j] = b[j];
            }
        }
    }
    if (vertical)
        low->v1 = high->v0 = (cell->v0 + cell->v1) * .5;
    else
        low->u1 = high->u0 = (cell->u0 + cell->u1) * .5;
}

/* Derivatives are convex combinations of control-net differences. A common
 * cross-product sign excludes an interior fold; mixed signs require subdivision. */
static int patch_orientation(const patch_cell *cell) {
    patch_point du[12], dv[12];
    unsigned n = 0;
    for (unsigned j = 0; j < 4; j++)
        for (unsigned i = 0; i < 3; i++)
            du[n++] = patch_sub(cell->p[j][i + 1], cell->p[j][i]);
    n = 0;
    for (unsigned j = 0; j < 3; j++)
        for (unsigned i = 0; i < 4; i++)
            dv[n++] = patch_sub(cell->p[j + 1][i], cell->p[j][i]);
    int sign = 0;
    for (unsigned i = 0; i < 12; i++)
        for (unsigned j = 0; j < 12; j++) {
            double cross = patch_cross(du[i], dv[j]);
            int current = cross > 0 ? 1 : cross < 0 ? -1 : 0;
            if (current && sign && current != sign)
                return 0;
            if (current)
                sign = current;
        }
    return sign ? 1 : 2;
}

static double patch_flatness(const patch_cell *cell) {
    double error = 0;
    for (unsigned j = 0; j < 4; j++)
        for (unsigned i = 0; i < 4; i++) {
            patch_point b = patch_bilinear(cell, i / 3.0, j / 3.0);
            error = fmax(error, hypot(cell->p[j][i].x - b.x, cell->p[j][i].y - b.y));
        }
    patch_point cross = {cell->p[0][0].x - cell->p[3][0].x + cell->p[3][3].x - cell->p[0][3].x,
                         cell->p[0][0].y - cell->p[3][0].y + cell->p[3][3].y - cell->p[0][3].y};
    return error + .25 * hypot(cross.x, cross.y);
}

static int patch_evaluate(patch_target *target, const patch_cell *cell, double u, double v,
                          patch_point *point, patch_point *du, patch_point *dv) {
    if (!patch_charge(target, 16))
        return 0;
    double a = 1 - u, b = 1 - v;
    double bu[4] = {a * a * a, 3 * u * a * a, 3 * u * u * a, u * u * u},
           bv[4] = {b * b * b, 3 * v * b * b, 3 * v * v * b, v * v * v};
    double d_u[4] = {-3 * a * a, 3 * a * a - 6 * u * a, 6 * u * a - 3 * u * u, 3 * u * u};
    double d_v[4] = {-3 * b * b, 3 * b * b - 6 * v * b, 6 * v * b - 3 * v * v, 3 * v * v};
    *point = cell->p[0][0];
    du->x = du->y = dv->x = dv->y = 0;
    for (unsigned j = 0; j < 4; j++)
        for (unsigned i = 0; i < 4; i++) {
            patch_point p = patch_sub(cell->p[j][i], cell->p[0][0]);
            point->x += p.x * bu[i] * bv[j];
            point->y += p.y * bu[i] * bv[j];
            du->x += p.x * d_u[i] * bv[j];
            du->y += p.y * d_u[i] * bv[j];
            dv->x += p.x * bu[i] * d_v[j];
            dv->y += p.y * bu[i] * d_v[j];
        }
    return isfinite(point->x) && isfinite(point->y) && isfinite(du->x) && isfinite(du->y) &&
           isfinite(dv->x) && isfinite(dv->y);
}

/* Return -1 for a work/numeric failure, 0 when subdivision is needed, or 1
 * with an inverse parameter pair. Geometry is evaluated at the actual sample. */
static int patch_inverse(patch_target *target, const patch_cell *cell, double x, double y,
                         double tolerance, double *u, double *v) {
    *u = *v = .5;
    for (unsigned step = 0; step < 24; step++) {
        patch_point point, du, dv;
        if (!patch_evaluate(target, cell, *u, *v, &point, &du, &dv))
            return -1;
        double rx = point.x - x, ry = point.y - y;
        if (fmax(fabs(rx), fabs(ry)) <= tolerance) {
            if (*u < -1e-10 || *u > 1 + 1e-10 || *v < -1e-10 || *v > 1 + 1e-10)
                return 0;
            *u = fmax(0, fmin(1, *u));
            *v = fmax(0, fmin(1, *v));
            return 1;
        }
        double det = patch_cross(du, dv);
        if (!isfinite(det))
            return -1;
        if (!det)
            return 0;
        double su = (rx * dv.y - ry * dv.x) / det, sv = (du.x * ry - du.y * rx) / det;
        if (!isfinite(su) || !isfinite(sv) || fabs(su) > 2 || fabs(sv) > 2)
            return 0;
        *u -= su;
        *v -= sv;
        if (*u < -.25 || *u > 1.25 || *v < -.25 || *v > 1.25)
            return 0;
    }
    return 0;
}

static double patch_turn(patch_point a, patch_point b, patch_point c) {
    return patch_cross(patch_sub(b, a), patch_sub(c, a));
}

static unsigned patch_hull(const patch_cell *cell, patch_point hull[32]) {
    patch_point points[16];
    unsigned n = 0;
    for (unsigned j = 0; j < 4; j++)
        for (unsigned i = 0; i < 4; i++) {
            patch_point p = cell->p[j][i];
            unsigned at = n;
            while (at && (points[at - 1].x > p.x ||
                          (points[at - 1].x == p.x && points[at - 1].y > p.y))) {
                points[at] = points[at - 1];
                at--;
            }
            points[at] = p;
            n++;
        }
    unsigned count = 0;
    for (unsigned i = 0; i < n; i++) {
        if (i && points[i].x == points[i - 1].x && points[i].y == points[i - 1].y)
            continue;
        while (count >= 2 && patch_turn(hull[count - 2], hull[count - 1], points[i]) <= 0)
            count--;
        hull[count++] = points[i];
    }
    unsigned lower = count;
    for (int i = (int)n - 2; i >= 0; i--) {
        if (points[i].x == points[i + 1].x && points[i].y == points[i + 1].y)
            continue;
        while (count > lower && patch_turn(hull[count - 2], hull[count - 1], points[i]) <= 0)
            count--;
        hull[count++] = points[i];
    }
    return count ? count - 1 : 0;
}

static int patch_sample(patch_target *target, unsigned x, unsigned y, double u, double v) {
    pdf_mesh_context *mesh = target->mesh;
    size_t pixel = (size_t)y * mesh->state->img->width + x;
    if (target->stamp[pixel] == target->serial) {
        double old_u = target->parameters[pixel * 2], old_v = target->parameters[pixel * 2 + 1];
        /* Equivalent inverse roots can differ by roundoff in v. Resolve that
         * numerical tie with the specified u priority, not traversal order. */
        double difference = v - old_v, roundoff = 64 * DBL_EPSILON;
        if (difference < -roundoff || (fabs(difference) <= roundoff && u <= old_u))
            return 1;
    }
    double values[PDF_COLOR_COMPONENTS] = {0};
    for (int c = 0; c < mesh->interpolate; c++) {
        /* Difference-form interpolation preserves constant components exactly. */
        double left = target->colors[0][c] + v * (target->colors[1][c] - target->colors[0][c]);
        double right = target->colors[3][c] + v * (target->colors[2][c] - target->colors[3][c]);
        values[c] = left + u * (right - left);
    }
    if (mesh->function) {
        double low = fmin(mesh->decode[4], mesh->decode[5]),
               high = fmax(mesh->decode[4], mesh->decode[5]);
        double input[2] = {fmin(high, fmax(low, values[0])), 0};
        if (!cupidimage_pdf_evaluate_function(mesh->function, input, values,
                                              &mesh->state->form_budget->shade_evaluations_left))
            return 0;
    }
    cupidimage_pdf_mesh_color(&mesh->colors, values, mesh->pixels + pixel, 0);
    target->parameters[pixel * 2] = u;
    target->parameters[pixel * 2 + 1] = v;
    target->stamp[pixel] = target->serial;
    return 1;
}

static int patch_walk(patch_target *target, const patch_cell *cell, unsigned depth) {
    if (!patch_charge(target, 16))
        return 0;
    double xmin = cell->p[0][0].x, xmax = xmin, ymin = cell->p[0][0].y, ymax = ymin;
    for (unsigned j = 0; j < 4; j++)
        for (unsigned i = 0; i < 4; i++) {
            xmin = fmin(xmin, cell->p[j][i].x);
            xmax = fmax(xmax, cell->p[j][i].x);
            ymin = fmin(ymin, cell->p[j][i].y);
            ymax = fmax(ymax, cell->p[j][i].y);
        }
    unsigned width = target->mesh->state->img->width, height = target->mesh->state->img->height;
    unsigned x0 = (unsigned)fmax(0, fmin(width, ceil(xmin - .5))),
             x1 = (unsigned)fmax(0, fmin(width, floor(xmax - .5) + 1));
    unsigned y0 = (unsigned)fmax(0, fmin(height, ceil(ymin - .5))),
             y1 = (unsigned)fmax(0, fmin(height, floor(ymax - .5) + 1));
    if (x0 >= x1 || y0 >= y1)
        return 1;
    if (!patch_charge(target, 160))
        return 0;
    int orientation = patch_orientation(cell);
    if (orientation == 2)
        return 1;
    int subdivide = !orientation || patch_flatness(cell) > .25;
    if (!subdivide) {
        patch_point hull[32];
        unsigned count = patch_hull(cell, hull);
        if (count < 3)
            return 1;
        if (!patch_charge(target, (size_t)(x1 - x0) * (y1 - y0)))
            return 0;
        double tolerance = 1e-10 * fmin(1, fmax(xmax - xmin, ymax - ymin));
        for (unsigned y = y0; y < y1 && !subdivide; y++)
            for (unsigned x = x0; x < x1; x++) {
                patch_point sample = {x + .5, y + .5};
                int inside = 1;
                if (!cupidimage_pdf_mesh_inside_box(target->mesh, x, y))
                    continue;
                for (unsigned i = 0; i < count; i++)
                    if (patch_turn(hull[i], hull[(i + 1) % count], sample) < 0) {
                        inside = 0;
                        break;
                    }
                if (!inside)
                    continue;
                double u, v;
                int found = patch_inverse(target, cell, sample.x, sample.y, tolerance, &u, &v);
                if (found < 0)
                    return 0;
                if (!found) {
                    subdivide = 1;
                    break;
                }
                u = cell->u0 + u * (cell->u1 - cell->u0);
                v = cell->v0 + v * (cell->v1 - cell->v0);
                if (!patch_sample(target, x, y, u, v))
                    return 0;
            }
    }
    if (!subdivide)
        return 1;
    if (depth == 20)
        return 0;
    patch_cell left, right, low, high;
    patch_split(cell, 0, &left, &right);
    patch_split(&left, 1, &low, &high);
    if (!patch_walk(target, &low, depth + 1) || !patch_walk(target, &high, depth + 1))
        return 0;
    patch_split(&right, 1, &low, &high);
    return patch_walk(target, &low, depth + 1) && patch_walk(target, &high, depth + 1);
}

int cupidimage_pdf_paint_patches(pdf_mesh_context *mesh, const unsigned char *data, size_t length,
                                 int type, int draw) {
    if ((type != 6 && type != 7) || !length)
        return 0;
    patch_target target = {0};
    target.mesh = mesh;
    patch_point previous[16] = {0};
    double previous_colors[4][PDF_COLOR_COMPONENTS] = {{0}};
    size_t bytes = 0, pixels = (size_t)mesh->state->img->width * mesh->state->img->height, pos = 0;
    int ok = 0;
    if (draw) {
        size_t live = mesh->state->form_budget->transparency_bytes_live;
        size_t per_pixel = sizeof(uint32_t) + 2 * sizeof(double);
        if (live > PDF_TRANSPARENCY_MEMORY_LIMIT ||
            pixels > (PDF_TRANSPARENCY_MEMORY_LIMIT - live) / per_pixel)
            return 0;
        bytes = pixels * per_pixel;
        mesh->state->form_budget->transparency_bytes_live += bytes;
        target.stamp = calloc(pixels, sizeof(*target.stamp));
        target.parameters = malloc(pixels * 2 * sizeof(double));
        if (!target.stamp || !target.parameters)
            goto done;
    }
    while (pos < length) {
        unsigned bit = 0, flag = cupidimage_pdf_mesh_bits(data + pos, &bit, mesh->flag_bits) & 3u;
        if (flag && !target.serial)
            goto done;
        unsigned points = type == 6 ? 12 : 16, start = flag ? 4 : 0, colors = flag ? 2 : 4;
        unsigned bits = mesh->flag_bits + (points - start) * 2 * mesh->coordinate_bits +
                        colors * mesh->samples * mesh->component_bits;
        size_t record = (bits + 7u) / 8;
        if (record > length - pos || !patch_charge(&target, points + colors * mesh->samples))
            goto done;
        patch_point p[16];
        double corner[4][PDF_COLOR_COMPONENTS] = {{0}};
        if (flag) {
            for (unsigned i = 0; i < 4; i++)
                p[i] = previous[(flag * 3 + i) % 12];
            memcpy(corner[0], previous_colors[flag], sizeof(corner[0]));
            memcpy(corner[1], previous_colors[(flag + 1) % 4], sizeof(corner[1]));
        }
        const float *m = mesh->state->gs.ctm;
        for (unsigned i = start; i < points; i++) {
            double x = cupidimage_pdf_mesh_decode(
                cupidimage_pdf_mesh_bits(data + pos, &bit, mesh->coordinate_bits),
                mesh->coordinate_bits, mesh->decode);
            double y = cupidimage_pdf_mesh_decode(
                cupidimage_pdf_mesh_bits(data + pos, &bit, mesh->coordinate_bits),
                mesh->coordinate_bits, mesh->decode + 2);
            p[i].x = (m[0] * x + m[2] * y + m[4] - mesh->state->page_x0_pts) * (96.0 / 72.0);
            p[i].y = (mesh->state->page_top_pts - (m[1] * x + m[3] * y + m[5])) * (96.0 / 72.0);
            if (!isfinite(p[i].x) || !isfinite(p[i].y))
                goto done;
        }
        for (unsigned i = flag ? 2 : 0; i < 4; i++) {
            for (int c = 0; c < mesh->samples; c++)
                corner[i][c] = cupidimage_pdf_mesh_decode(
                    cupidimage_pdf_mesh_bits(data + pos, &bit, mesh->component_bits),
                    mesh->component_bits, mesh->decode + 4 + c * 2);
            if (mesh->colors.indexed) {
                unsigned index =
                    (unsigned)floor(fmin(mesh->colors.high, fmax(0, corner[i][0])) + .5);
                for (int c = 0; c < mesh->colors.components; c++)
                    corner[i][c] = cupidimage_pdf_color_palette_value(
                        &mesh->colors, c,
                        mesh->colors.palette[index * mesh->colors.components + c]);
            }
        }
        memcpy(previous, p, points * sizeof(*p));
        memcpy(previous_colors, corner, sizeof(corner));
        if (target.serial == UINT32_MAX)
            goto done;
        target.serial++;
        pos += record;
        if (draw) {
            static const unsigned char order[16][2] = {
                {0, 0}, {0, 1}, {0, 2}, {0, 3}, {1, 3}, {2, 3}, {3, 3}, {3, 2},
                {3, 1}, {3, 0}, {2, 0}, {1, 0}, {1, 1}, {1, 2}, {2, 2}, {2, 1}};
            patch_cell cell = {0};
            cell.u1 = cell.v1 = 1;
            for (unsigned i = 0; i < points; i++)
                cell.p[order[i][1]][order[i][0]] = p[i];
            if (type == 6)
                for (unsigned j = 1; j < 3; j++)
                    for (unsigned i = 1; i < 3; i++) {
                        double u = i / 3.0, v = j / 3.0;
                        patch_point b = patch_bilinear(&cell, u, v);
                        cell.p[j][i].x = (1 - v) * cell.p[0][i].x + v * cell.p[3][i].x +
                                         (1 - u) * cell.p[j][0].x + u * cell.p[j][3].x - b.x;
                        cell.p[j][i].y = (1 - v) * cell.p[0][i].y + v * cell.p[3][i].y +
                                         (1 - u) * cell.p[j][0].y + u * cell.p[j][3].y - b.y;
                    }
            memcpy(target.colors, corner, sizeof(corner));
            if (!patch_walk(&target, &cell, 0))
                goto done;
        }
    }
    ok = target.serial != 0;
done:
    mesh->state->form_budget->transparency_bytes_live -= bytes;
    free(target.stamp);
    free(target.parameters);
    return ok;
}
