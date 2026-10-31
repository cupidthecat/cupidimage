#include "formats/pdf/fonts/truetype_vm/vm.h"
#include <assert.h>
#include <stdio.h>

static void instruction(pdf_tt_vm *vm, unsigned value, unsigned opcode, pdf_tt_vm_stage stage) {
    unsigned char code[] = {0xb8, (unsigned char)(value >> 8), (unsigned char)value,
                            (unsigned char)opcode};
    assert(cupidimage_pdf_tt_vm_run(vm, code, sizeof(code), stage, 16));
    assert(vm->stack_size == 0);
}

static int expected(unsigned flags, unsigned ppem, unsigned transform, int previous) {
    unsigned threshold = flags & 255;
    if (!threshold)
        return 0;
    if (threshold == 255)
        return 1;
    const int conditions[] = {ppem <= threshold, (transform & PDF_TT_VM_ROTATED) != 0,
                              (transform & PDF_TT_VM_STRETCHED) != 0};
    for (unsigned i = 0; i < 3; i++)
        if ((flags & (1u << (8 + i))) && conditions[i])
            previous = 1;
    for (unsigned i = 0; i < 3; i++)
        if ((flags & (1u << (11 + i))) && !conditions[i])
            previous = 0;
    return previous;
}

int main(void) {
    size_t checks = 0;
    for (unsigned transform = 0; transform < 4; transform++) {
        const unsigned sizes[] = {1, 16, 254, 255, 300};
        for (unsigned size = 0; size < sizeof(sizes) / sizeof(*sizes); size++) {
            pdf_tt_vm vm;
            assert(cupidimage_pdf_tt_vm_init(&vm, 16, 0, NULL, 0, 0, 0));
            assert(cupidimage_pdf_tt_vm_set_size_xy(&vm, 1000, sizes[size] * 64, sizes[size] * 64,
                                                    sizes[size] * 48, 0, transform, NULL));
            for (unsigned flags = 0; flags <= 65535; flags++) {
                for (int previous = 0; previous <= 1; previous++) {
                    vm.graphics.scan_control = previous;
                    instruction(&vm, flags, 0x85, PDF_TT_VM_GLYPH);
                    assert(vm.graphics.scan_control ==
                           expected(flags, sizes[size], transform, previous));
                    assert(vm.graphics.scan_flags == flags);
                    checks++;
                }
            }
            for (unsigned mode = 0; mode < 8; mode++) {
                instruction(&vm, 255, 0x85, PDF_TT_VM_PREP);
                instruction(&vm, mode, 0x8d, PDF_TT_VM_PREP);
                assert(cupidimage_pdf_tt_vm_save_preparation(&vm));
                assert(cupidimage_pdf_tt_vm_begin_glyph(&vm, NULL, 0, 0));
                assert(vm.graphics.scan_control && vm.graphics.scan_type == mode);
                instruction(&vm, 0, 0x85, PDF_TT_VM_GLYPH);
                assert(!vm.graphics.scan_control);
                assert(cupidimage_pdf_tt_vm_begin_glyph(&vm, NULL, 0, 0));
                assert(vm.graphics.scan_control && vm.graphics.scan_type == mode);
                checks += 3;
            }
            instruction(&vm, 255, 0x85, PDF_TT_VM_GLYPH);
            instruction(&vm, 16, 0x85, PDF_TT_VM_GLYPH);
            assert(vm.graphics.scan_control);
            instruction(&vm, 0x1000 | 16, 0x85, PDF_TT_VM_GLYPH);
            assert(vm.graphics.scan_control == !!(transform & PDF_TT_VM_ROTATED));
            checks += 2;
            cupidimage_pdf_tt_vm_free(&vm);
        }
    }
    printf("TrueType scan controls: %zu state/condition checks\n", checks);
    return 0;
}
