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
int cupidimage_pdf_calibrated_output(pdf_image_colors *space) {
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
                                    size_t *pos, int kind, pdf_image_colors *space) {
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
    space->icc = NULL;
    space->calibrated = kind;
    space->components = kind == 1 ? 1 : 3;
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
    if (ok && kind == 1)
        ok = cupidimage_pdf_graphics_number(st, &dictionary, "Gamma", 0, 1e30, space->gamma) &&
             space->gamma[0] > 0;
    if (ok && kind == 2) {
        ok = calibrated_array(st, &dictionary, "Gamma", space->gamma, 3, 0) &&
             calibrated_array(st, &dictionary, "Matrix", space->matrix, 9, 0);
        for (unsigned i = 0; i < 3; i++)
            ok = ok && space->gamma[i] > 0;
    }
    if (ok && kind == 3) {
        space->lab_range[0] = space->lab_range[2] = -100;
        space->lab_range[1] = space->lab_range[3] = 100;
        ok = calibrated_array(st, &dictionary, "Range", space->lab_range, 4, 0) &&
             space->lab_range[0] <= space->lab_range[1] &&
             space->lab_range[2] <= space->lab_range[3];
    }
    ok = ok && cupidimage_pdf_calibrated_output(space);
    cupidimage_pdf_object_view_release(&value);
    if (ok)
        *pos = end;
    return ok;
}

void cupidimage_pdf_color_range(const pdf_image_colors *space, int component, double *minimum,
                                double *maximum) {
    *minimum = 0;
    *maximum = 1;
    if (space->calibrated != 3)
        return;
    if (component == 0)
        *maximum = 100;
    else if (component < 3) {
        *minimum = space->lab_range[(component - 1) * 2];
        *maximum = space->lab_range[(component - 1) * 2 + 1];
    }
}

double cupidimage_pdf_color_component(const pdf_image_colors *space, int component, double value) {
    double minimum, maximum;
    cupidimage_pdf_color_range(space, component, &minimum, &maximum);
    return fmax(minimum, fmin(maximum, value));
}

double cupidimage_pdf_color_palette_value(const pdf_image_colors *space, int component,
                                          unsigned char value) {
    double minimum, maximum;
    cupidimage_pdf_color_range(space, component, &minimum, &maximum);
    return minimum + value / 255.0 * (maximum - minimum);
}

static double calibrated_lab_inverse(double value) {
    return value >= 6.0 / 29.0 ? value * value * value : (108.0 / 841.0) * (value - 4.0 / 29.0);
}

static double calibrated_encode(double value) {
    value = fmax(0, fmin(1, value));
    return value <= .0031308 ? 12.92 * value : 1.055 * pow(value, 1 / 2.4) - .055;
}

void cupidimage_pdf_calibrated_xyz(const pdf_image_colors *space, const double values[4],
                                   double source[3]) {
    if (space->calibrated == 3) {
        double l = cupidimage_pdf_color_component(space, 0, values[0]);
        double a = cupidimage_pdf_color_component(space, 1, values[1]);
        double b = cupidimage_pdf_color_component(space, 2, values[2]);
        double m = (l + 16) / 116;
        source[0] = space->white[0] * calibrated_lab_inverse(m + a / 500);
        source[1] = calibrated_lab_inverse(m);
        source[2] = space->white[2] * calibrated_lab_inverse(m - b / 200);
    } else if (space->components == 1) {
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
}

void cupidimage_pdf_calibrated_color(const pdf_image_colors *space, const double values[4],
                                     int gray, double out[3]) {
    double source[3];
    cupidimage_pdf_calibrated_xyz(space, values, source);
    cupidimage_pdf_xyz_color(space, source, gray, out);
}

void cupidimage_pdf_xyz_color(const pdf_image_colors *space, const double source[3], int gray,
                              double out[3]) {
    double xyz[3];
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
