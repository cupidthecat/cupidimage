static void interpolation_tests(void) {
    pdf_tt_vm vm;
    const int32_t design[][2] = {{0, 0}, {30, 30}, {90, 90}};
    const int32_t scale[] = {13312, 37888, 65536, 99328};
    for (unsigned axis = 0; axis < 2; axis++)
        for (unsigned size = 0; size < 4; size++)
            for (int left = -129; left <= 129; left += 43)
                for (int right = -129; right <= 129; right += 43) {
                    pdf_tt_vm_point points[3] = {0};
                    for (unsigned i = 0; i < 3; i++)
                        for (unsigned a = 0; a < 2; a++)
                            points[i].original[a] = points[i].current[a] =
                                (int32_t)(((int64_t)design[i][a] * scale[size] + 32768) / 65536);
                    points[2].end = 1;
                    points[0].current[axis] = left;
                    points[2].current[axis] = right;
                    points[0].touched = points[2].touched = (unsigned char)(1u << axis);
                    for (unsigned method = 0; method < 2; method++) {
                        init(&vm, NULL, 0);
                        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
                        CHECK(cupidimage_pdf_tt_vm_set_design(&vm, design, 3, scale[size]));
                        vm.graphics.reference[1] = 0;
                        vm.graphics.reference[2] = 2;
                        unsigned char ip[] = {(unsigned char)(axis ? 0 : 1), 0xb0, 1, 0x39};
                        unsigned char iup = (unsigned char)(axis ? 0x30 : 0x31);
                        CHECK(cupidimage_pdf_tt_vm_run(&vm, method ? &iup : ip,
                                                       method ? 1 : sizeof(ip), 0, 100));
                        int64_t numerator = (int64_t)(right - left) * (method ? 65536 : 30);
                        int64_t value =
                            numerator < 0 ? -((-numerator + 45) / 90) : (numerator + 45) / 90;
                        if (method) {
                            numerator = value * 30;
                            value = numerator < 0 ? -((-numerator + 32768) / 65536)
                                                  : (numerator + 32768) / 65536;
                        }
                        CHECK(vm.zones[1].points[1].current[axis] == left + value);
                        CHECK(vm.zones[1].points[1].current[1 - axis] ==
                              points[1].current[1 - axis]);
                        CHECK(vm.zones[1].points[1].touched == (method ? 0 : (1u << axis)));
                        CHECK(vm.graphics.reference[1] == 0 && vm.graphics.reference[2] == 2);
                        CHECK(vm.graphics.loop == 1 && !vm.stack_size);
                        finish(&vm);
                    }
                }
    /* Two contours, wraparound anchors, a single anchor, and excluded phantoms. */
    pdf_tt_vm_point points[] = {{{10, 0}, {10, 0}, 0, 1, 0}, {{84, 0}, {20, 0}, 1, 1, 0},
                                {{30, 0}, {30, 0}, 0, 1, 1}, {{-10, 0}, {-10, 0}, 0, 1, 0},
                                {{25, 0}, {5, 0}, 1, 1, 0},  {{45, 0}, {25, 0}, 1, 1, 0},
                                {{35, 0}, {35, 0}, 0, 1, 1}, {{1000, 0}, {1000, 0}, 0, 1, 0}};
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 8, 7));
    static const unsigned char twice[] = {0x31, 0x31};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, twice, sizeof(twice), 0, 100));
    const int32_t expected[] = {74, 84, 94, 10, 25, 45, 55, 1000};
    for (unsigned i = 0; i < 8; i++) {
        CHECK(vm.zones[1].points[i].current[0] == expected[i]);
        CHECK(vm.zones[1].points[i].touched == points[i].touched);
    }
    finish(&vm);
    for (unsigned kind = 0; kind < 5; kind++) {
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 1, points, 8, 7));
        unsigned char code[] = {0xb0, 2, 0x39};
        if (kind == 0) {
            CHECK(!cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
            CHECK(vm.error == PDF_TT_VM_ARITHMETIC);
        } else if (kind == 1) {
            vm.graphics.reference[2] = 99;
            CHECK(!cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
            CHECK(vm.error == PDF_TT_VM_POINT);
        } else {
            code[0] = 0x31;
            if (kind == 2)
                vm.graphics.zone[2] = 0;
            if (kind == 3)
                vm.zones[1].points[6].end = 0;
            CHECK(!cupidimage_pdf_tt_vm_run(&vm, code, 1, 0, kind == 4 ? 21 : 100));
            CHECK(vm.error == (kind == 4 ? PDF_TT_VM_BUDGET : PDF_TT_VM_POINT));
        }
        finish(&vm);
    }
    init(&vm, NULL, 0);
    vm.graphics.loop = 0;
    static const unsigned char empty[] = {0x39, 0x31, 0x30};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, empty, sizeof(empty), 0, 100));
    CHECK(vm.graphics.loop == 1);
    finish(&vm);
}
