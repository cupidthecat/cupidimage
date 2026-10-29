#include "hints.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const double ps_hint_defaults[8] = {.039625, 7, 1, 0, 0, 0, 0, .06};

void cupidimage_pdf_ps_hints_init(pdf_ps_private_hints *hints) {
    memset(hints, 0, sizeof(*hints));
    memcpy(hints->scalars, ps_hint_defaults, sizeof(ps_hint_defaults));
}

void cupidimage_pdf_ps_hint_set(pdf_ps_private_hints *hints, unsigned field, const double *values,
                                unsigned count, int delta, int valid) {
    if (field >= PDF_PS_HINT_FIELDS)
        return;
    unsigned bit = 1u << field;
    hints->seen |= bit;
    hints->invalid &= ~bit;
    if (field < PDF_PS_BLUE_SCALE) {
        static const unsigned limits[6] = {14, 10, 14, 10, 12, 12};
        double decoded[14] = {0}, previous = 0;
        hints->counts[field] = 0;
        memset(hints->arrays[field], 0, sizeof(hints->arrays[field]));
        if (count > limits[field] || (field < PDF_PS_STEM_SNAP_H && count % 2))
            valid = 0;
        if (valid) {
            for (unsigned i = 0; i < count; i++) {
                double value = values[i] + (delta ? previous : 0);
                if (!isfinite(value) || fabs(value) > 1e30) {
                    valid = 0;
                    break;
                }
                decoded[i] = previous = value;
            }
        }
        if (valid) {
            hints->counts[field] = count;
            memcpy(hints->arrays[field], decoded, sizeof(decoded));
        }
    } else {
        unsigned index = field - PDF_PS_BLUE_SCALE;
        hints->scalars[index] = ps_hint_defaults[index];
        if (count != 1 || !isfinite(values[0]) || values[0] < 0 || values[0] > 1e30)
            valid = 0;
        if (valid && (field == PDF_PS_FORCE_BOLD || field == PDF_PS_LANGUAGE_GROUP) &&
            values[0] != 0 && values[0] != 1)
            valid = 0;
        if (valid)
            hints->scalars[index] = values[0];
    }
    /* Invalid hints do not make an otherwise usable unhinted font unreadable. */
    if (!valid)
        hints->invalid |= bit;
}

void cupidimage_pdf_ps_hint_program_free(pdf_ps_hint_program *program) {
    free(program->events);
    memset(program, 0, sizeof(*program));
}
