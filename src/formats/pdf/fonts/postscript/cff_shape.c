#include "cff_shape.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int cff_hint_component(const pdf_cff_font *font, unsigned glyph,
                              const pdf_ps_gridfit_size *size, pdf_cff_outline *output,
                              size_t *work_left, unsigned depth) {
    pdf_cff_outline source = {0}, accent = {0}, base = {0};
    pdf_ps_hint_program hints = {0};
    int ok = 0;
    memset(output, 0, sizeof(*output));
    if (!cupidimage_pdf_type2_outline_budget(font, glyph, &source, &hints, work_left))
        goto done;
    if (!source.composite) {
        if (!source.count) {
            output->width = source.width;
            output->width_y = source.width_y;
            ok = 1;
        } else {
            unsigned fd = font->fdselect ? font->fdselect[glyph] : 0;
            ok = cupidimage_pdf_ps_gridfit(&font->private_dicts[fd].hints, &hints, &source, size,
                                           output, work_left);
        }
        goto done;
    }
    if (depth || !isfinite(source.accent_x) || !isfinite(source.accent_y))
        goto done;
    unsigned a = cupidimage_pdf_cff_standard_glyph(font, source.accent);
    unsigned b = cupidimage_pdf_cff_standard_glyph(font, source.base);
    if (a == glyph || b == glyph)
        goto done;
    pdf_ps_gridfit_size accent_size = *size;
    double x = round(source.accent_x * 65536) + size->origin_x;
    double y = round(source.accent_y * 65536) + size->origin_y;
    if (!isfinite(x) || !isfinite(y) || fabs(x) > 4096.0 * 65536 || fabs(y) > 4096.0 * 65536)
        goto done;
    accent_size.origin_x = (int32_t)x;
    accent_size.origin_y = (int32_t)y;
    if (!cff_hint_component(font, a, &accent_size, &accent, work_left, 1) ||
        !cff_hint_component(font, b, size, &base, work_left, 1) ||
        accent.count > 65536 - base.count)
        goto done;
    unsigned count = accent.count + base.count;
    if (count > *work_left)
        goto done;
    *work_left -= count;
    if (count) {
        output->segments = malloc((size_t)count * sizeof(*output->segments));
        if (!output->segments)
            goto done;
        if (accent.count)
            memcpy(output->segments, accent.segments,
                   (size_t)accent.count * sizeof(*output->segments));
        if (base.count)
            memcpy(output->segments + accent.count, base.segments,
                   (size_t)base.count * sizeof(*output->segments));
    }
    output->count = output->capacity = count;
    output->width = source.width;
    output->width_y = source.width_y;
    ok = 1;
done:
    cupidimage_pdf_type2_free(&source);
    cupidimage_pdf_type2_free(&accent);
    cupidimage_pdf_type2_free(&base);
    cupidimage_pdf_ps_hint_program_free(&hints);
    if (!ok)
        cupidimage_pdf_type2_free(output);
    return ok;
}

int cupidimage_pdf_cff_hint_outline(const pdf_cff_font *font, unsigned glyph,
                                    const pdf_ps_gridfit_size *size, pdf_cff_outline *output,
                                    size_t *work_left) {
    if (!output)
        return 0;
    memset(output, 0, sizeof(*output));
    if (!font || !size || !work_left || size->x_scale <= 0 || size->y_scale <= 0 ||
        size->x_scale > 2 * 65536 || size->y_scale > 2 * 65536 ||
        (int64_t)size->origin_x < -4096 * 65536 || (int64_t)size->origin_x > 4096 * 65536 ||
        (int64_t)size->origin_y < -4096 * 65536 || (int64_t)size->origin_y > 4096 * 65536)
        return 0;
    return cff_hint_component(font, glyph, size, output, work_left, 0);
}
