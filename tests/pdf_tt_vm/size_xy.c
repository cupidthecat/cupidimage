static void size_xy_tests(void) {
    for (unsigned i = 0; i < 20000; i++) {
        uint64_t x = (uint32_t)random_value() & 65535;
        uint64_t y = (uint32_t)random_value() & 65535;
        CHECK(vm_ratio_root(x * x + y * y) == (uint64_t)floor(hypot((double)x, (double)y) + .5));
    }
    CHECK(vm_ratio_root(UINT64_MAX) == (UINT64_C(1) << 32));
    for (unsigned op = 0x7e; op <= 0x7f; op++) {
        pdf_tt_vm vm;
        init(&vm, NULL, 0);
        unsigned char code[] = {0xb1, 42, 97, (unsigned char)op};
        CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
        CHECK(vm.stack_size == 1 && vm.stack[0] == 42);
        finish(&vm);
        error(code + 3, 1, PDF_TT_VM_STACK, 100);
    }
    const int32_t raw[] = {1000, -1000, 0};
    for (unsigned x = 1; x <= 40; x++)
        for (unsigned y = 1; y <= 40; y++) {
            pdf_tt_vm vm;
            init(&vm, raw, 3);
            CHECK(cupidimage_pdf_tt_vm_set_size_xy(&vm, 1000, x * 64, y * 64, 12 * 64, 0, 0, raw));
            unsigned largest = x > y ? x : y;
            CHECK(vm.cvt[0] == (int32_t)largest * 64 && vm.cvt[1] == -(int32_t)largest * 64);
            for (unsigned axis = 0; axis < 2; axis++) {
                unsigned size = axis ? y : x;
                unsigned char code[] = {(unsigned char)(1 - axis), 0x4b, 0x4c, 0xb0, 0, 0x45};
                CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), PDF_TT_VM_PREP, 100));
                CHECK(vm.stack_size == 3 && vm.stack[0] == (int32_t)size && vm.stack[1] == 768);
                CHECK(vm.stack[2] == (int32_t)size * 64);
                int32_t value = 0;
                CHECK(cupidimage_pdf_tt_vm_cvt_read(&vm, 1, &value) &&
                      value == -(int32_t)size * 64);
                CHECK(cupidimage_pdf_tt_vm_cvt_write(&vm, 2, 97));
                CHECK(cupidimage_pdf_tt_vm_cvt_read(&vm, 2, &value) && value == 97);
                CHECK(cupidimage_pdf_tt_vm_cvt_delta(&vm, 2, -8));
                int ratio = (int)round(size * 65536.0 / largest);
                int base = (int)round(97 * 65536.0 / ratio) + (int)round(-8 * 65536.0 / ratio);
                int expected = (int)round(base * (double)ratio / 65536);
                CHECK(cupidimage_pdf_tt_vm_cvt_read(&vm, 2, &value) && value == expected);
            }
            finish(&vm);
        }
    for (unsigned flags = 0; flags < 4; flags++) {
        pdf_tt_vm vm;
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_set_size_xy(&vm, 1000, 13 * 64, 37 * 64, 999, 1, flags, NULL));
        static const unsigned char info[] = {0xb0, 255, 0x88};
        CHECK(cupidimage_pdf_tt_vm_run(&vm, info, sizeof(info), PDF_TT_VM_PREP, 100));
        CHECK(vm.stack[0] == (int32_t)(4131 + ((flags & 1) ? 256 : 0) + ((flags & 2) ? 512 : 0)));
        static const unsigned char rotation_control[] = {0xb0, 2, 0x88, 0x58, 0xb1,
                                                         1,    1, 0x8e, 0x59};
        CHECK(cupidimage_pdf_tt_vm_run(&vm, rotation_control, sizeof(rotation_control),
                                       PDF_TT_VM_PREP, 100));
        CHECK(cupidimage_pdf_tt_vm_save_preparation(&vm));
        CHECK(cupidimage_pdf_tt_vm_begin_glyph(&vm, NULL, 0, 0));
        static const unsigned char glyph_code[] = {0xb0, 42};
        CHECK(cupidimage_pdf_tt_vm_run(&vm, glyph_code, sizeof(glyph_code), PDF_TT_VM_GLYPH, 100));
        CHECK(vm.stack_size == ((flags & 1) ? 0u : 1u));
        CHECK(vm.size.transform_flags == flags);
        CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 1000, 1024, 768, 0, NULL));
        CHECK(!vm.size.transform_flags && vm.size.axis_scale[0] == vm.size.axis_scale[1]);
        finish(&vm);
    }
    pdf_tt_vm vm;
    const int32_t zero = 0;
    init(&vm, &zero, 1);
    CHECK(cupidimage_pdf_tt_vm_set_size_xy(&vm, 1024, 12 * 64, 24 * 64, 12 * 64, 0, 0, &zero));
    pdf_tt_vm_point points[2] = {0};
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 2, 2));
    static const unsigned char delta[] = {0x01, 0xb2, 0x38, 1,    1,    0x5d, 0x00, 0xb2,
                                          0x38, 1,    1,    0x5d, 0xb2, 0xf8, 1,    1,
                                          0x5d, 0x01, 0xb2, 0x38, 0,    1,    0x73};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, delta, sizeof(delta), 0, 100));
    CHECK(vm.zones[1].points[1].current[0] == 8 && vm.zones[1].points[1].current[1] == 8);
    CHECK(vm.cvt[0] == 16);
    int32_t value = 0;
    CHECK(cupidimage_pdf_tt_vm_cvt_read(&vm, 0, &value) && value == 8);
    finish(&vm);
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 2, 2));
    const int32_t design[][2] = {{0, 0}, {3, 5}};
    CHECK(cupidimage_pdf_tt_vm_set_design_xy(&vm, design, 2, 49152, 131072));
    vm.graphics.dual_projection[0] = 9830;
    vm.graphics.dual_projection[1] = 13107;
    CHECK(cupidimage_pdf_tt_vm_design_distance(&vm, 1, 1, 1, 0, &value) && value == 9);
    CHECK(cupidimage_pdf_tt_vm_design_distance(&vm, 1, 0, 1, 1, &value) && value == -9);
    finish(&vm);
    const int32_t invalid[][3] = {{0, 64, 0}, {64, 0, 0}, {64, 64, 4}, {1, 32767 * 64, 0}};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        init(&vm, NULL, 0);
        CHECK(!cupidimage_pdf_tt_vm_set_size_xy(&vm, 1000, invalid[i][0], invalid[i][1], 64, 0,
                                                (unsigned)invalid[i][2], NULL));
        CHECK(vm.error == PDF_TT_VM_SIZE);
        finish(&vm);
    }
    init(&vm, &zero, 1);
    CHECK(cupidimage_pdf_tt_vm_set_size_xy(&vm, 1024, 64, 32767 * 64, 64, 0, 0, &zero));
    CHECK(!cupidimage_pdf_tt_vm_cvt_write(&vm, 0, INT32_MAX));
    CHECK(vm.error == PDF_TT_VM_ARITHMETIC && vm.cvt[0] == 0);
    finish(&vm);
}
