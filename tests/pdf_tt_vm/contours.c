static void contour_tests(void) {
    const pdf_tt_vm_point points[] = {
        {{31, 0}, {0, 0}, 0, 1, 0},        {{100, 100}, {100, 100}, 0, 1, 1},
        {{200, 200}, {200, 200}, 0, 1, 0}, {{300, 300}, {300, 300}, 0, 1, 1},
        {{400, 400}, {400, 400}, 0, 0, 0}, {{500, 500}, {500, 500}, 0, 0, 0}};
    for (unsigned op = 0x34; op <= 0x35; op++)
        for (unsigned contour = 0; contour < 2; contour++) {
            pdf_tt_vm vm;
            init(&vm, NULL, 0);
            CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 3, points, 6, 4));
            vm.graphics.loop = 7;
            unsigned char code[] = {0xb0, (unsigned char)contour, (unsigned char)op};
            CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
            for (unsigned i = 0; i < 6; i++) {
                int shifted = i >= contour * 2 && i < contour * 2 + 2 && i != 0;
                CHECK(vm.zones[1].points[i].current[0] ==
                      points[i].current[0] + (shifted ? 31 : 0));
                CHECK(vm.zones[1].points[i].current[1] == points[i].current[1]);
                CHECK(vm.zones[1].points[i].touched == (shifted ? 1 : 0));
                CHECK(vm.zones[1].points[i].original[0] == points[i].original[0]);
            }
            CHECK(vm.graphics.loop == 7 && !vm.stack_size);
            finish(&vm);
        }
    for (unsigned source = 0; source < 2; source++)
        for (unsigned target = 0; target < 2; target++) {
            pdf_tt_vm vm;
            init(&vm, NULL, 0);
            CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 3, points, 6, 4));
            vm.graphics.zone[0] = source;
            vm.graphics.zone[2] = target;
            vm.zones[0].points[0].current[0] = 97;
            static const unsigned char code[] = {0xb0, 0, 0x35};
            CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
            for (unsigned i = 0; i < (target ? 2u : 3u); i++) {
                int32_t base = target ? points[i].current[0] : i ? 0 : 97;
                int moved = source != target || i != 0;
                CHECK(vm.zones[target].points[i].current[0] ==
                      base + (moved ? source ? 31 : 97 : 0));
                CHECK(vm.zones[target].points[i].touched == (moved ? 1 : 0));
            }
            finish(&vm);
        }
    for (unsigned kind = 0; kind < 5; kind++) {
        pdf_tt_vm vm;
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 3, points, 6, 4));
        unsigned char code[] = {0xb0, 1, 0x35};
        if (kind == 0)
            vm.graphics.zone[2] = 0;
        if (kind == 1)
            vm.graphics.reference[1] = 99;
        if (kind == 2)
            vm.zones[1].points[3].end = 0;
        CHECK(!cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0,
                                        kind >= 3 ? kind == 3 ? 3 : 8 : 100));
        CHECK(vm.error == (kind >= 3 ? PDF_TT_VM_BUDGET : PDF_TT_VM_POINT));
        finish(&vm);
    }
    pdf_tt_vm vm;
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 6, 4));
    static const unsigned char exact[] = {0xb0, 1, 0x35};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, exact, sizeof(exact), 0, 9));
    CHECK(vm.work_left == 0);
    finish(&vm);
    for (unsigned op = 0x36; op <= 0x37; op++)
        for (unsigned source = 0; source < 2; source++)
            for (unsigned target = 0; target < 2; target++) {
                init(&vm, NULL, 0);
                CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 3, points, 6, 4));
                vm.graphics.zone[op & 1 ? 0 : 1] = source;
                vm.graphics.zone[2] = 1 - target;
                vm.graphics.loop = 7;
                vm.zones[0].points[0].current[0] = 97;
                vm.zones[0].points[1].touched = 2;
                unsigned char code[] = {0xb0, (unsigned char)target, (unsigned char)op};
                CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
                for (unsigned i = 0; i < (target ? 6u : 3u); i++) {
                    int moved = (target == 0 || i < 4) && (source != target || i != 0);
                    int32_t base = target ? points[i].current[0] : i ? 0 : 97;
                    CHECK(vm.zones[target].points[i].current[0] ==
                          base + (moved ? source ? 31 : 97 : 0));
                    CHECK(vm.zones[target].points[i].touched == (!target && i == 1 ? 2 : 0));
                }
                CHECK(vm.graphics.loop == 7 && !vm.stack_size);
                finish(&vm);
            }
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 6, 4));
    static const unsigned char shifted_zone[] = {0xb0, 1, 0x37};
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, shifted_zone, sizeof(shifted_zone), 0, 6));
    CHECK(vm.error == PDF_TT_VM_BUDGET);
    finish(&vm);
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 6, 4));
    CHECK(cupidimage_pdf_tt_vm_run(&vm, shifted_zone, sizeof(shifted_zone), 0, 7));
    CHECK(vm.work_left == 0 && vm.zones[1].points[4].current[0] == 400);
    finish(&vm);
}
