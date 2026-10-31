#include "pdf_functions.h"
#include <math.h>

int cupidimage_pdf_form_matrix(const pdf_render_state *st, const pdf_object_view *form,
                               double matrix[6]) {
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
        matrix[i] = numbers[i];
    }
    return 1;
}
