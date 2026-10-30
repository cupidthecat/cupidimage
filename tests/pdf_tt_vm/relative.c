static int32_t relative_grid(int32_t value) {
    return value < 0 ? -((-value + 32) / 64 * 64) : (value + 32) / 64 * 64;
}

static void relative_tests(void) {
    pdf_tt_vm vm;
    const pdf_tt_vm_point points[] = {{{31, 35}, {31, 35}, 0, 1, 0},
                                      {{100, 0}, {100, 0}, 0, 1, 0},
                                      {{0, 100}, {0, 100}, 0, 1, 1}};
    const int32_t values[] = {-129, -65, -1, 0, 1, 31, 64, 97, 145, 257};
    const int32_t cutins[] = {0, 68, 76, 256}, minimums[] = {0, 16, 64, 128};
    for (unsigned op = 0xe0; op <= 0xff; op++) {
        if ((op & 3) == 3) {
            unsigned char code = (unsigned char)op;
            error(&code, 1, PDF_TT_VM_UNSUPPORTED, 100);
            continue;
        }
        for (unsigned i = 0; i < sizeof(values) / sizeof(*values); i++)
            for (unsigned j = 0; j < sizeof(cutins) / sizeof(*cutins); j++)
                for (unsigned k = 0; k < sizeof(minimums) / sizeof(*minimums); k++)
                    for (unsigned flip = 0; flip < 2; flip++)
                        for (unsigned direction = 0; direction < 2; direction++) {
                            init(&vm, values + i, 1);
                            CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
                            unsigned reference = direction ? 1 : 0, target = direction ? 0 : 1;
                            vm.graphics.reference[0] = reference;
                            vm.graphics.cvt_cut_in = cutins[j];
                            vm.graphics.minimum_distance = minimums[k];
                            vm.graphics.auto_flip = (int)flip;
                            vm.graphics.loop = 7;
                            unsigned char code[] = {0xb1, (unsigned char)target, 0,
                                                    (unsigned char)op};
                            CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), PDF_TT_VM_GLYPH,
                                                           100));
                            int32_t original = direction ? -69 : 69, distance = values[i];
                            if (flip && ((original < 0) != (distance < 0)))
                                distance = -distance;
                            if (op & 4) {
                                int32_t difference = distance - original;
                                if (difference < 0)
                                    difference = -difference;
                                if (difference > cutins[j])
                                    distance = original;
                                distance = relative_grid(distance);
                            }
                            if (op & 8) {
                                if (original >= 0 && distance < minimums[k])
                                    distance = minimums[k];
                                else if (original < 0 && distance > -minimums[k])
                                    distance = -minimums[k];
                            }
                            CHECK(vm.zones[1].points[target].current[0] ==
                                  points[reference].current[0] + distance);
                            CHECK(vm.zones[1].points[target].current[1] ==
                                  points[target].current[1]);
                            CHECK(vm.zones[1].points[target].original[0] ==
                                  points[target].original[0]);
                            CHECK(vm.zones[1].points[target].touched == 1 &&
                                  vm.cvt[0] == values[i]);
                            CHECK(vm.graphics.reference[0] == (op & 16 ? target : reference));
                            CHECK(vm.graphics.reference[1] == reference &&
                                  vm.graphics.reference[2] == target);
                            CHECK(vm.graphics.loop == 7 && !vm.stack_size);
                            finish(&vm);
                        }
    }
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
    static const unsigned char zero_cvt[] = {0xb9, 0, 1, 0xff, 0xff, 0xe0};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, zero_cvt, sizeof(zero_cvt), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.zones[1].points[1].current[0] == 31 && !vm.cvt);
    finish(&vm);
    for (unsigned op = 0x3a; op <= 0x3b; op++)
        for (unsigned i = 0; i < sizeof(values) / sizeof(*values); i++) {
            init(&vm, values + i, 1);
            CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
            vm.graphics.minimum_distance = 1024;
            static const unsigned char set_half[] = {0x19};
            CHECK(cupidimage_pdf_tt_vm_run(&vm, set_half, 1, PDF_TT_VM_GLYPH, 100));
            unsigned char code[] = {0xb1, 1, 0, 0x45, (unsigned char)op};
            CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), PDF_TT_VM_GLYPH, 100));
            CHECK(vm.zones[1].points[1].current[0] == 31 + values[i]);
            CHECK(vm.zones[1].points[1].original[0] == 100);
            CHECK(vm.graphics.reference[0] == (op & 1 ? 1u : 0u));
            CHECK(vm.graphics.reference[1] == 0 && vm.graphics.reference[2] == 1);
            finish(&vm);
        }
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 2, points, 3, 3));
    static const unsigned char twilight[] = {0xb1, 0, 97, 0x48, 0xb0, 0, 0x14, 0xb1, 1, 64, 0x3a};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, twilight, sizeof(twilight), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.zones[0].points[1].original[0] == 95 && vm.zones[0].points[1].current[0] == 161);
    CHECK(vm.zones[0].points[1].original[1] == 35 && vm.zones[0].points[1].current[1] == 35);
    finish(&vm);
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
    static const unsigned char align[] = {0xb0, 1, 0x10, 0xb0, 3, 0x17, 0xb2, 0, 1, 2, 0x3c};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, align, sizeof(align), PDF_TT_VM_GLYPH, 100));
    for (unsigned i = 0; i < 3; i++) {
        CHECK(vm.zones[1].points[i].current[0] == 100);
        CHECK(vm.zones[1].points[i].original[0] == points[i].original[0]);
        CHECK(vm.zones[1].points[i].touched == 1);
    }
    CHECK(vm.graphics.loop == 1 && vm.graphics.reference[0] == 1 && !vm.stack_size);
    finish(&vm);
    static const unsigned char underflow[] = {0xe0, 0x3a, 0x3b, 0x3c};
    for (unsigned i = 0; i < sizeof(underflow); i++)
        error(underflow + i, 1, PDF_TT_VM_STACK, 100);
    init(&vm, values, 1);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
    static const unsigned char bad_cvt[] = {0xb9, 0, 1, 0xff, 0xfe, 0xe0};
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, bad_cvt, sizeof(bad_cvt), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.error == PDF_TT_VM_STORAGE);
    finish(&vm);
    const int32_t small[] = {31};
    init(&vm, small, 1);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 2, points, 3, 3));
    static const unsigned char mirp_twilight[] = {0xb0, 0, 0x16, 0xb1, 0, 31, 0x48,
                                                  0xb0, 0, 0x2f, 0xb1, 1, 0,  0xec};
    CHECK(
        cupidimage_pdf_tt_vm_run(&vm, mirp_twilight, sizeof(mirp_twilight), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.zones[0].points[1].original[0] == 62 && vm.zones[0].points[1].current[0] == 64);
    CHECK(vm.zones[0].points[1].touched == 1 && vm.graphics.reference[2] == 1);
    finish(&vm);
    const int32_t minimum[] = {INT32_MIN};
    init(&vm, minimum, 1);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
    static const unsigned char overflowing_flip[] = {0xb1, 1, 0, 0xe0};
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, overflowing_flip, sizeof(overflowing_flip),
                                    PDF_TT_VM_GLYPH, 100));
    CHECK(vm.error == PDF_TT_VM_ARITHMETIC && vm.zones[1].points[1].current[0] == 100);
    finish(&vm);
    init(&vm, values, 1);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
    vm.graphics.reference[0] = 3;
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, overflowing_flip, sizeof(overflowing_flip),
                                    PDF_TT_VM_GLYPH, 100));
    CHECK(vm.error == PDF_TT_VM_POINT);
    finish(&vm);
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
    static const unsigned char align_budget[] = {0xb0, 3, 0x17, 0xb2, 0, 1, 2, 0x3c};
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, align_budget, sizeof(align_budget), PDF_TT_VM_GLYPH, 10));
    CHECK(vm.error == PDF_TT_VM_BUDGET && !vm.zones[1].points[0].touched);
    finish(&vm);
    init(&vm, NULL, 0);
    static const unsigned char align_zero[] = {0xb0, 0, 0x17, 0x3c};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, align_zero, sizeof(align_zero), PDF_TT_VM_GLYPH, 4));
    CHECK(vm.graphics.loop == 1 && !vm.stack_size && !vm.work_left);
    finish(&vm);
}
