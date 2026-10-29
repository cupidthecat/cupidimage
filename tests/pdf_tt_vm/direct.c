static void direct_tests(void) {
    pdf_tt_vm vm;
    const int32_t design[][2] = {{31, 35}, {100, 0}, {0, 100}};
    const int32_t scales[] = {13312, 37888, 65536, 98304, 100001};
    for (unsigned op = 0xc0; op <= 0xdf; op++) {
        if ((op & 3) == 3) {
            unsigned char code = (unsigned char)op;
            error(&code, 1, PDF_TT_VM_UNSUPPORTED, 100);
            continue;
        }
        for (unsigned scale = 0; scale < sizeof(scales) / sizeof(*scales); scale++)
            for (unsigned axis = 0; axis < 2; axis++)
                for (unsigned reference = 0; reference < 3; reference++)
                    for (unsigned target = 0; target < 3; target++) {
                        pdf_tt_vm_point points[3] = {0};
                        for (unsigned i = 0; i < 3; i++)
                            for (unsigned a = 0; a < 2; a++)
                                points[i].original[a] = points[i].current[a] =
                                    (int32_t)(((int64_t)design[i][a] * scales[scale] + 32768) /
                                              65536);
                        points[reference].current[axis] += 43;
                        init(&vm, NULL, 0);
                        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 2, points, 3, 3));
                        CHECK(cupidimage_pdf_tt_vm_set_design(&vm, design, 3, scales[scale]));
                        vm.graphics.reference[0] = reference;
                        unsigned char code[] = {(unsigned char)(axis ? 0 : 1), 0xb0,
                                                (unsigned char)target, (unsigned char)op};
                        CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
                        int64_t product =
                            (int64_t)(design[target][axis] - design[reference][axis]) *
                            scales[scale];
                        int32_t distance = (int32_t)(product < 0 ? -((-product + 32768) / 65536)
                                                                 : (product + 32768) / 65536);
                        int negative = distance < 0;
                        if (op & 4)
                            distance = distance < 0 ? -((-distance + 32) / 64 * 64)
                                                    : (distance + 32) / 64 * 64;
                        if (op & 8) {
                            if (!negative && distance < 64)
                                distance = 64;
                            if (negative && distance > -64)
                                distance = -64;
                        }
                        CHECK(vm.zones[1].points[target].current[axis] ==
                              points[reference].current[axis] + distance);
                        CHECK(vm.zones[1].points[target].current[1 - axis] ==
                              points[target].current[1 - axis]);
                        CHECK(vm.zones[1].points[target].touched == (1u << axis));
                        CHECK(vm.graphics.reference[0] == (op & 16 ? target : reference));
                        CHECK(vm.graphics.reference[1] == reference &&
                              vm.graphics.reference[2] == target);
                        CHECK(!memcmp(vm.zones[1].design, design, sizeof(design)));
                        CHECK(!vm.stack_size);
                        finish(&vm);
                    }
    }
    const pdf_tt_vm_point points[3] = {0};
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
    int32_t copy[3][2];
    memcpy(copy, design, sizeof(copy));
    CHECK(cupidimage_pdf_tt_vm_set_design(&vm, copy, 3, 65536));
    copy[0][0] = -99;
    CHECK(vm.zones[1].design[0][0] == 31);
    CHECK(cupidimage_pdf_tt_vm_set_design(&vm, design, 3, 32768));
    CHECK(vm.zones[1].design_scale == 32768);
    fail_at = allocations + 1;
    CHECK(!cupidimage_pdf_tt_vm_set_design(&vm, design, 3, 12345));
    CHECK(vm.error == PDF_TT_VM_MEMORY && vm.zones[1].design_scale == 32768);
    fail_at = 0;
    finish(&vm);
    for (unsigned kind = 0; kind < 4; kind++) {
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
        CHECK(!cupidimage_pdf_tt_vm_set_design(&vm, kind == 0 ? NULL : design, kind == 1 ? 2 : 3,
                                               kind == 2   ? 0
                                               : kind == 3 ? -1
                                                           : 65536));
        CHECK(vm.error == PDF_TT_VM_POINT);
        finish(&vm);
    }
    for (unsigned kind = 0; kind < 3; kind++) {
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 4096, 4096, 4096, 0, NULL));
        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 2, points, 3, 3));
        CHECK(cupidimage_pdf_tt_vm_run(&vm, NULL, 0, PDF_TT_VM_PREP, 100));
        CHECK(cupidimage_pdf_tt_vm_save_preparation(&vm));
        CHECK(cupidimage_pdf_tt_vm_set_design(&vm, design, 3, 12345));
        if (kind == 0)
            CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 4096, 2048, 2048, 0, NULL));
        else if (kind == 1)
            CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 1, points, 3, 3));
        else
            CHECK(cupidimage_pdf_tt_vm_begin_glyph(&vm, points, 3, 3));
        CHECK(!vm.zones[1].design && !vm.zones[1].design_scale);
        finish(&vm);
    }
    for (unsigned original = 0; original < 2; original++) {
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 1, points, 3, 3));
        CHECK(cupidimage_pdf_tt_vm_set_design(&vm, design, 3, 13312));
        vm.zones[1].points[0].current[0] = 97;
        vm.zones[1].points[1].current[0] = -31;
        unsigned char measure[] = {0xb1, 0, 1, (unsigned char)(0x49 + original)};
        CHECK(cupidimage_pdf_tt_vm_run(&vm, measure, sizeof(measure), 0, 100));
        CHECK(vm.stack_size == 1 && vm.stack[0] == (original ? -14 : 128));
        vm.stack_size = 0;
        vm.graphics.zone[0] = 0;
        vm.zones[0].points[0].original[0] = 29;
        vm.zones[0].points[0].current[0] = 37;
        CHECK(cupidimage_pdf_tt_vm_run(&vm, measure, sizeof(measure), 0, 100));
        CHECK(vm.stack_size == 1 && vm.stack[0] == (original ? 29 : 68));
        finish(&vm);
    }
    for (unsigned kind = 0; kind < 4; kind++) {
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 3, 3));
        const int32_t extreme[][2] = {{INT32_MIN, 0}, {INT32_MAX, 0}, {0, 0}};
        CHECK(cupidimage_pdf_tt_vm_set_design(&vm, kind == 2 ? design : extreme, 3,
                                              kind == 3 ? INT32_MAX : 65536));
        unsigned char code[] = {0xb0, 1, 0xc0};
        if (kind == 1) {
            code[1] = 9;
        } else if (kind == 2) {
            vm.graphics.reference[0] = UINT32_MAX;
        }
        CHECK(!cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
        CHECK(vm.error == (kind == 1 || kind == 2 ? PDF_TT_VM_POINT : PDF_TT_VM_ARITHMETIC));
        finish(&vm);
    }
    for (int direction = -1; direction <= 1; direction += 2) {
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_normalize(&vm, 3, 5, vm.graphics.freedom));
        CHECK(cupidimage_pdf_tt_vm_normalize(&vm, direction * 7, direction * 11,
                                             vm.graphics.projection));
        int64_t product = (int64_t)vm.graphics.freedom[0] * vm.graphics.projection[0] +
                          (int64_t)vm.graphics.freedom[1] * vm.graphics.projection[1];
        int64_t divisor = product >= 0 ? product / 16384 : -((-product + 16383) / 16384);
        pdf_tt_vm_point point = {0};
        CHECK(cupidimage_pdf_tt_vm_move(&vm, &point, 987654));
        for (unsigned axis = 0; axis < 2; axis++) {
            int64_t numerator = INT64_C(987654) * vm.graphics.freedom[axis];
            int64_t expected = divisor < 0 ? -((numerator - divisor / 2) / -divisor)
                                           : (numerator + divisor / 2) / divisor;
            CHECK(point.current[axis] == expected);
        }
        CHECK(point.touched == 3);
        finish(&vm);
    }
}
