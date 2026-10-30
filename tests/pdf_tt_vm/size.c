static void size_tests(void) {
    pdf_tt_vm vm;
    const int32_t raw[] = {1000, -1000, 1, -1, 500, -500};
    init(&vm, raw, 6);
    CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 1000, 16 * 64, 12 * 64, 1, raw));
    const int32_t scaled[] = {1024, -1024, 1, -1, 512, -512};
    CHECK(!memcmp(vm.cvt, scaled, sizeof(scaled)));
    CHECK(vm.size.ppem == 16 && vm.size.point_size == 768 && vm.size.grayscale);
    static const unsigned char measurements[] = {0x4b, 0x4c, 0xb0, 0xff, 0x88};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, measurements, sizeof(measurements), PDF_TT_VM_PREP, 100));
    CHECK(vm.stack_size == 3 && vm.stack[0] == 16 && vm.stack[1] == 768 && vm.stack[2] == 4131);
    static const unsigned char write_units[] = {0xb9, 0, 0, 0xff, 0x06, 0x70, 0xb8, 0, 125, 0x1f};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, write_units, sizeof(write_units), PDF_TT_VM_PREP, 100));
    CHECK(vm.cvt[0] == -256 && vm.graphics.single_width == 128);
    static const unsigned char controls[] = {0xb1, 2,    2,    0x8e, 0xb1, 4, 3,   0x8e,
                                             0xb8, 0x01, 0xff, 0x85, 0xb0, 5, 0x8d};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, controls, sizeof(controls), PDF_TT_VM_PREP, 100));
    CHECK(vm.graphics.instruction_control == 6 && vm.graphics.scan_flags == 511 &&
          vm.graphics.scan_type == 5);
    static const unsigned char inhibit[] = {0xb1, 1, 1, 0x8e};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, inhibit, sizeof(inhibit), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.graphics.instruction_control == 6);
    CHECK(cupidimage_pdf_tt_vm_run(&vm, inhibit, sizeof(inhibit), PDF_TT_VM_PREP, 100));
    CHECK(vm.graphics.instruction_control == 7);
    static const unsigned char invalid[] = {0xff};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, invalid, sizeof(invalid), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.stack_size == 0);
    CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 1000, 8 * 64, 6 * 64, 0, raw));
    CHECK(vm.graphics.instruction_control == 0 && vm.graphics.scan_flags == 0 &&
          vm.graphics.scan_type == 0);
    CHECK(vm.cvt[0] == 512 && vm.cvt[1] == -512);
    finish(&vm);
    for (unsigned shift = 0; shift <= 6; shift++)
        for (unsigned op = 0x73; op <= 0x75; op++)
            for (unsigned argument = 0; argument < 256; argument++) {
                const int32_t initial[] = {0};
                init(&vm, initial, 1);
                unsigned ppem = 9 + (op - 0x73) * 16 + (argument >> 4);
                CHECK(
                    cupidimage_pdf_tt_vm_set_size(&vm, 2048, (int32_t)ppem * 64, 768, 0, initial));
                unsigned char code[] = {0xb0, (unsigned char)shift,    0x5f,
                                        0xb2, (unsigned char)argument, 0,
                                        1,    (unsigned char)op};
                CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), PDF_TT_VM_PREP, 100));
                int step = (int)(argument & 15) - 8;
                if (step >= 0)
                    step++;
                CHECK(vm.cvt[0] == step * (64 >> shift) && vm.stack_size == 0);
                CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 2048, (int32_t)(ppem + 1) * 64, 768, 0,
                                                    initial));
                CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), PDF_TT_VM_PREP, 100));
                CHECK(vm.cvt[0] == 0 && vm.stack_size == 0);
                finish(&vm);
            }
    static const unsigned char invalid_size_ops[] = {0x4b, 0x4c, 0x73, 0x74, 0x75};
    for (unsigned i = 0; i < sizeof(invalid_size_ops); i++)
        error(invalid_size_ops + i, 1, PDF_TT_VM_SIZE, 100);
    static const unsigned char invalid_shift[] = {0xb0, 7, 0x5f};
    error(invalid_shift, sizeof(invalid_shift), PDF_TT_VM_PROGRAM, 100);
    for (unsigned op = 0x1f; op <= 0x8e; op++)
        if (op == 0x1f || op == 0x70 || op == 0x85 || op == 0x88 || op == 0x8d || op == 0x8e) {
            unsigned char code = (unsigned char)op;
            error(&code, 1, PDF_TT_VM_STACK, 100);
        }
    const int32_t invalid_config[][3] = {
        {0, 1024, 768},  {16385, 1024, 768},     {2048, 0, 768},       {2048, -1, 768},
        {2048, 1024, 0}, {2048, INT32_MAX, 768}, {16, 32767 * 64, 768}};
    for (unsigned i = 0; i < sizeof(invalid_config) / sizeof(*invalid_config); i++) {
        init(&vm, NULL, 0);
        CHECK(!cupidimage_pdf_tt_vm_set_size(&vm, (unsigned)invalid_config[i][0],
                                             invalid_config[i][1], invalid_config[i][2], 0, NULL));
        CHECK(vm.error == PDF_TT_VM_SIZE);
        finish(&vm);
    }
    init(&vm, raw, 6);
    CHECK(!cupidimage_pdf_tt_vm_set_size(&vm, 1000, 1024, 768, 0, NULL));
    CHECK(vm.error == PDF_TT_VM_SIZE);
    finish(&vm);
    const int32_t extreme[] = {INT32_MAX};
    init(&vm, extreme, 1);
    CHECK(!cupidimage_pdf_tt_vm_set_size(&vm, 16, 1024, 768, 0, extreme));
    CHECK(vm.error == PDF_TT_VM_ARITHMETIC);
    finish(&vm);
    const unsigned char bad_deltas[][5] = {
        {0xb0, 1, 0x73, 0x59, 0x59}, {0xb2, 0, 1, 1, 0x73}, {0xb2, 0x78, 0, 1, 0x73}};
    const pdf_tt_vm_error expected[] = {PDF_TT_VM_STACK, PDF_TT_VM_STORAGE, PDF_TT_VM_ARITHMETIC};
    for (unsigned i = 0; i < 3; i++) {
        init(&vm, extreme, 1);
        CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 1024, 1024, 768, 0, extreme));
        CHECK(!cupidimage_pdf_tt_vm_run(&vm, bad_deltas[i], 5, PDF_TT_VM_PREP, 100));
        CHECK(vm.error == expected[i]);
        finish(&vm);
    }
    init(&vm, raw, 6);
    CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 1000, 1024, 768, 0, raw));
    static const unsigned char delta_budget[] = {0xb2, 0x78, 0, 1, 0x73};
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, delta_budget, sizeof(delta_budget), PDF_TT_VM_PREP, 5));
    CHECK(vm.error == PDF_TT_VM_BUDGET);
    finish(&vm);

    static const unsigned char bad_controls[][4] = {{0xb1, 1, 0, 0x8e},
                                                    {0xb1, 0, 4, 0x8e},
                                                    {0xb1, 1, 2, 0x8e},
                                                    {0xb1, 2, 3, 0x8e},
                                                    {0xb0, 8, 0x8d, 0x59}};
    for (unsigned i = 0; i < sizeof(bad_controls) / sizeof(*bad_controls); i++)
        error(bad_controls[i], sizeof(bad_controls[i]), PDF_TT_VM_PROGRAM, 100);
    static const unsigned char no_size_width[] = {0xb0, 1, 0x1f};
    error(no_size_width, sizeof(no_size_width), PDF_TT_VM_SIZE, 100);
    init(&vm, NULL, 0);
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, NULL, 0, (pdf_tt_vm_stage)3, 100));
    CHECK(vm.error == PDF_TT_VM_PROGRAM);
    finish(&vm);
    init(&vm, raw, 6);
    CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 1000, 1024, 768, 0, raw));
    static const unsigned char zero_delta[] = {0xb0, 0, 0x73};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, zero_delta, sizeof(zero_delta), PDF_TT_VM_PREP, 3));
    CHECK(!vm.stack_size && !vm.work_left && vm.cvt[0] == 1024);
    static const unsigned char high_base[] = {0xb0, 0, 0x45, 0x5e, 0xb2, 0x78, 0, 1, 0x73};
    vm.cvt[0] = INT32_MAX;
    CHECK(cupidimage_pdf_tt_vm_run(&vm, high_base, sizeof(high_base), PDF_TT_VM_PREP, 100));
    CHECK(vm.cvt[0] == INT32_MAX && vm.graphics.delta_base == INT32_MAX);
    finish(&vm);
    for (unsigned length = 65536; length <= 65537; length++) {
        unsigned char *code = malloc((size_t)length + 4);
        CHECK(code != NULL);
        code[0] = 0xb0;
        code[1] = 0;
        code[2] = 0x2c;
        memset(code + 3, 0x59, length);
        code[length + 3] = 0x2d;
        init(&vm, NULL, 0);
        int ok = cupidimage_pdf_tt_vm_run(&vm, code, (size_t)length + 4, PDF_TT_VM_PREP, 100000);
        CHECK(ok == (length == 65536));
        CHECK(vm.error == (ok ? PDF_TT_VM_OK : PDF_TT_VM_FUNCTION));
        finish(&vm);
        free(code);
    }
}
