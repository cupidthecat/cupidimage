static void point_tests(void) {
    pdf_tt_vm vm;
    const pdf_tt_vm_point input[] = {{{31, 35}, {31, 35}, 0, 1, 0},
                                     {{100, 0}, {0, 100}, 0, 1, 0},
                                     {{0, 100}, {0, 100}, 0, 1, 1}};
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 2, input, 3, 3));
    CHECK(vm.zones[0].count == 2 && vm.zones[1].outline_count == 3);
    CHECK(vm.zones[1].points != input && vm.zones[1].points[2].end);
    static const unsigned char position[] = {0xb0, 0, 0x2f, 0xb0, 0, 0x46, 0xb0, 0, 0x47};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, position, sizeof(position), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.stack_size == 2 && vm.stack[0] == 0 && vm.stack[1] == 31);
    CHECK(vm.zones[1].points[0].current[1] == 35 && vm.zones[1].points[0].touched == 1);
    CHECK(vm.graphics.reference[0] == 0 && vm.graphics.reference[1] == 0);
    static const unsigned char set_x[] = {0xb1, 0, 97, 0x48};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, set_x, sizeof(set_x), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.zones[1].points[0].current[0] == 97 && vm.zones[1].points[0].original[0] == 31);
    static const unsigned char set_y[] = {0x00, 0xb0, 0, 0x2f};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, set_y, sizeof(set_y), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.zones[1].points[0].current[1] == 64 && vm.zones[1].points[0].touched == 3);
    static const unsigned char untouch[] = {0xb0, 0, 0x29};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, untouch, sizeof(untouch), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.zones[1].points[0].touched == 1);
    static const unsigned char twilight[] = {0x01, 0xb0, 0, 0x16, 0xb1, 1, 64, 0x48};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, twilight, sizeof(twilight), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.zones[0].points[1].current[0] == 64 && vm.zones[0].points[1].original[0] == 64);
    CHECK(vm.zones[0].points[1].touched == 1 && !vm.zones[0].points[0].touched);
    finish(&vm);
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, input, 3, 3));
    static const unsigned char mark[] = {0xb0, 2, 0x2e};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, mark, sizeof(mark), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.graphics.reference[0] == 2 && vm.graphics.reference[1] == 2);
    CHECK(vm.zones[1].points[2].current[0] == 0 && vm.zones[1].points[2].current[1] == 100);
    CHECK(vm.zones[1].points[2].touched == 1);
    finish(&vm);
    const pdf_tt_vm_point lines[] = {
        {{0, 0}, {0, 0}, 0, 1, 0}, {{100, 0}, {0, 100}, 0, 1, 0}, {{0, 100}, {100, 0}, 0, 1, 1}};
    for (unsigned op = 0x06; op <= 0x09; op++) {
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, lines, 3, 3));
        unsigned char code[] = {0xb1, 0, 1, (unsigned char)op};
        CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), PDF_TT_VM_GLYPH, 100));
        int32_t *vector = op >= 8 ? vm.graphics.freedom : vm.graphics.projection;
        CHECK(vector[0] == (op & 1 ? 0 : -16384));
        CHECK(vector[1] == (op & 1 ? -16384 : 0));
        if (op < 8)
            CHECK(!memcmp(vector, vm.graphics.dual_projection, 2 * sizeof(*vector)));
        finish(&vm);
    }
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, lines, 3, 3));
    static const unsigned char dual[] = {0xb1, 0, 1, 0x86};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, dual, sizeof(dual), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.graphics.projection[0] == -16384 && vm.graphics.projection[1] == 0);
    CHECK(vm.graphics.dual_projection[0] == 0 && vm.graphics.dual_projection[1] == -16384);
    finish(&vm);
    for (unsigned failure = 1; failure <= 2; failure++) {
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 2, lines, 3, 3));
        pdf_tt_vm_point *old = vm.zones[1].points;
        fail_at = allocations + failure;
        CHECK(!cupidimage_pdf_tt_vm_set_zones(&vm, 5, input, 3, 3));
        CHECK(vm.error == PDF_TT_VM_MEMORY && vm.zones[1].points == old);
        fail_at = 0;
        finish(&vm);
    }
    const unsigned char one_point[] = {0x29, 0x2e, 0x2f, 0x46, 0x47};
    for (unsigned i = 0; i < sizeof(one_point); i++) {
        error(one_point + i, 1, PDF_TT_VM_STACK, 100);
        unsigned char code[] = {0xb0, 0, one_point[i]};
        error(code, sizeof(code), PDF_TT_VM_POINT, 100);
    }
    static const unsigned char two_points[] = {0x06, 0x07, 0x08, 0x09, 0x48, 0x86, 0x87};
    for (unsigned i = 0; i < sizeof(two_points); i++) {
        error(two_points + i, 1, PDF_TT_VM_STACK, 100);
        unsigned char code[] = {0xb1, 0, 0, two_points[i]};
        error(code, sizeof(code), PDF_TT_VM_POINT, 100);
    }
    for (unsigned i = 0; i < 4; i++) {
        init(&vm, NULL, 0);
        CHECK(!cupidimage_pdf_tt_vm_set_zones(&vm, i == 0 ? 65536 : 0, i == 1 ? NULL : input,
                                              i == 2 ? 65541 : 3, i == 3 ? 4 : 3));
        CHECK(vm.error == PDF_TT_VM_POINT);
        finish(&vm);
    }
    init(&vm, NULL, 0);
    CHECK(!cupidimage_pdf_tt_vm_set_zones(&vm, 0, input, 8, 3));
    CHECK(vm.error == PDF_TT_VM_POINT);
    finish(&vm);
    init(&vm, NULL, 0);
    CHECK(!cupidimage_pdf_tt_vm_set_zones(&vm, 0, input, 65537, 65537));
    CHECK(vm.error == PDF_TT_VM_POINT);
    finish(&vm);
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, lines, 3, 3));
    static const unsigned char perpendicular[] = {0x04, 0xb1, 0, 64, 0x48};
    CHECK(
        !cupidimage_pdf_tt_vm_run(&vm, perpendicular, sizeof(perpendicular), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.error == PDF_TT_VM_ARITHMETIC && !vm.zones[1].points[0].touched);
    finish(&vm);

    for (unsigned negative = 0; negative < 2; negative++) {
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, input, 3, 3));
        unsigned char code[] = {0xb8, negative ? 0xff : 0, negative ? 0xff : 3, 0x46};
        CHECK(!cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), PDF_TT_VM_GLYPH, 100));
        CHECK(vm.error == PDF_TT_VM_POINT);
        finish(&vm);
    }
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 2, input, 3, 3));
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, NULL, 0, 0));
    CHECK(!vm.zones[0].points && !vm.zones[1].points && !vm.zones[1].count);
    finish(&vm);
    init(&vm, NULL, 0);
    const pdf_tt_vm_point large[] = {{{INT32_MAX, INT32_MAX}, {INT32_MAX, INT32_MAX}, 0, 1, 1}};
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, large, 1, 1));
    static const unsigned char overflow_round[] = {0xb0, 0, 0x2f};
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, overflow_round, sizeof(overflow_round), PDF_TT_VM_GLYPH,
                                    100));
    CHECK(vm.error == PDF_TT_VM_ARITHMETIC && vm.zones[1].points[0].current[0] == INT32_MAX);
    finish(&vm);
    init(&vm, NULL, 0);
    const int32_t extremes[][2] = {
        {INT32_MIN, INT32_MIN}, {INT32_MIN, INT32_MAX}, {INT32_MAX, 1}, {1, INT32_MIN}, {0, 0}};
    for (unsigned i = 0; i < sizeof(extremes) / sizeof(*extremes); i++) {
        int32_t vector[2] = {0, 16384};
        CHECK(cupidimage_pdf_tt_vm_normalize(&vm, extremes[i][0], extremes[i][1], vector));
        CHECK(vector[0] >= -16384 && vector[0] <= 16384 && vector[1] >= -16384 &&
              vector[1] <= 16384);
        if (!extremes[i][0] && !extremes[i][1])
            CHECK(vector[0] == 0 && vector[1] == 16384);
    }
    for (unsigned i = 0; i < 10000; i++) {
        int32_t x = random_value(), y = random_value(), a[2] = {16384, 0}, b[2] = {16384, 0};
        CHECK(cupidimage_pdf_tt_vm_normalize(&vm, x, y, a));
        CHECK(cupidimage_pdf_tt_vm_normalize(&vm, -x, -y, b));
        if (x || y)
            CHECK(a[0] == -b[0] && a[1] == -b[1]);
        CHECK(cupidimage_pdf_tt_vm_normalize(&vm, x * 2, y * 2, b));
        CHECK(a[0] == b[0] && a[1] == b[1]);
        int64_t length = (int64_t)a[0] * a[0] + (int64_t)a[1] * a[1];
        CHECK(length >= INT64_C(268300000) && length <= INT64_C(268500000));
    }
    finish(&vm);
}

static void projection_precision_tests(void) {
    for (unsigned axis = 0; axis < 2; axis++)
        for (unsigned positive = 0; positive < 2; positive++) {
            pdf_tt_vm vm;
            init(&vm, NULL, 0);
            pdf_tt_vm_point points[2] = {0};
            points[0].current[axis] = 768;
            points[1].current[axis] = 775;
            points[1].current[1 - axis] = 704;
            CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 2, 2));
            vm.graphics.freedom[axis] = 16384;
            vm.graphics.freedom[1 - axis] = 0;
            int32_t vector[2];
            vector[axis] = 554;
            vector[1 - axis] = positive ? 1 : -1;
            CHECK(
                cupidimage_pdf_tt_vm_normalize(&vm, vector[0], vector[1], vm.graphics.projection));
            CHECK(vm.graphics.projection[axis] == 16384 &&
                  vm.graphics.projection[1 - axis] == (positive ? 29 : -29));
            static const unsigned char code[] = {0xb0, 1, 0x3c};
            CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
            CHECK(vm.zones[1].points[1].current[axis] == (positive ? 767 : 769));
            CHECK(vm.zones[1].points[1].current[1 - axis] == 704);
            finish(&vm);
        }
}
