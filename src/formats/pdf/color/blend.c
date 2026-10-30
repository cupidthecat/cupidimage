#include "../pdf_render_internal.h"
#include "matrix.h"
#include <math.h>
#include <string.h>

static const pdf_blend_space blend_rgb = {.colors = {.components = 3}};
static const pdf_blend_space blend_gray = {.colors = {.components = 1}};

const pdf_blend_space *cupidimage_pdf_current_blend(const pdf_render_state *st) {
    if (st->group && st->group->blend.colors.components)
        return &st->group->blend;
    return st->group && st->group->gray ? &blend_gray : &blend_rgb;
}

static int blend_numbers_equal(const double *a, const double *b, unsigned count) {
    for (unsigned i = 0; i < count; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static int blend_colors_equal(const pdf_image_colors *a, const pdf_image_colors *b) {
    if (a->components != b->components || a->calibrated != b->calibrated ||
        a->encoded_transfer != b->encoded_transfer)
        return 0;
    if (a->icc || b->icc)
        return cupidimage_pdf_icc_equal(a->icc, b->icc);
    if (a->calibrated)
        return ((a->encoded_transfer != 3 && a->encoded_transfer != 4) ||
                blend_numbers_equal(a->source_range, b->source_range, 6)) &&
               (a->encoded_transfer != 4 ||
                blend_numbers_equal(a->source_matrix, b->source_matrix, 9)) &&
               blend_numbers_equal(a->white, b->white, 3) &&
               blend_numbers_equal(a->gamma, b->gamma, 3) &&
               (a->calibrated != 2 || blend_numbers_equal(a->matrix, b->matrix, 9));
    return 1;
}

int cupidimage_pdf_blend_equal(const pdf_blend_space *a, const pdf_blend_space *b) {
    return a && b && blend_colors_equal(&a->colors, &b->colors);
}

int cupidimage_pdf_blend_prepare(const pdf_render_state *st, const pdf_image_colors *colors,
                                 pdf_blend_space *blend) {
    if (!st || !colors || !blend || !st->clip_budget)
        return 0;
    memset(blend, 0, sizeof(*blend));
    if ((colors->components != 1 && colors->components != 3 && colors->components != 4) ||
        colors->pattern || colors->indexed || colors->tint || colors->encoded_transfer ||
        colors->calibrated < 0 || colors->calibrated > 2)
        return 0;
    blend->colors = *colors;
    if (colors->icc)
        return cupidimage_pdf_icc_bidirectional(colors->icc, st->gs.rendering_intent,
                                                &st->clip_budget->tests_left);
    if (!colors->calibrated)
        return 1;
    if (st->clip_budget->tests_left < 64)
        return 0;
    st->clip_budget->tests_left -= 64;
    if (!cupidimage_pdf_color_matrix_inverse(colors->adaptation, blend->inverse_adaptation))
        return 0;
    if (colors->calibrated == 2) {
        double matrix[9];
        for (unsigned i = 0; i < 3; i++)
            for (unsigned j = 0; j < 3; j++)
                matrix[i * 3 + j] = colors->matrix[j * 3 + i];
        if (!cupidimage_pdf_color_matrix_inverse(matrix, blend->inverse_matrix))
            return 0;
    }
    return 1;
}

static int blend_working_xyz(const pdf_render_state *st, const pdf_image_colors *space,
                             const double *values, double xyz[3]) {
    if (space->icc)
        return cupidimage_pdf_icc_working_xyz(space->icc, values, st->gs.rendering_intent, xyz);
    if (space->calibrated) {
        double source[3];
        cupidimage_pdf_calibrated_xyz(space, values, source);
        return cupidimage_pdf_color_matrix_apply(space->adaptation, source, xyz);
    }
    if (space->components != 1 && space->components != 3 && space->components != 4)
        return 0;
    double rgb[3];
    cupidimage_pdf_device_color(values, space->components, 0, rgb);
    for (unsigned i = 0; i < 3; i++)
        rgb[i] = rgb[i] <= .04045 ? rgb[i] / 12.92 : pow((rgb[i] + .055) / 1.055, 2.4);
    /* Inverse of the fixed output matrix, retaining its full double precision. */
    static const double to_xyz[9] = {0.41245643226823603, 0.35757607628002747, 0.1804374802944501,
                                     0.212672846318362,   0.7151521671548808,  0.07217499957321262,
                                     0.01933390410329902, 0.11919202824322096, 0.950304073677404};
    return cupidimage_pdf_color_matrix_apply(to_xyz, rgb, xyz);
}

int cupidimage_pdf_blend_convert(const pdf_render_state *st, const pdf_image_colors *source,
                                 const pdf_blend_space *destination, const double *values,
                                 double *output) {
    if (!st || !source || !values || !output)
        return 0;
    if (!destination)
        destination = &blend_rgb;
    const pdf_image_colors *target = &destination->colors;
    if (!target->icc && !target->calibrated && target->components == 4) {
        if (!source->icc && !source->calibrated) {
            if (source->components == 4) {
                for (unsigned i = 0; i < 4; i++)
                    output[i] = fmax(0, fmin(1, values[i]));
                return 1;
            }
            if (source->components == 1) {
                double gray = fmax(0, fmin(1, values[0]));
                output[0] = output[1] = output[2] = 0;
                output[3] = 1 - gray;
                return 1;
            }
            return source->components == 3 && cupidimage_pdf_rgb_to_cmyk(st, values, 1, output);
        }
        double rgb[3];
        if (!cupidimage_pdf_blend_convert(st, source, &blend_rgb, values, rgb))
            return 0;
        return cupidimage_pdf_rgb_to_cmyk(st, rgb, 0, output);
    }
    if (!target->icc && !target->calibrated) {
        int gray = target->components == 1;
        if (source->icc)
            return cupidimage_pdf_icc_color(source->icc, values, gray, st->gs.rendering_intent,
                                            output);
        if (source->calibrated) {
            cupidimage_pdf_calibrated_color(source, values, gray, output);
            return 1;
        }
        if (source->components != 1 && source->components != 3 && source->components != 4)
            return 0;
        cupidimage_pdf_device_color(values, source->components, gray, output);
        return 1;
    }
    if (blend_colors_equal(source, target)) {
        for (int i = 0; i < target->components; i++)
            output[i] = fmax(0, fmin(1, values[i]));
        if (target->components == 1)
            output[1] = output[2] = output[0];
        return 1;
    }
    double xyz[3], native[3], linear[3];
    if (!blend_working_xyz(st, source, values, xyz))
        return 0;
    if (target->icc)
        return cupidimage_pdf_icc_from_working(target->icc, xyz, st->gs.rendering_intent, output,
                                               &st->clip_budget->tests_left);
    if (st->clip_budget->tests_left < 8)
        return 0;
    st->clip_budget->tests_left -= 8;
    if (!cupidimage_pdf_color_matrix_apply(destination->inverse_adaptation, xyz, native))
        return 0;
    if (target->calibrated == 1)
        linear[0] = native[1];
    else if (!cupidimage_pdf_color_matrix_apply(destination->inverse_matrix, native, linear))
        return 0;
    for (int i = 0; i < target->components; i++) {
        double value = fmax(0, fmin(1, linear[i]));
        output[i] = pow(value, 1 / target->gamma[i]);
        if (!isfinite(output[i]) || fabs(pow(output[i], target->gamma[i]) - value) > 1e-9)
            return 0;
    }
    if (target->components == 1)
        output[1] = output[2] = output[0];
    return 1;
}

int cupidimage_pdf_blend_luminosity(const pdf_render_state *st, const pdf_blend_space *blend,
                                    const double *color, double *output) {
    if (!st || !blend || !color || !output)
        return 0;
    const pdf_image_colors *space = &blend->colors;
    double values[4] = {color[0], color[1], color[2], space->components == 4 ? color[3] : 0};
    if (space->icc)
        return cupidimage_pdf_icc_luminosity(space->icc, values, st->gs.rendering_intent, output);
    if (space->calibrated) {
        double xyz[3];
        cupidimage_pdf_calibrated_xyz(space, values, xyz);
        if (!isfinite(xyz[1]))
            return 0;
        *output = fmax(0, fmin(1, xyz[1]));
    } else {
        double gray[3];
        cupidimage_pdf_device_color(values, space->components, 1, gray);
        *output = gray[0];
    }
    return 1;
}
