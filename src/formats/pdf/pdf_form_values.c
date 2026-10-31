#include "pdf_functions.h"
#include <math.h>

int cupidimage_pdf_form_matrix(const pdf_render_state *st, const pdf_object_view *form,
                               float matrix[6]) {
    cupidimage_pdf_matrix_identity(matrix);
    pdf_object_view value = {0};
    int status =
        cupidimage_pdf_optional_value(st->document, st->document_size, form, "Matrix", &value);
    cupidimage_pdf_object_view_release(&value);
    if (status <= 0)
        return status == 0;
    double numbers[6];
    if (!cupidimage_pdf_function_array(st, form, "Matrix", numbers, 6))
        return 0;
    for (unsigned i = 0; i < 6; i++) {
        if (!isfinite(numbers[i]) || fabs(numbers[i]) > 1e30)
            return 0;
        matrix[i] = (float)numbers[i];
    }
    return 1;
}

void cupidimage_pdf_matrix_identity(float m[6]) {
    m[0] = 1.0f;
    m[1] = 0.0f;
    m[2] = 0.0f;
    m[3] = 1.0f;
    m[4] = 0.0f;
    m[5] = 0.0f;
}

void cupidimage_pdf_matrix_concat(float dst[6], const float left[6]) {
    float a = dst[0] * left[0] + dst[2] * left[1];
    float b = dst[1] * left[0] + dst[3] * left[1];
    float c = dst[0] * left[2] + dst[2] * left[3];
    float d = dst[1] * left[2] + dst[3] * left[3];
    float e = dst[0] * left[4] + dst[2] * left[5] + dst[4];
    float f = dst[1] * left[4] + dst[3] * left[5] + dst[5];
    dst[0] = a;
    dst[1] = b;
    dst[2] = c;
    dst[3] = d;
    dst[4] = e;
    dst[5] = f;
}
