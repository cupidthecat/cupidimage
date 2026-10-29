#include "internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int tt_hint_count(pdf_tt_hint *hint, unsigned glyph, unsigned chain[16], unsigned depth,
                         unsigned *calls, size_t *work, unsigned *count) {
    if (depth >= 16 || !*calls)
        return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_BUDGET);
    (*calls)--;
    for (unsigned i = 0; i < depth; i++)
        if (chain[i] == glyph)
            return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_PROGRAM);
    chain[depth] = glyph;
    pdf_tt_table view;
    if (!cupidimage_pdf_tt_glyph_data(&hint->font, glyph, &view) ||
        (view.length && view.length < 10))
        return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_PROGRAM);
    if (!cupidimage_pdf_tt_hint_take(hint, work, 1) ||
        !cupidimage_pdf_tt_hint_take(hint, work, view.length))
        return 0;
    *count = 0;
    if (!view.length)
        return 1;
    int32_t contours = tt_hint_i16(view.data);
    if (contours >= 0) {
        if ((unsigned)contours > (view.length - 10) / 2)
            return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_PROGRAM);
        if (contours)
            *count = tt_hint_u16(view.data + 10 + ((unsigned)contours - 1) * 2) + 1;
        return 1;
    }
    size_t at = 10;
    pdf_tt_component record;
    int instructions = 0;
    do {
        unsigned child;
        if (!cupidimage_pdf_tt_component_read(&view, &at, &record))
            return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_PROGRAM);
        instructions |= record.flags & 256;
        if (!tt_hint_count(hint, record.glyph, chain, depth + 1, calls, work, &child))
            return 0;
        if (child > 65536 - *count)
            return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_POINT);
        *count += child;
    } while (record.flags & 32);
    pdf_tt_table program;
    if (!cupidimage_pdf_tt_component_program(&view, at, instructions, &program))
        return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_PROGRAM);
    return 1;
}

static int tt_hint_transform(pdf_tt_hint *hint, const int32_t matrix[4], const int32_t input[2],
                             int32_t output[2]) {
    int32_t result[2];
    for (unsigned axis = 0; axis < 2; axis++) {
        /* Component transforms quantize each product to the device grid before
         * adding it; this is distinct from vector projection's combined dot. */
        int64_t value = cupidimage_pdf_tt_vm_divide((int64_t)input[0] * matrix[axis], 16384) +
                        cupidimage_pdf_tt_vm_divide((int64_t)input[1] * matrix[axis + 2], 16384);
        if (value < INT32_MIN || value > INT32_MAX)
            return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_ARITHMETIC);
        result[axis] = (int32_t)value;
    }
    memcpy(output, result, sizeof(result));
    return 1;
}

static int tt_hint_offset(pdf_tt_hint *hint, const pdf_tt_component *record, int32_t result[2]) {
    for (unsigned axis = 0; axis < 2; axis++) {
        int64_t value;
        if (record->flags & 0x800) {
            int64_t transformed = (int64_t)record->argument[0] * record->matrix[axis] +
                                  (int64_t)record->argument[1] * record->matrix[axis + 2];
            value = cupidimage_pdf_tt_vm_divide(transformed * hint->vm.size.axis_scale[axis],
                                                INT64_C(1073741824));
        } else {
            value = cupidimage_pdf_tt_vm_divide(
                (int64_t)record->argument[axis] * hint->vm.size.axis_scale[axis], 65536);
        }
        if (value < INT32_MIN || value > INT32_MAX)
            return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_ARITHMETIC);
        result[axis] = (int32_t)value;
        if ((record->flags & 4) && !(hint->vm.prepared_graphics.instruction_control & 1) &&
            !cupidimage_pdf_tt_hint_round(hint, result[axis], &result[axis]))
            return 0;
    }
    return 1;
}

int cupidimage_pdf_tt_hint_composite(pdf_tt_hint *hint, unsigned glyph, const pdf_tt_table *view,
                                     unsigned chain[16], unsigned depth, unsigned *calls,
                                     size_t *work) {
    unsigned count_chain[16], count_calls = 4096, total;
    memcpy(count_chain, chain, depth * sizeof(*chain));
    if (!tt_hint_count(hint, glyph, count_chain, depth, &count_calls, work, &total) ||
        !cupidimage_pdf_tt_hint_take(hint, work, total + 4))
        return 0;
    pdf_tt_vm_point *points = calloc((size_t)total + 4, sizeof(*points));
    if (!points)
        return cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_MEMORY);
    int32_t design_phantoms[4][2];
    int ok = cupidimage_pdf_tt_hint_phantoms(hint, glyph, view, design_phantoms);
    for (unsigned i = 0; ok && i < 4; i++)
        for (unsigned axis = 0; ok && axis < 2; axis++)
            ok = cupidimage_pdf_tt_hint_scale(hint, axis, design_phantoms[i][axis],
                                              &points[total + i].current[axis]);
    size_t at = 10;
    unsigned used = 0, ordinal = 0;
    int instructions = 0;
    pdf_tt_component record = {0};
    while (ok) {
        if (!cupidimage_pdf_tt_component_read(view, &at, &record) ||
            (!ordinal && !(record.flags & 2))) {
            ok = cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_PROGRAM);
            break;
        }
        ordinal++;
        instructions |= record.flags & 256;
        if (!cupidimage_pdf_tt_hint_load(hint, record.glyph, chain, depth + 1, calls, work)) {
            ok = 0;
            break;
        }
        pdf_tt_vm_zone *child = &hint->vm.zones[1];
        unsigned child_count = child->outline_count;
        if (child_count > total - used) {
            ok = cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_POINT);
            break;
        }
        if (record.flags & 512)
            for (unsigned i = 0; i < 4; i++)
                points[total + i] = child->points[child_count + i];
        for (unsigned i = 0; ok && i < child_count; i++) {
            points[used + i] = child->points[i];
            ok = tt_hint_transform(hint, record.matrix, child->points[i].current,
                                   points[used + i].current);
        }
        int32_t offset[2] = {0};
        if (ok && (record.flags & 2))
            ok = tt_hint_offset(hint, &record, offset);
        else if (ok) {
            unsigned parent_index = (unsigned)record.argument[0],
                     child_index = (unsigned)record.argument[1];
            if ((parent_index >= used && (parent_index < total || parent_index >= total + 4)) ||
                child_index >= child_count + 4) {
                ok = cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_POINT);
            } else {
                int32_t anchor[2];
                ok = tt_hint_transform(hint, record.matrix, child->points[child_index].current,
                                       anchor);
                for (unsigned axis = 0; ok && axis < 2; axis++) {
                    int64_t delta = (int64_t)points[parent_index].current[axis] - anchor[axis];
                    if (delta < INT32_MIN || delta > INT32_MAX)
                        ok = cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_ARITHMETIC);
                    else
                        offset[axis] = (int32_t)delta;
                }
            }
        }
        for (unsigned i = 0; ok && i < child_count; i++)
            for (unsigned axis = 0; ok && axis < 2; axis++) {
                int64_t value = (int64_t)points[used + i].current[axis] + offset[axis];
                if (value < INT32_MIN || value > INT32_MAX)
                    ok = cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_ARITHMETIC);
                else
                    points[used + i].current[axis] = (int32_t)value;
            }
        used += child_count;
        if (!(record.flags & 32))
            break;
    }
    pdf_tt_table program = {0};
    if (ok &&
        (used != total || !cupidimage_pdf_tt_component_program(view, at, instructions, &program)))
        ok = cupidimage_pdf_tt_vm_fail(&hint->vm, PDF_TT_VM_PROGRAM);
    for (unsigned i = 0; ok && i < total + 4; i++) {
        memcpy(points[i].original, points[i].current, sizeof(points[i].original));
        points[i].touched = 0;
        if (i >= total && program.length && !(hint->vm.prepared_graphics.instruction_control & 1)) {
            unsigned axis = i - total < 2 ? 0 : 1;
            ok = cupidimage_pdf_tt_hint_round(hint, points[i].current[axis],
                                              &points[i].current[axis]);
        }
    }
    if (ok)
        ok = cupidimage_pdf_tt_vm_begin_component(&hint->vm, points, total + 4, total) &&
             cupidimage_pdf_tt_vm_run(&hint->vm, program.data, program.length, PDF_TT_VM_GLYPH,
                                      *work);
    if (ok)
        *work = hint->vm.work_left;
    free(points);
    return ok;
}
