#include "page.h"
#include <limits.h>
#include <math.h>
#include <string.h>

static int ps_page_fixed(double value, int32_t *result) {
    value = trunc(value * 65536);
    if (!isfinite(value) || value < INT32_MIN || value > INT32_MAX)
        return 0;
    *result = (int32_t)value;
    return 1;
}

static int ps_page_offset(double value, int32_t *result) {
    value = round(value * 64);
    if (!isfinite(value) || value < INT32_MIN || value > INT32_MAX)
        return 0;
    *result = (int32_t)value;
    return 1;
}

typedef int (*ps_page_builder)(const void *, unsigned, const pdf_ps_gridfit_size *,
                               pdf_cff_outline *, size_t *);

static int ps_page_shape(const void *font, unsigned glyph, const double f[6],
                         const double text_matrix[6], size_t *work_left, pdf_cff_outline *output,
                         int *fitted, ps_page_builder build) {
    for (unsigned i = 0; i < 6; i++)
        if (!isfinite(text_matrix[i]) || !isfinite(f[i]) || fabs(text_matrix[i]) > 1e30 ||
            fabs(f[i]) > 1e30)
            return 0;
    if (text_matrix[0] * text_matrix[3] == text_matrix[1] * text_matrix[2] ||
        f[0] * f[3] == f[1] * f[2])
        return 1;
    double magnitude = hypot(text_matrix[2], text_matrix[3]);
    if (!isfinite(magnitude) || magnitude > 32767)
        return 0;
    double ppem = fmax(1, floor(magnitude + .5));
    double unit = fabs(f[3] != 0 ? f[3] : f[1]);
    if (!unit)
        return 1;
    double scaled = floor(ppem * unit * 64 * 65536 + .5);
    if (!isfinite(scaled) || scaled < 1 || scaled > INT32_MAX - 32)
        return 0;
    int32_t scale = ((int32_t)scaled + 32) / 64;
    pdf_ps_gridfit_size size = {.x_scale = scale, .y_scale = scale};
    int32_t font_matrix[4], page_matrix[4], font_offset[2] = {0}, page_offset[2];
    for (unsigned i = 0; i < 4; i++)
        if (!ps_page_fixed(f[i] / unit, font_matrix + i) ||
            !ps_page_fixed(text_matrix[i] / ppem, page_matrix + i))
            return 0;
    for (unsigned i = 0; i < 2; i++)
        if (!ps_page_offset(text_matrix[i] * f[4] + text_matrix[i + 2] * f[5] + text_matrix[i + 4],
                            page_offset + i))
            return 0;
    size_t quota = *work_left < 4000000 ? *work_left : 4000000;
    *work_left -= quota;
    int ok = build(font, glyph, &size, output, &quota) &&
             cupidimage_pdf_ps_outline_transform(output, font_matrix, font_offset, &quota) &&
             cupidimage_pdf_ps_outline_transform(output, page_matrix, page_offset, &quota);
    if (ok && output->count > quota)
        ok = 0;
    if (ok) {
        quota -= output->count;
        unsigned used = 0;
        for (unsigned i = 0; i < output->count; i++) {
            pdf_cff_segment segment = output->segments[i];
            if (segment.kind == PDF_CFF_CLOSE && used &&
                output->segments[used - 1].kind == PDF_CFF_LINE &&
                output->segments[used - 1].points[0] == segment.points[0] &&
                output->segments[used - 1].points[1] == segment.points[1])
                used--;
            output->segments[used++] = segment;
        }
        output->count = used;
    }
    *work_left += quota;
    if (!ok) {
        cupidimage_pdf_type2_free(output);
        return 0;
    }
    *fitted = 1;
    return 1;
}

static int ps_page_cff(const void *font, unsigned glyph, const pdf_ps_gridfit_size *size,
                       pdf_cff_outline *output, size_t *work) {
    return cupidimage_pdf_cff_hint_outline(font, glyph, size, output, work);
}

static int ps_page_type1(const void *font, unsigned glyph, const pdf_ps_gridfit_size *size,
                         pdf_cff_outline *output, size_t *work) {
    return cupidimage_pdf_type1_hint_outline(font, glyph, size, output, work);
}

int cupidimage_pdf_cff_page_shape(const pdf_cff_font *font, unsigned glyph,
                                  const double text_matrix[6], size_t *work_left,
                                  pdf_cff_outline *output, int *fitted) {
    if (!output || !fitted)
        return 0;
    memset(output, 0, sizeof(*output));
    *fitted = 0;
    if (!font || glyph >= font->chars.count || !text_matrix || !work_left)
        return 0;
    unsigned fd = font->fdselect ? font->fdselect[glyph] : 0;
    if (fd >= font->private_count || !font->private_dicts)
        return 0;
    return ps_page_shape(font, glyph, font->private_dicts[fd].matrix, text_matrix, work_left,
                         output, fitted, ps_page_cff);
}

int cupidimage_pdf_type1_page_shape(const pdf_type1_font *font, unsigned glyph,
                                    const double text_matrix[6], size_t *work_left,
                                    pdf_cff_outline *output, int *fitted) {
    if (!output || !fitted)
        return 0;
    memset(output, 0, sizeof(*output));
    *fitted = 0;
    if (!font || glyph >= font->glyph_count || !text_matrix || !work_left)
        return 0;
    return ps_page_shape(font, glyph, font->matrix, text_matrix, work_left, output, fitted,
                         ps_page_type1);
}
