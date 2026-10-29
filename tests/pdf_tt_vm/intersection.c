static void intersection_tests(void) {
    pdf_tt_vm vm;

    /* Exact wide products, signed ties, and values near the coordinate limits. */
    static const struct {
        int64_t n, d;
        int32_t b;
        int64_t answer;
    } ratios[] = {
        {INT64_MAX - INT32_MAX, INT64_MAX - INT32_MAX, INT32_MIN, INT32_MIN},
        {INT64_MAX - INT32_MAX, INT64_MAX - INT32_MAX - 1, INT32_MAX, INT32_MAX},
        {INT64_C(4611686018427387904), INT64_C(2305843009213693952), INT32_MAX,
         UINT32_C(4294967294)},
        {1, 2, 1, 1},
        {-1, 2, 1, -1},
        {1, -2, 1, -1},
        {-1, -2, -1, -1},
        {INT64_C(9223372034707292160), INT64_C(6148914691236517205), 123456789, 185185183},
        {0, 1, INT32_MIN, 0}};

    for (unsigned i = 0; i < sizeof(ratios) / sizeof(*ratios); i++) {
        init(&vm, NULL, 0);
        int64_t answer;
        CHECK(vm_intersection_ratio(&vm, ratios[i].n, ratios[i].b, ratios[i].d, &answer));
        CHECK(answer == ratios[i].answer);
        finish(&vm);
    }
    for (unsigned i = 0; i < 10000; i++) {
        int32_t n = random_value(), b = random_value(), d = random_value();
        if (!d)
            d = 1;
        int64_t expected = cupidimage_pdf_tt_vm_divide((int64_t)n * b, d), answer;
        init(&vm, NULL, 0);
        int ok = vm_intersection_ratio(&vm, n, b, d, &answer);
        if (expected < -(int64_t)UINT32_MAX || expected > UINT32_MAX)
            CHECK(!ok && vm.error == PDF_TT_VM_ARITHMETIC);
        else
            CHECK(ok && answer == expected);
        finish(&vm);
    }
    static const unsigned char code[] = {0xb4, 4, 0, 1, 2, 3, 0x0f};
    for (unsigned kind = 0; kind < 5; kind++) {
        pdf_tt_vm_point points[5] = {{{0, 0}, {1, 2}, 0, 1, 0},
                                     {{128, 128}, {3, 4}, 0, 1, 0},
                                     {{0, 128}, {5, 6}, 0, 1, 0},
                                     {{128, 0}, {7, 8}, 0, 1, 0},
                                     {{99, 77}, {9, 10}, 0, 0, 1}};
        int32_t expected[2] = {64, 64};
        if (kind == 1) { /* Parallel. */
            points[3].current[1] = 256;
            expected[1] = 128;
        }
        if (kind == 2) { /* Nearly parallel is still a genuine intersection. */
            points[1].current[0] = 10000;
            points[1].current[1] = 0;
            points[2].current[1] = 1;
            points[3].current[0] = 10000;
            points[3].current[1] = 2;
            expected[0] = -10000;
            expected[1] = 0;
        }
        init(&vm, NULL, 0);
        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 5, points, 5, 5));
        if (kind == 3) { /* All three zone pointers have independent roles. */
            memcpy(vm.zones[0].points, points, sizeof(points));
            vm.graphics.zone[0] = vm.graphics.zone[2] = 0;
        }
        unsigned char local[sizeof(code)];
        memcpy(local, code, sizeof(code));
        if (kind == 4)
            local[1] = 0; /* Destination aliases an endpoint. */
        CHECK(cupidimage_pdf_tt_vm_run(&vm, local, sizeof(local), 0, 100));
        pdf_tt_vm_point *p = &vm.zones[kind == 3 ? 0 : 1].points[kind == 4 ? 0 : 4];
        CHECK(p->current[0] == expected[0] && p->current[1] == expected[1] && p->touched == 3);
        CHECK(p->original[0] == (kind == 4 ? 1 : 9) && p->original[1] == (kind == 4 ? 2 : 10));
        CHECK(p->on == (kind == 4) && p->end == (kind != 4) && !vm.stack_size);
        finish(&vm);
    }
    for (unsigned bad = 0; bad < 5; bad++) {
        init(&vm, NULL, 0);
        pdf_tt_vm_point points[5] = {0};
        CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, points, 5, 5));
        unsigned char local[sizeof(code)];
        memcpy(local, code, sizeof(code));
        local[bad + 1] = 99;
        CHECK(!cupidimage_pdf_tt_vm_run(&vm, local, sizeof(local), 0, 100));
        CHECK(vm.error == PDF_TT_VM_POINT);
        finish(&vm);
    }
    static const unsigned char bare = 0x0f;
    error(&bare, 1, PDF_TT_VM_STACK, 100);
}
