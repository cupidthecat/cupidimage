static void graphics_tests(void) {
    pdf_tt_vm vm;
    init(&vm, NULL, 0);
    pdf_tt_vm_graphics *g = &vm.graphics;
    CHECK(g->rounding.period == 64 && g->rounding.phase == 0 && g->rounding.threshold == 32 &&
          !g->rounding.off);
    CHECK(g->projection[0] == 16384 && !g->projection[1]);
    CHECK(g->freedom[0] == 16384 && !g->freedom[1]);
    CHECK(g->dual_projection[0] == 16384 && !g->dual_projection[1]);
    CHECK(g->zone[0] == 1 && g->zone[1] == 1 && g->zone[2] == 1);
    CHECK(!g->reference[0] && !g->reference[1] && !g->reference[2]);
    CHECK(g->loop == 1 && g->minimum_distance == 64 && g->cvt_cut_in == 68);
    CHECK(g->delta_base == 9 && g->delta_shift == 3 && !g->single_width_cut_in && g->auto_flip);
    static const unsigned char axis[] = {0x00, 0x0c, 0x0d, 0x03, 0x0c, 0x0d, 0x0e, 0x0d,
                                         0x04, 0x0d, 0x01, 0x02, 0x05, 0x0c, 0x0d};
    static const int32_t expected[] = {0,     16384, 0, 16384, 16384, 0,     0,     16384,
                                       16384, 0,     0, 16384, 0,     16384, 16384, 0};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, axis, sizeof(axis), 0, 100));
    CHECK(vm.stack_size == sizeof(expected) / sizeof(*expected));
    CHECK(!memcmp(vm.stack, expected, sizeof(expected)));
    CHECK(g->dual_projection[0] == 0 && g->dual_projection[1] == 16384);
    static const unsigned char setters[] = {
        0xb0, 7,    0x10, 0xb0, 8,    0x11, 0xb0, 9,    0x12, 0xb0, 0,    0x16, 0xb0,
        1,    0x14, 0xb0, 1,    0x15, 0xb0, 100,  0x17, 0xb0, 25,   0x1a, 0xb0, 16,
        0x1d, 0xb0, 32,   0x1e, 0xb0, 12,   0x5e, 0xb0, 5,    0x5f, 0x4e};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, setters, sizeof(setters), 0, 100));
    CHECK(!vm.stack_size);
    CHECK(g->reference[0] == 7 && g->reference[1] == 8 && g->reference[2] == 9);
    CHECK(g->zone[0] == 0 && g->zone[1] == 1 && g->zone[2] == 1);
    CHECK(g->loop == 100 && g->minimum_distance == 25 && g->cvt_cut_in == 16);
    CHECK(g->delta_base == 12 && g->delta_shift == 5 && g->single_width_cut_in == 32 &&
          !g->auto_flip);
    static const unsigned char finish_set[] = {0xb0, 1, 0x13, 0xb0, 0, 0x17, 0x4d};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, finish_set, sizeof(finish_set), 0, 100));
    CHECK(g->zone[0] == 1 && g->loop == 0 && g->auto_flip);
    finish(&vm);
    const unsigned char pops[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
                                  0x17, 0x1a, 0x1d, 0x1e, 0x5e, 0x5f};
    for (unsigned i = 0; i < sizeof(pops); i++) {
        error(pops + i, 1, PDF_TT_VM_STACK, 100);
        unsigned char negative[] = {0xb8, 0xff, 0xff, pops[i]};
        if (pops[i] == 0x1a || pops[i] == 0x1d || pops[i] == 0x1e)
            result(negative, sizeof(negative), NULL, 0);
        else
            error(negative, sizeof(negative), PDF_TT_VM_PROGRAM, 100);
    }
    for (unsigned op = 0x13; op <= 0x16; op++) {
        unsigned char bad_zone[] = {0xb0, 2, (unsigned char)op};
        error(bad_zone, sizeof(bad_zone), PDF_TT_VM_PROGRAM, 100);
    }
    for (unsigned op = 0x0c; op <= 0x0d; op++) {
        CHECK(cupidimage_pdf_tt_vm_init(&vm, 1, 0, NULL, 0, 0, 0));
        unsigned char code = (unsigned char)op;
        CHECK(!cupidimage_pdf_tt_vm_run(&vm, &code, 1, 0, 100));
        CHECK(vm.error == PDF_TT_VM_STACK);
        finish(&vm);
    }
}
