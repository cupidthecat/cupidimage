#include "internal.h"
#include <stdlib.h>
#include <string.h>

int cupidimage_pdf_tt_vm_save_preparation(pdf_tt_vm *vm) {
    if (vm->error != PDF_TT_VM_OK)
        return 0;
    if (!vm->size.configured)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_SIZE);
    if (vm->stage != PDF_TT_VM_PREP)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
    int32_t *storage = vm->prepared_storage, *cvt = vm->prepared_cvt;
    if (!storage && vm->storage_count)
        storage = malloc((size_t)vm->storage_count * sizeof(*storage));
    if (!cvt && vm->cvt_count)
        cvt = malloc((size_t)vm->cvt_count * sizeof(*cvt));
    if ((vm->storage_count && !storage) || (vm->cvt_count && !cvt)) {
        if (storage != vm->prepared_storage)
            free(storage);
        if (cvt != vm->prepared_cvt)
            free(cvt);
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_MEMORY);
    }
    vm->prepared_storage = storage;
    vm->prepared_cvt = cvt;
    if (vm->storage_count)
        memcpy(storage, vm->storage, (size_t)vm->storage_count * sizeof(*storage));
    if (vm->cvt_count)
        memcpy(cvt, vm->cvt, (size_t)vm->cvt_count * sizeof(*cvt));
    vm->prepared_graphics = vm->graphics;
    pdf_tt_vm_graphics *g = &vm->prepared_graphics;
    g->projection[0] = g->freedom[0] = g->dual_projection[0] = 16384;
    g->projection[1] = g->freedom[1] = g->dual_projection[1] = 0;
    g->reference[0] = g->reference[1] = g->reference[2] = 0;
    g->zone[0] = g->zone[1] = g->zone[2] = 1;
    g->loop = 1;
    vm->prepared = 1;
    return 1;
}

static int vm_begin_outline(pdf_tt_vm *vm, const pdf_tt_vm_point *points, unsigned count,
                            unsigned outline, int restore_private) {
    if (vm->error != PDF_TT_VM_OK)
        return 0;
    if (!vm->prepared)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
    if (!cupidimage_pdf_tt_vm_load_glyph_points(vm, points, count, outline))
        return 0;
    if (vm->prepared_graphics.instruction_control & 2) {
        cupidimage_pdf_tt_vm_graphics_defaults(&vm->graphics);
        /* Inhibition remains effective even when the other defaults are discarded. */
        vm->graphics.instruction_control = vm->prepared_graphics.instruction_control & 1;
    } else {
        vm->graphics = vm->prepared_graphics;
    }
    vm->graphics.rounding = (pdf_tt_vm_round){64, 0, 32, 0};
    if (restore_private && vm->storage_count)
        memcpy(vm->storage, vm->prepared_storage, (size_t)vm->storage_count * sizeof(*vm->storage));
    if (restore_private && vm->cvt_count)
        memcpy(vm->cvt, vm->prepared_cvt, (size_t)vm->cvt_count * sizeof(*vm->cvt));
    vm->stack_size = 0;
    vm->stage = PDF_TT_VM_GLYPH;
    return 1;
}

int cupidimage_pdf_tt_vm_begin_glyph(pdf_tt_vm *vm, const pdf_tt_vm_point *points, unsigned count,
                                     unsigned outline) {
    return vm_begin_outline(vm, points, count, outline, 1);
}

int cupidimage_pdf_tt_vm_begin_component(pdf_tt_vm *vm, const pdf_tt_vm_point *points,
                                         unsigned count, unsigned outline) {
    return vm_begin_outline(vm, points, count, outline, 0);
}
