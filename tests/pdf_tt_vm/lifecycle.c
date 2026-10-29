static void lifecycle_tests(void) {
    pdf_tt_vm vm;
    const int32_t raw[] = {64, 128};
    const pdf_tt_vm_point points[] = {{{31, 35}, {31, 35}, 0, 1, 1}};
    static const unsigned char definition[] = {0xb0, 1, 0x2c, 0xb0, 42, 0x2d};
    init(&vm, raw, 2);
    CHECK(cupidimage_pdf_tt_vm_run(&vm, definition, sizeof(definition), PDF_TT_VM_FONT, 100));
    CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 64, 64, 64, 0, raw));
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 2, points, 1, 1));
    static const unsigned char prep[] = {
        0xb1, 0,    255, 0x48, 0xb0, 0, 0x46, 0xb1, 0,    33,   0x42, 0xb1, 0,    96,
        0x44, 0xb0, 0,   0x16, 0xb1, 0, 77,   0x48, 0x00, 0xb0, 1,    0x10, 0xb0, 1,
        0x11, 0xb0, 1,   0x12, 0xb0, 9, 0x17, 0xb0, 23,   0x1d, 0xb0, 12,   0x5e, 0x19};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, prep, sizeof(prep), PDF_TT_VM_PREP, 100));
    CHECK(vm.stack_size == 1 && vm.stack[0] == 0);
    CHECK(vm.zones[1].points[0].current[0] == 31 && vm.zones[1].points[0].touched == 0);
    CHECK(vm.graphics.projection[1] == 16384 && vm.graphics.loop == 9);
    CHECK(cupidimage_pdf_tt_vm_save_preparation(&vm));
    unsigned saved_allocations = allocations;
    CHECK(cupidimage_pdf_tt_vm_save_preparation(&vm));
    CHECK(allocations == saved_allocations);
    CHECK(cupidimage_pdf_tt_vm_begin_glyph(&vm, points, 1, 1));
    CHECK(vm.stack_size == 0 && vm.graphics.projection[0] == 16384 &&
          vm.graphics.projection[1] == 0);
    CHECK(vm.graphics.freedom[0] == 16384 && vm.graphics.dual_projection[0] == 16384);
    CHECK(vm.graphics.loop == 1 && vm.graphics.zone[0] == 1 && vm.graphics.zone[1] == 1 &&
          vm.graphics.zone[2] == 1);
    CHECK(!vm.graphics.reference[0] && !vm.graphics.reference[1] && !vm.graphics.reference[2]);
    CHECK(vm.graphics.cvt_cut_in == 23 && vm.graphics.delta_base == 12 &&
          vm.graphics.rounding.phase == 0);
    CHECK(vm.cvt[0] == 96 && vm.storage[0] == 33);
    CHECK(vm.zones[0].points[0].current[0] == 77);
    static const unsigned char glyph[] = {0xb1, 0,    17, 0x44, 0xb1, 0, 19,  0x42, 0xb0, 9, 0x1a,
                                          0x7a, 0xb0, 0,  0x16, 0xb1, 0, 101, 0x48, 0xb0, 1, 0x2b};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, glyph, sizeof(glyph), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.stack_size == 1 && vm.stack[0] == 42);
    CHECK(vm.cvt[0] == 17 && vm.storage[0] == 19 && vm.graphics.minimum_distance == 9);
    CHECK(vm.graphics.rounding.off && vm.zones[0].points[0].current[0] == 101);
    CHECK(cupidimage_pdf_tt_vm_begin_glyph(&vm, points, 1, 1));
    CHECK(vm.cvt[0] == 96 && vm.storage[0] == 33 && vm.graphics.minimum_distance == 64);
    CHECK(!vm.graphics.rounding.off && vm.graphics.rounding.phase == 0);
    CHECK(vm.zones[0].points[0].current[0] == 101 && vm.zones[0].points[0].original[0] == 101);
    CHECK(!vm.stack_size && vm.zones[1].points[0].current[0] == 31);
    CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 64, 128, 128, 1, raw));
    CHECK(vm.cvt[0] == 128 && vm.storage[0] == 0 && !vm.prepared);
    CHECK(!vm.zones[1].points && !vm.zones[1].count);
    CHECK(!vm.zones[0].points[0].current[0] && !vm.zones[0].points[0].original[0] &&
          !vm.zones[0].points[0].touched);
    CHECK(vm.functions[1].defined && vm.stage == PDF_TT_VM_FONT);
    CHECK(!cupidimage_pdf_tt_vm_begin_glyph(&vm, points, 1, 1));
    CHECK(vm.error == PDF_TT_VM_PROGRAM);
    finish(&vm);
    for (unsigned flags = 0; flags < 4; flags++) {
        init(&vm, raw, 2);
        CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 64, 64, 64, 0, raw));
        unsigned char code[] = {0x19, 0xb0, 9,    0x1a,      0xb1, flags & 1,
                                1,    0x8e, 0xb1, flags & 2, 2,    0x8e};
        CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), PDF_TT_VM_PREP, 100));
        CHECK(cupidimage_pdf_tt_vm_save_preparation(&vm));
        CHECK(cupidimage_pdf_tt_vm_begin_glyph(&vm, points, 1, 1));
        CHECK(vm.graphics.minimum_distance == (flags & 2 ? 64 : 9));
        static const unsigned char round[] = {0xb0, 0, 0x68};
        CHECK(cupidimage_pdf_tt_vm_run(&vm, round, sizeof(round), PDF_TT_VM_GLYPH, 100));
        if (flags & 1)
            CHECK(vm.stack_size == 0);
        else
            CHECK(vm.stack_size == 1 && vm.stack[0] == 0);
        finish(&vm);
    }
    for (unsigned failure = 1; failure <= 2; failure++) {
        init(&vm, raw, 2);
        CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 64, 64, 64, 0, raw));
        CHECK(cupidimage_pdf_tt_vm_run(&vm, NULL, 0, PDF_TT_VM_PREP, 100));
        fail_at = allocations + failure;
        CHECK(!cupidimage_pdf_tt_vm_save_preparation(&vm));
        CHECK(vm.error == PDF_TT_VM_MEMORY && !vm.prepared && !vm.prepared_storage &&
              !vm.prepared_cvt);
        fail_at = 0;
        finish(&vm);
    }
    init(&vm, raw, 2);
    CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 64, 64, 64, 0, raw));
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 2, points, 1, 1));
    CHECK(cupidimage_pdf_tt_vm_run(&vm, NULL, 0, PDF_TT_VM_PREP, 100));
    CHECK(cupidimage_pdf_tt_vm_save_preparation(&vm));
    pdf_tt_vm_point *old = vm.zones[1].points;
    fail_at = allocations + 1;
    CHECK(!cupidimage_pdf_tt_vm_begin_glyph(&vm, points, 1, 1));
    CHECK(vm.error == PDF_TT_VM_MEMORY && vm.zones[1].points == old);
    fail_at = 0;
    finish(&vm);
    init(&vm, NULL, 0);
    CHECK(!cupidimage_pdf_tt_vm_save_preparation(&vm));
    CHECK(vm.error == PDF_TT_VM_SIZE);
    finish(&vm);
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 64, 64, 64, 0, NULL));
    CHECK(!cupidimage_pdf_tt_vm_save_preparation(&vm));
    CHECK(vm.error == PDF_TT_VM_PROGRAM);
    finish(&vm);
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 64, 64, 64, 0, NULL));
    CHECK(cupidimage_pdf_tt_vm_run(&vm, NULL, 0, PDF_TT_VM_PREP, 100));
    CHECK(cupidimage_pdf_tt_vm_save_preparation(&vm));
    CHECK(cupidimage_pdf_tt_vm_run(&vm, NULL, 0, PDF_TT_VM_PREP, 100));
    CHECK(!vm.prepared);
    CHECK(cupidimage_pdf_tt_vm_save_preparation(&vm));
    CHECK(cupidimage_pdf_tt_vm_run(&vm, NULL, 0, PDF_TT_VM_FONT, 100));
    CHECK(!vm.prepared);
    finish(&vm);
    CHECK(cupidimage_pdf_tt_vm_init(&vm, 64, 0, NULL, 0, 0, 0));
    CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 64, 64, 64, 0, NULL));
    CHECK(cupidimage_pdf_tt_vm_run(&vm, NULL, 0, PDF_TT_VM_PREP, 100));
    unsigned before = allocations;
    CHECK(cupidimage_pdf_tt_vm_save_preparation(&vm));
    CHECK(cupidimage_pdf_tt_vm_begin_glyph(&vm, NULL, 0, 0));
    CHECK(allocations == before && !vm.prepared_cvt && !vm.prepared_storage);
    finish(&vm);
    const int32_t component_cvt[] = {64};
    init(&vm, component_cvt, 1);
    CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 64, 64, 64, 0, component_cvt));
    CHECK(cupidimage_pdf_tt_vm_run(&vm, NULL, 0, PDF_TT_VM_PREP, 100));
    CHECK(cupidimage_pdf_tt_vm_save_preparation(&vm));
    CHECK(cupidimage_pdf_tt_vm_begin_glyph(&vm, NULL, 0, 0));
    vm.cvt[0] = 97;
    vm.storage[0] = 145;
    vm.graphics.minimum_distance = 192;
    vm.graphics.loop = 7;
    CHECK(cupidimage_pdf_tt_vm_begin_component(&vm, NULL, 0, 0));
    CHECK(vm.cvt[0] == 97 && vm.storage[0] == 145);
    CHECK(vm.graphics.minimum_distance == 64 && vm.graphics.loop == 1);
    CHECK(vm.prepared_cvt[0] == 64 && vm.prepared_storage[0] == 0);
    CHECK(cupidimage_pdf_tt_vm_begin_glyph(&vm, NULL, 0, 0));
    CHECK(vm.cvt[0] == 64 && vm.storage[0] == 0);
    finish(&vm);
}
