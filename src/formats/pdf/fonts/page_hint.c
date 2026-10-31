#include "page_hint.h"
#include "../pdf_internal.h"
#include "program.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct pdf_page_hint {
    pdf_font_program *program;
    pdf_tt_hint hint;
    int32_t pixels, points;
    int grayscale;
    unsigned transform_flags, profile;
};

void cupidimage_pdf_page_hint_free(pdf_page_hint **cache) {
    if (!cache || !*cache)
        return;
    pdf_page_hint *entry = *cache;
    cupidimage_pdf_tt_hint_free(&entry->hint);
    cupidimage_pdf_font_program_free(entry->program);
    free(entry);
    *cache = NULL;
}

static unsigned page_hint_u16(const unsigned char *p) {
    return (unsigned)p[0] * 256 + p[1];
}

static int page_hint_prepare_cost(const pdf_tt_font *font, size_t *work) {
    if (!font->maxp.data || font->maxp.length < 32 || font->cvt.length > 131070 ||
        font->cvt.length % 2)
        return 0;
    const unsigned char *m = font->maxp.data;
    unsigned instructions = page_hint_u16(m + 22);
    if (instructions > 256)
        instructions = 256;
    size_t cost = 256 + (size_t)page_hint_u16(m + 24) * sizeof(int32_t) +
                  (size_t)page_hint_u16(m + 18) * 2 * sizeof(int32_t) +
                  font->cvt.length * sizeof(int32_t) +
                  (size_t)page_hint_u16(m + 20) * sizeof(pdf_tt_vm_function) +
                  (size_t)instructions * sizeof(pdf_tt_vm_function) +
                  (size_t)page_hint_u16(m + 16) * sizeof(pdf_tt_vm_point);
    if (cost > *work)
        return 0;
    *work -= cost;
    return 1;
}

static size_t page_hint_quota(size_t *work) {
    size_t quota = *work < 1000000 ? *work : 1000000;
    *work -= quota;
    return quota;
}

static unsigned page_hint_transform_flags(const int32_t matrix[4]) {
    /* The device's downward Y axis is not a glyph rotation. Reflection alone
     * is distinct from rotation; unequal axis lengths or shear mean stretch. */
    int64_t a = matrix[0], b = -(int64_t)matrix[1], c = matrix[2], d = -(int64_t)matrix[3];
    unsigned flags = b || c || (a < 0 && d < 0) ? PDF_TT_VM_ROTATED : 0;
    uint64_t x_length = (uint64_t)(a * a) + (uint64_t)(b * b);
    uint64_t y_length = (uint64_t)(c * c) + (uint64_t)(d * d);
    if (x_length != y_length || a * c != -(b * d))
        flags |= PDF_TT_VM_STRETCHED;
    return flags;
}

int cupidimage_pdf_page_hint_shape(pdf_page_hint **cache, pdf_font_program *program, unsigned glyph,
                                   const double matrix[6], int grayscale, size_t *work_left,
                                   pdf_tt_hint_shape *shape) {
    return cupidimage_pdf_page_hint_shape_profile(cache, program, glyph, matrix, grayscale,
                                                  PDF_TT_PROFILE_LEGACY, work_left, shape);
}

int cupidimage_pdf_page_hint_shape_profile(pdf_page_hint **cache, pdf_font_program *program,
                                           unsigned glyph, const double matrix[6], int grayscale,
                                           unsigned profile, size_t *work_left,
                                           pdf_tt_hint_shape *shape) {
    if (!shape)
        return 0;
    memset(shape, 0, sizeof(*shape));
    if (!cache || !program || program->is_cff || program->is_type1 || !matrix || !work_left ||
        (grayscale != 0 && grayscale != 1) || profile > PDF_TT_PROFILE_MODERN)
        return 0;
    for (unsigned i = 0; i < 6; i++)
        if (!isfinite(matrix[i]))
            goto fail;
    double magnitude = hypot(matrix[2], matrix[3]) * program->font.units;
    if (!isfinite(magnitude) || magnitude > 32767)
        goto fail;
    int32_t ppem = (int32_t)floor(magnitude + .5);
    if (ppem < 1)
        ppem = 1;
    int32_t points = (int32_t)floor(magnitude * 48 + .5);
    if (points < 1)
        points = 1;
    int32_t transform[4], offset[2];
    for (unsigned i = 0; i < 4; i++) {
        double value = trunc(matrix[i] * program->font.units / ppem * 65536);
        if (!isfinite(value) || value < INT32_MIN || value > INT32_MAX)
            goto fail;
        transform[i] = (int32_t)value;
    }
    for (unsigned i = 0; i < 2; i++) {
        double value = round(matrix[i + 4] * 64);
        if (!isfinite(value) || value < INT32_MIN || value > INT32_MAX)
            goto fail;
        offset[i] = (int32_t)value;
    }
    unsigned transform_flags = grayscale ? 0 : page_hint_transform_flags(transform);
    if (*cache && ((*cache)->program != program || (*cache)->pixels != ppem * 64 ||
                   (*cache)->points != points || (*cache)->grayscale != grayscale ||
                   (*cache)->transform_flags != transform_flags || (*cache)->profile != profile))
        cupidimage_pdf_page_hint_free(cache);
    if (!*cache) {
        if (!page_hint_prepare_cost(&program->font, work_left))
            goto fail;
        pdf_page_hint *entry = calloc(1, sizeof(*entry));
        if (!entry)
            return 0;
        *cache = entry;
        if (!cupidimage_pdf_font_program_retain(program))
            goto fail;
        entry->program = program;
        entry->pixels = ppem * 64;
        entry->points = points;
        entry->grayscale = grayscale;
        entry->transform_flags = transform_flags;
        entry->profile = profile;
        size_t quota = page_hint_quota(work_left);
        if (!quota || !cupidimage_pdf_tt_hint_init_profile(
                          &entry->hint, &program->font, entry->pixels, entry->pixels, points,
                          grayscale, transform_flags, profile, quota))
            goto fail;
        *work_left += entry->hint.vm.work_left;
    }
    pdf_tt_hint *hint = &(*cache)->hint;
    size_t restore_cost =
        256 + ((size_t)hint->vm.storage_count + hint->vm.cvt_count) * sizeof(int32_t);
    if (restore_cost > *work_left)
        goto fail;
    *work_left -= restore_cost;
    size_t quota = page_hint_quota(work_left);
    if (!quota)
        goto fail;
    hint->glyph_budget = quota;
    if (!cupidimage_pdf_tt_hint_glyph(hint, glyph) ||
        !cupidimage_pdf_tt_hint_shape_build(hint, transform, offset, shape))
        goto fail;
    *work_left += hint->vm.work_left;
    return 1;
fail:
    cupidimage_pdf_page_hint_free(cache);
    return 0;
}
