static void movement_tests(void) {
    pdf_tt_vm vm;
    const pdf_tt_vm_point points[] = {{{0, 0}, {0, 0}, 0, 1, 0},
                                      {{31, 19}, {-91, 19}, 0, 1, 0},
                                      {{100, 100}, {100, 100}, 0, 1, 1}};
    const int32_t targets[] = {-257, -38, -37, -1, 0, 31, 32, 63, 64, 99, 100, 257};
    const int32_t cutins[] = {-1, 0, 16, 68, INT32_MAX};
    for (unsigned rounded = 0; rounded < 2; rounded++)
        for (unsigned i = 0; i < sizeof(targets) / sizeof(*targets); i++)
            for (unsigned j = 0; j < sizeof(cutins) / sizeof(*cutins); j++) {
                init(&vm, targets + i, 1);
                CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 1, points, 3, 3));
                vm.graphics.cvt_cut_in = cutins[j];
                unsigned char code[] = {0xb1, 1, 0, (unsigned char)(0x3e + rounded)};
                CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), PDF_TT_VM_GLYPH, 100));
                int32_t expected = targets[i];
                if (rounded) {
                    int64_t difference = (int64_t)expected - 31;
                    if (difference < 0)
                        difference = -difference;
                    if (difference > cutins[j])
                        expected = 31;
                    expected =
                        expected < 0 ? -((-expected + 32) / 64 * 64) : (expected + 32) / 64 * 64;
                }
                CHECK(vm.zones[1].points[1].current[0] == expected);
                CHECK(vm.zones[1].points[1].original[0] == -91 &&
                      vm.zones[1].points[1].current[1] == 19);
                CHECK(vm.graphics.reference[0] == 1 && vm.graphics.reference[1] == 1);
                CHECK(vm.zones[1].points[1].touched == 1 && !vm.stack_size);
                finish(&vm);
            }
    const int32_t value[] = {-31};
    init(&vm, value, 1);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 1, points, 3, 3));
    static const unsigned char twilight[] = {0xb0, 0,   0x16, 0xb1, 0,    100, 0x48, 0x00, 0xb1,
                                             0,    200, 0x48, 0x01, 0xb1, 0,   0,    0x3f};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, twilight, sizeof(twilight), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.zones[0].points[0].original[0] == -31 && vm.zones[0].points[0].original[1] == 0);
    CHECK(vm.zones[0].points[0].current[0] == 0 && vm.zones[0].points[0].current[1] == 0);
    CHECK(vm.zones[0].points[0].touched == 3);
    finish(&vm);
    for (unsigned band = 0; band < 3; band++)
        for (unsigned shift = 0; shift <= 6; shift++)
            for (unsigned argument = 0; argument < 256; argument++) {
                const unsigned ops[] = {0x5d, 0x71, 0x72};
                init(&vm, NULL, 0);
                unsigned size = 9 + band * 16 + (argument >> 4);
                CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 2048, (int32_t)size * 64, 768, 0, NULL));
                CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
                unsigned char code[] = {0xb0,
                                        9,
                                        0x17,
                                        0xb0,
                                        (unsigned char)shift,
                                        0x5f,
                                        0xb2,
                                        (unsigned char)argument,
                                        1,
                                        1,
                                        (unsigned char)ops[band]};
                CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), PDF_TT_VM_GLYPH, 100));
                int step = (int)(argument & 15) - 8;
                if (step >= 0)
                    step++;
                CHECK(vm.zones[1].points[1].current[0] == 31 + step * (64 >> shift));
                CHECK(vm.zones[1].points[1].original[0] == -91 &&
                      vm.zones[1].points[1].touched == 1);
                CHECK(vm.graphics.loop == 9 && !vm.stack_size);
                CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 2048, (int32_t)(size + 1) * 64, 768, 0,
                                                    NULL));
                CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
                CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), PDF_TT_VM_GLYPH, 100));
                CHECK(vm.zones[1].points[1].current[0] == 31 && !vm.zones[1].points[1].touched);
                finish(&vm);
            }
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
    static const unsigned char shift_three[] = {0x04, 0xb0, 3, 0x17, 0xb3, 0, 1, 2, 64, 0x38};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, shift_three, sizeof(shift_three), PDF_TT_VM_GLYPH, 100));
    for (unsigned i = 0; i < 3; i++) {
        CHECK(vm.zones[1].points[i].current[0] == points[i].current[0]);
        CHECK(vm.zones[1].points[i].current[1] == points[i].current[1] + 64);
        CHECK(vm.zones[1].points[i].touched == 2);
    }
    CHECK(vm.graphics.loop == 1 && !vm.stack_size);
    finish(&vm);
    init(&vm, NULL, 0);
    static const unsigned char zero_loop[] = {0xb0, 0, 0x17, 0xb0, 64, 0x38};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, zero_loop, sizeof(zero_loop), PDF_TT_VM_GLYPH, 6));
    CHECK(vm.graphics.loop == 1 && !vm.stack_size && !vm.work_left);
    finish(&vm);
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, shift_three, sizeof(shift_three), PDF_TT_VM_GLYPH, 12));
    CHECK(vm.error == PDF_TT_VM_BUDGET && !vm.zones[1].points[0].touched);
    finish(&vm);
    static const unsigned char need_two[] = {0x3e, 0x3f};
    for (unsigned i = 0; i < sizeof(need_two); i++)
        error(need_two + i, 1, PDF_TT_VM_STACK, 100);
    static const unsigned char no_size[] = {0x5d, 0x71, 0x72};
    for (unsigned i = 0; i < sizeof(no_size); i++)
        error(no_size + i, 1, PDF_TT_VM_SIZE, 100);
    static const unsigned char no_shift[] = {0x38};
    error(no_shift, sizeof(no_shift), PDF_TT_VM_STACK, 100);
    for (unsigned op = 0x32; op <= 0x33; op++) {
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 1, points, 3, 3));
        unsigned char code[] = {0xb0, 1, op & 1 ? 0x11 : 0x12, 0xb0, 3, 0x17, 0xb2, 0,
                                1,    2, (unsigned char)op};
        CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), PDF_TT_VM_GLYPH, 100));
        for (unsigned i = 0; i < 3; i++) {
            CHECK(vm.zones[1].points[i].current[0] == points[i].current[0] + 122);
            CHECK(vm.zones[1].points[i].original[0] == points[i].original[0]);
            CHECK(vm.zones[1].points[i].touched == 1);
        }
        CHECK(vm.graphics.loop == 1 && !vm.stack_size);
        finish(&vm);
    }
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 1, points, 3, 3));
    vm.zones[0].points[0].current[0] = 73;
    vm.zones[0].points[0].original[0] = 9;
    static const unsigned char from_twilight[] = {0xb0, 0, 0x13, 0xb0, 0, 0x11, 0xb0, 1, 0x33};
    CHECK(
        cupidimage_pdf_tt_vm_run(&vm, from_twilight, sizeof(from_twilight), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.zones[1].points[1].current[0] == 95 && vm.zones[1].points[1].original[0] == -91);
    CHECK(vm.zones[0].points[0].current[0] == 73);
    finish(&vm);

    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
    static const unsigned char absent_cvt[] = {0xb1, 1, 0, 0x3e};
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, absent_cvt, sizeof(absent_cvt), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.error == PDF_TT_VM_STORAGE && vm.zones[1].points[1].current[0] == 31);
    finish(&vm);
    const int32_t maximum[] = {INT32_MAX};
    init(&vm, maximum, 1);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
    vm.graphics.cvt_cut_in = INT32_MAX;
    static const unsigned char rounded_overflow[] = {0xb1, 1, 0, 0x3f};
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, rounded_overflow, sizeof(rounded_overflow),
                                    PDF_TT_VM_GLYPH, 100));
    CHECK(vm.error == PDF_TT_VM_ARITHMETIC && vm.zones[1].points[1].current[0] == 31);
    finish(&vm);
    const unsigned char delta_bad[][5] = {
        {0xb0, 1, 0x5d, 0x59, 0x59}, {0xb2, 8, 3, 1, 0x5d}, {0xb2, 8, 1, 1, 0x5d}};
    const pdf_tt_vm_error errors[] = {PDF_TT_VM_STACK, PDF_TT_VM_POINT, PDF_TT_VM_BUDGET};
    for (unsigned i = 0; i < 3; i++) {
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 2048, 9 * 64, 768, 0, NULL));
        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
        CHECK(!cupidimage_pdf_tt_vm_run(&vm, delta_bad[i], 5, PDF_TT_VM_GLYPH, i == 2 ? 5 : 100));
        CHECK(vm.error == errors[i] && vm.zones[1].points[1].current[0] == 31);
        finish(&vm);
    }
    for (unsigned op = 0x32; op <= 0x33; op++) {
        unsigned char missing[] = {(unsigned char)op};
        error(missing, 1, PDF_TT_VM_STACK, 100);
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
        unsigned char limited[] = {0xb0, 1, op & 1 ? 0x11 : 0x12, 0xb0, 3, 0x17, 0xb2, 0,
                                   1,    2, (unsigned char)op};
        CHECK(!cupidimage_pdf_tt_vm_run(&vm, limited, sizeof(limited), PDF_TT_VM_GLYPH, 13));
        CHECK(vm.error == PDF_TT_VM_BUDGET && !vm.zones[1].points[0].touched);
        finish(&vm);
    }
}

static void align_points_tests(void) {
    const pdf_tt_vm_point points[] = {{{-93, 35}, {20, 25}, 0, 1, 0},
                                      {{100, -14}, {100, 0}, 0, 0, 1}};
    for (unsigned axis = 0; axis < 2; axis++)
        for (unsigned za = 0; za < 2; za++)
            for (unsigned zb = 0; zb < 2; zb++)
                for (unsigned a = 0; a < 2; a++)
                    for (unsigned b = 0; b < 2; b++) {
                        pdf_tt_vm vm;
                        init(&vm, NULL, 0);
                        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 2, points, 2, 2));
                        vm.zones[0].points[0] = points[1];
                        vm.zones[0].points[1] = points[0];
                        vm.graphics.zone[1] = za;
                        vm.graphics.zone[0] = zb;
                        vm.graphics.loop = 5;
                        pdf_tt_vm_point before_a = vm.zones[za].points[a];
                        pdf_tt_vm_point before_b = vm.zones[zb].points[b];
                        int32_t half = (before_b.current[axis] - before_a.current[axis]) / 2;
                        unsigned char code[] = {(unsigned char)(1 - axis), 0xb1, (unsigned char)a,
                                                (unsigned char)b, 0x27};
                        CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
                        CHECK(vm.zones[za].points[a].current[axis] ==
                              before_a.current[axis] + half);
                        CHECK(vm.zones[zb].points[b].current[axis] ==
                              before_b.current[axis] - half);
                        CHECK(vm.zones[za].points[a].current[1 - axis] ==
                              before_a.current[1 - axis]);
                        CHECK(vm.zones[zb].points[b].current[1 - axis] ==
                              before_b.current[1 - axis]);
                        CHECK(vm.zones[za].points[a].touched == (1u << axis));
                        CHECK(vm.zones[zb].points[b].touched == (1u << axis));
                        CHECK(!memcmp(vm.zones[za].points[a].original, before_a.original,
                                      sizeof(before_a.original)));
                        CHECK(!memcmp(vm.zones[zb].points[b].original, before_b.original,
                                      sizeof(before_b.original)));
                        CHECK(vm.zones[za].points[a].on == before_a.on &&
                              vm.zones[zb].points[b].end == before_b.end);
                        CHECK(vm.graphics.loop == 5 && !vm.stack_size);
                        finish(&vm);
                    }
    const unsigned char missing = 0x27;
    error(&missing, 1, PDF_TT_VM_STACK, 100);
    for (unsigned bad = 0; bad < 2; bad++) {
        pdf_tt_vm vm;
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 2, 2));
        unsigned char code[] = {0xb1, 0, 0, 0x27};
        code[bad + 1] = 99;
        CHECK(!cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
        CHECK(vm.error == PDF_TT_VM_POINT);
        finish(&vm);
    }
}
