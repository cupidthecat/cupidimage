static void point_flag_tests(void) {
    const pdf_tt_vm_point points[] = {{{31, 35}, {20, 25}, 3, 1, 0},
                                      {{100, 0}, {100, 0}, 1, 0, 0},
                                      {{0, 100}, {0, 100}, 2, 1, 1},
                                      {{64, 0}, {60, 0}, 0, 0, 0}};
    for (unsigned op = 0x81; op <= 0x82; op++)
        for (unsigned low = 0; low < 4; low++)
            for (unsigned high = 0; high < 4; high++) {
                pdf_tt_vm vm;
                init(&vm, NULL, 0);
                CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 2, points, 4, 3));
                vm.graphics.loop = 7;
                unsigned char code[] = {0xb1, (unsigned char)low, (unsigned char)high,
                                        (unsigned char)op};
                CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
                for (unsigned i = 0; i < 4; i++) {
                    pdf_tt_vm_point expected = points[i];
                    if (i >= low && i <= high)
                        expected.on = op == 0x81;
                    for (unsigned axis = 0; axis < 2; axis++)
                        CHECK(vm.zones[1].points[i].current[axis] == expected.current[axis] &&
                              vm.zones[1].points[i].original[axis] == expected.original[axis]);
                    CHECK(vm.zones[1].points[i].on == expected.on &&
                          vm.zones[1].points[i].touched == expected.touched &&
                          vm.zones[1].points[i].end == expected.end);
                }
                CHECK(vm.graphics.loop == 7 && !vm.stack_size);
                finish(&vm);
            }
    pdf_tt_vm vm;
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 2, points, 4, 3));
    static const unsigned char repeated[] = {0xb0, 4, 0x17, 0xb3, 0, 1, 0, 3, 0x80};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, repeated, sizeof(repeated), 0, 100));
    CHECK(vm.zones[1].points[0].on == 1 && vm.zones[1].points[1].on == 1 &&
          vm.zones[1].points[3].on == 1);
    CHECK(vm.graphics.loop == 1 && vm.zones[1].points[0].touched == 3);
    finish(&vm);
    for (unsigned op = 0x80; op <= 0x82; op++) {
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 2, points, 4, 3));
        vm.graphics.zone[0] = 0;
        vm.zones[0].points[0].on = op == 0x82;
        unsigned char code[] = {0xb1, 0, 0, (unsigned char)op};
        CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
        CHECK(vm.zones[0].points[0].on == (op != 0x82));
        CHECK(vm.zones[1].points[0].on == 1);
        finish(&vm);
    }
    for (unsigned op = 0x80; op <= 0x82; op++)
        for (unsigned kind = 0; kind < 3; kind++) {
            init(&vm, NULL, 0);
            CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 4, 3));
            unsigned char code[] = {0xb1, 0, 0, (unsigned char)op};
            if (kind == 0)
                code[2] = 99;
            if (kind == 1)
                code[0] = 0xb0;
            if (kind == 1) {
                CHECK(!cupidimage_pdf_tt_vm_run(&vm, code + 3, 1, 0, 100));
                CHECK(vm.error == PDF_TT_VM_STACK);
            } else {
                CHECK(!cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, kind == 2 ? 4 : 100));
                CHECK(vm.error == (kind == 2 ? PDF_TT_VM_BUDGET : PDF_TT_VM_POINT));
            }
            finish(&vm);
        }
    init(&vm, NULL, 0);
    vm.graphics.loop = 0;
    static const unsigned char zero = 0x80;
    CHECK(cupidimage_pdf_tt_vm_run(&vm, &zero, 1, 0, 1));
    CHECK(vm.graphics.loop == 1);
    finish(&vm);
}
