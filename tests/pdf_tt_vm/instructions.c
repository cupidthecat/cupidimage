static void instruction_definition_tests(void) {
    pdf_tt_vm vm;
    static const unsigned char definition[] = {0xb0, 0x90, 0x89, 0xb0, 42, 0x2d};
    static const unsigned char invoke[] = {0x90};
    for (unsigned stage = 1; stage <= 2; stage++) {
        CHECK(cupidimage_pdf_tt_vm_init(&vm, 32, 0, NULL, 0, 2, 1));
        CHECK(cupidimage_pdf_tt_vm_run(&vm, definition, sizeof(definition), stage, 100));
        CHECK(vm.instruction_count == 1);
        CHECK(cupidimage_pdf_tt_vm_run(&vm, invoke, sizeof(invoke), 0, 100));
        CHECK(vm.stack_size == 1 && vm.stack[0] == 42);
        static const unsigned char replacement[] = {0xb0, 0x90, 0x89, 0xb0, 17, 0x2d};
        CHECK(cupidimage_pdf_tt_vm_run(&vm, replacement, sizeof(replacement), stage, 100));
        CHECK(vm.instruction_count == 1);
        CHECK(cupidimage_pdf_tt_vm_run(&vm, invoke, sizeof(invoke), 0, 100));
        CHECK(vm.stack_size == 1 && vm.stack[0] == 17);
        finish(&vm);
    }
    for (unsigned opcode = 0; opcode < 256; opcode++) {
        CHECK(cupidimage_pdf_tt_vm_init(&vm, 32, 0, NULL, 0, 0, 1));
        unsigned char code[] = {0xb0, (unsigned char)opcode, 0x89, 0xb0, 0x2d, 0x2d};
        CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 1, 100));
        CHECK(vm.instructions[opcode].defined && vm.instructions[opcode].length == 2);
        if (opcode == 0x28 || opcode == 0x7b || opcode == 0x83 || opcode == 0x84 ||
            (opcode >= 0x8f && opcode <= 0xaf)) {
            CHECK(cupidimage_pdf_tt_vm_run(&vm, code + 1, 1, 0, 100));
            CHECK(vm.stack_size == 1 && vm.stack[0] == 0x2d);
        }
        finish(&vm);
    }
    /* Existing opcodes retain their standard semantics. */
    CHECK(cupidimage_pdf_tt_vm_init(&vm, 32, 0, NULL, 0, 0, 1));
    static const unsigned char existing[] = {0xb0, 0x60, 0x89, 0x22, 0x2d};
    static const unsigned char add[] = {0xb1, 2, 3, 0x60};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, existing, sizeof(existing), 1, 100));
    CHECK(cupidimage_pdf_tt_vm_run(&vm, add, sizeof(add), 0, 100));
    CHECK(vm.stack_size == 1 && vm.stack[0] == 5);
    finish(&vm);
    for (unsigned kind = 0; kind < 8; kind++) {
        CHECK(cupidimage_pdf_tt_vm_init(&vm, 32, 0, NULL, 0, 2, kind == 0 ? 0 : 1));
        unsigned char code[] = {0xb0, 0x90, 0x89, 0x90, 0x2d};
        if (kind == 2)
            code[3] = 0x89;
        if (kind == 3)
            code[3] = 0x2c;
        if (kind <= 3) {
            CHECK(!cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), kind == 1 ? 0 : 1, 100));
            CHECK(vm.error == PDF_TT_VM_FUNCTION);
        } else if (kind == 4) {
            CHECK(!cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code) - 1, 1, 100));
            CHECK(vm.error == PDF_TT_VM_PROGRAM);
        } else {
            CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 1, 100));
            if (kind == 5) {
                code[1] = 0x91;
                CHECK(!cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 1, 100));
                CHECK(vm.error == PDF_TT_VM_FUNCTION);
            } else {
                CHECK(!cupidimage_pdf_tt_vm_run(&vm, invoke, sizeof(invoke), 0,
                                                kind == 6 ? 5 : 1000));
                CHECK(vm.error == (kind == 6 ? PDF_TT_VM_BUDGET : PDF_TT_VM_FUNCTION));
            }
        }
        finish(&vm);
    }
    for (unsigned failure = 1; failure <= 5; failure++) {
        int32_t cvt = 0;
        allocations = 0;
        fail_at = failure;
        CHECK(!cupidimage_pdf_tt_vm_init(&vm, 8, 8, &cvt, 1, 8, 1));
        CHECK(vm.error == PDF_TT_VM_MEMORY && live_bytes == 0);
        cupidimage_pdf_tt_vm_free(&vm);
    }
    fail_at = 0;
}
