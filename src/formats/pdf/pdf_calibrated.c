#include "pdf_functions.h"
#include <math.h>
#include <string.h>

static int calibrated_array(const pdf_render_state *st, const pdf_object_view *view,
                            const char *key, double *values, int count, int required) {
    pdf_object_view value = {0};
    int present = cupidimage_pdf_optional_value(st->document, st->document_size, view, key, &value);
    cupidimage_pdf_object_view_release(&value);
    if (present <= 0)
        return present == 0 && !required;
    if (!cupidimage_pdf_function_array(st, view, key, values, count))
        return 0;
    for (int i = 0; i < count; i++)
        if (!isfinite(values[i]) || fabs(values[i]) > 1e30)
            return 0;
    return 1;
}

static void calibrated_multiply(const double matrix[9], const double value[3], double out[3]) {
    for (unsigned i = 0; i < 3; i++)
        out[i] =
            matrix[i * 3] * value[0] + matrix[i * 3 + 1] * value[1] + matrix[i * 3 + 2] * value[2];
}

/* ICC.1:2022 Annex E.3, linearized Bradford adaptation to the sRGB D65 white. */
static int calibrated_adaptation(pdf_image_colors *space) {
    static const double bradford[9] = {.8951, .2664, -.1614, -.7502, 1.7135,
                                       .0367, .0389, -.0685, 1.0296};
    static const double d65[3] = {.95047, 1, 1.08883};
    double inverse[9], source[3], target[3];
    const double *m = bradford;
    double determinant = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                         m[2] * (m[3] * m[7] - m[4] * m[6]);
    inverse[0] = (m[4] * m[8] - m[5] * m[7]) / determinant;
    inverse[1] = (m[2] * m[7] - m[1] * m[8]) / determinant;
    inverse[2] = (m[1] * m[5] - m[2] * m[4]) / determinant;
    inverse[3] = (m[5] * m[6] - m[3] * m[8]) / determinant;
    inverse[4] = (m[0] * m[8] - m[2] * m[6]) / determinant;
    inverse[5] = (m[2] * m[3] - m[0] * m[5]) / determinant;
    inverse[6] = (m[3] * m[7] - m[4] * m[6]) / determinant;
    inverse[7] = (m[1] * m[6] - m[0] * m[7]) / determinant;
    inverse[8] = (m[0] * m[4] - m[1] * m[3]) / determinant;
    calibrated_multiply(bradford, space->white, source);
    calibrated_multiply(bradford, d65, target);
    for (unsigned i = 0; i < 3; i++) {
        if (!source[i])
            return 0;
        target[i] /= source[i];
        if (!isfinite(target[i]))
            return 0;
    }
    for (unsigned row = 0; row < 3; row++)
        for (unsigned column = 0; column < 3; column++) {
            double value = 0;
            for (unsigned i = 0; i < 3; i++)
                value += inverse[row * 3 + i] * target[i] * bradford[i * 3 + column];
            if (!isfinite(value))
                return 0;
            space->adaptation[row * 3 + column] = value;
        }
    return 1;
}

int cupidimage_pdf_calibrated_space(const pdf_render_state *st, const pdf_object_view *parent,
                                    size_t *pos, int components, pdf_image_colors *space) {
    pdf_object_view value = {0}, dictionary = {0};
    size_t end = *pos;
    pdf_object_id object;
    if (!cupidimage_pdf_parse_reference(parent, &end, &object) &&
        !cupidimage_pdf_dictionary_view(parent, *pos, &dictionary, &end))
        return 0;
    if (!cupidimage_pdf_resolve_value(st->document, st->document_size, parent, *pos, &value, 0))
        return 0;
    size_t ignored;
    int ok = cupidimage_pdf_dictionary_view(&value, 0, &dictionary, &ignored);
    space->calibrated = 1;
    space->components = components;
    space->gamma[0] = space->gamma[1] = space->gamma[2] = 1;
    memset(space->matrix, 0, sizeof(space->matrix));
    space->matrix[0] = space->matrix[4] = space->matrix[8] = 1;
    double black[3] = {0};
    ok = ok && calibrated_array(st, &dictionary, "WhitePoint", space->white, 3, 1) &&
         calibrated_array(st, &dictionary, "BlackPoint", black, 3, 0) && space->white[0] > 0 &&
         space->white[1] == 1 && space->white[2] > 0;
    /* Nonzero black-point mapping needs the output-intent/black-compensation pipeline. */
    for (unsigned i = 0; i < 3; i++)
        ok = ok && black[i] == 0;
    if (ok && components == 1)
        ok = cupidimage_pdf_graphics_number(st, &dictionary, "Gamma", 0, 1e30, space->gamma) &&
             space->gamma[0] > 0;
    if (ok && components == 3) {
        ok = calibrated_array(st, &dictionary, "Gamma", space->gamma, 3, 0) &&
             calibrated_array(st, &dictionary, "Matrix", space->matrix, 9, 0);
        for (unsigned i = 0; i < 3; i++)
            ok = ok && space->gamma[i] > 0;
    }
    ok = ok && calibrated_adaptation(space);
    cupidimage_pdf_object_view_release(&value);
    if (ok)
        *pos = end;
    return ok;
}

static double calibrated_encode(double value) {
    value = fmax(0, fmin(1, value));
    return value <= .0031308 ? 12.92 * value : 1.055 * pow(value, 1 / 2.4) - .055;
}

void cupidimage_pdf_calibrated_color(const pdf_image_colors *space, const double values[4],
                                     int gray, double out[3]) {
    double source[3], xyz[3];
    if (space->components == 1) {
        double value = pow(fmax(0, fmin(1, values[0])), space->gamma[0]);
        for (unsigned i = 0; i < 3; i++)
            source[i] = space->white[i] * value;
    } else {
        double value[3];
        for (unsigned i = 0; i < 3; i++)
            value[i] = pow(fmax(0, fmin(1, values[i])), space->gamma[i]);
        /* PDF Matrix is [XA YA ZA XB YB ZB XC YC ZC]. */
        for (unsigned i = 0; i < 3; i++)
            source[i] = space->matrix[i] * value[0] + space->matrix[3 + i] * value[1] +
                        space->matrix[6 + i] * value[2];
    }
    calibrated_multiply(space->adaptation, source, xyz);
    if (gray) {
        out[0] = out[1] = out[2] = calibrated_encode(xyz[1]);
        return;
    }
    static const double to_rgb[9] = {3.2404542, -1.5371385, -.4985314, -.9692660, 1.8760108,
                                     .0415560,  .0556434,   -.2040259, 1.0572252};
    calibrated_multiply(to_rgb, xyz, out);
    for (unsigned i = 0; i < 3; i++)
        out[i] = calibrated_encode(out[i]);
}
