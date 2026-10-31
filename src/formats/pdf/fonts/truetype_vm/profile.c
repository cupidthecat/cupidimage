#include "internal.h"

int cupidimage_pdf_tt_vm_set_profile(pdf_tt_vm *vm, unsigned profile, int full_hinting) {
    if (!vm)
        return 0;
    if (vm->error != PDF_TT_VM_OK)
        return 0;
    if (profile > PDF_TT_PROFILE_MODERN || (full_hinting != 0 && full_hinting != 1) ||
        vm->prepared || vm->size.configured)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
    vm->profile = profile;
    vm->full_hinting = (unsigned)full_hinting;
    vm->compatibility = 0;
    return 1;
}

void cupidimage_pdf_tt_vm_profile_begin(pdf_tt_vm *vm) {
    vm->compatibility = vm->stage == PDF_TT_VM_GLYPH && vm->profile == PDF_TT_PROFILE_MODERN &&
                                vm->size.grayscale && !vm->full_hinting &&
                                !(vm->graphics.instruction_control & 4)
                            ? 4
                            : 0;
}

unsigned cupidimage_pdf_tt_vm_profile_info(const pdf_tt_vm *vm, unsigned selector) {
    int modern = vm->profile == PDF_TT_PROFILE_MODERN;
    unsigned info = (selector & 1) ? (modern ? 40 : 35) : 0;
    if ((selector & 2) && (vm->size.transform_flags & PDF_TT_VM_ROTATED))
        info |= 1u << 8;
    if ((selector & 4) && (vm->size.transform_flags & PDF_TT_VM_STRETCHED))
        info |= 1u << 9;
    if ((selector & 32) && vm->size.grayscale && !modern)
        info |= 1u << 12;
    /* Font programs precede size/render-mode selection. The modern profile's
     * default display mode is grayscale, not an LCD output layout. */
    if (modern && (vm->stage == PDF_TT_VM_FONT || vm->size.grayscale)) {
        if (selector & 64)
            info |= 1u << 13;
        if (selector & 1024)
            info |= 1u << 17;
        if (selector & 2048)
            info |= 1u << 18;
        if (selector & 4096)
            info |= 1u << 19;
    }
    return info;
}
