static void profile_tests(void) {
    pdf_tt_vm vm;
    CHECK(!cupidimage_pdf_tt_vm_set_profile(NULL, PDF_TT_PROFILE_MODERN, 0));
    for (unsigned profile = 0; profile < 2; profile++) {
        for (unsigned gray = 0; gray < 2; gray++) {
            for (unsigned full = 0; full < 2; full++) {
                init(&vm, NULL, 0);
                CHECK(cupidimage_pdf_tt_vm_set_profile(&vm, profile, (int)full));
                CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 1000, 1024, 768, (int)gray, NULL));
                for (unsigned stage = 0; stage < 3; stage++) {
                    vm.stage = (pdf_tt_vm_stage)stage;
                    for (unsigned flags = 0; flags < 4; flags++) {
                        vm.size.transform_flags = flags;
                        for (unsigned selector = 0; selector < 8192; selector++) {
                            unsigned expected = (selector & 1) ? (profile ? 40 : 35) : 0;
                            if ((selector & 2) && (flags & 1))
                                expected |= 1u << 8;
                            if ((selector & 4) && (flags & 2))
                                expected |= 1u << 9;
                            if (!profile && gray && (selector & 32))
                                expected |= 1u << 12;
                            if (profile && (gray || stage == PDF_TT_VM_FONT)) {
                                if (selector & 64)
                                    expected |= 1u << 13;
                                if (selector & 1024)
                                    expected |= 1u << 17;
                                if (selector & 2048)
                                    expected |= 1u << 18;
                                if (selector & 4096)
                                    expected |= 1u << 19;
                            }
                            CHECK(cupidimage_pdf_tt_vm_profile_info(&vm, selector) == expected);
                        }
                    }
                    for (unsigned control = 0; control < 8; control++) {
                        vm.graphics.instruction_control = control;
                        cupidimage_pdf_tt_vm_profile_begin(&vm);
                        CHECK(
                            vm.compatibility ==
                            (profile && gray && !full && stage == PDF_TT_VM_GLYPH && !(control & 4)
                                 ? 4u
                                 : 0u));
                    }
                }
                finish(&vm);
            }
        }
    }
    for (unsigned profile = 0; profile < 2; profile++) {
        for (unsigned gray = 0; gray < 2; gray++) {
            for (unsigned full = 0; full < 2; full++) {
                const pdf_tt_vm_point input[] = {{{31, 35}, {31, 35}, 0, 1, 1, 0}};
                init(&vm, NULL, 0);
                CHECK(cupidimage_pdf_tt_vm_set_profile(&vm, profile, (int)full));
                CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 1000, 1024, 768, (int)gray, NULL));
                CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, input, 1, 1));
                const unsigned char move[] = {0x01, 0xb1, 0, 97, 0x48, 0x00, 0xb1, 0, 97, 0x48};
                CHECK(cupidimage_pdf_tt_vm_run(&vm, move, sizeof(move), PDF_TT_VM_GLYPH, 100));
                CHECK(vm.zones[1].points[0].current[0] == (profile && gray && !full ? 31 : 97));
                CHECK(vm.zones[1].points[0].current[1] == 97);
                CHECK(vm.zones[1].points[0].touched == 3);
                finish(&vm);
            }
        }
    }
    /* Suppression changes movement, not stack validation or work accounting. */
    for (unsigned composite = 0; composite < 2; composite++) {
        for (unsigned touched = 0; touched < 2; touched++) {
            for (unsigned done = 0; done < 2; done++) {
                for (unsigned delta = 0; delta < 2; delta++) {
                    pdf_tt_vm_point input[] = {{{31, 35}, {31, 35}, touched ? 2 : 0, 1, 1, 0}};
                    init(&vm, NULL, 0);
                    CHECK(cupidimage_pdf_tt_vm_set_profile(&vm, PDF_TT_PROFILE_MODERN, 0));
                    CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 1000, 16 * 64, 768, 1, NULL));
                    CHECK(cupidimage_pdf_tt_vm_set_zones(&vm, 0, input, 1, 1));
                    vm.composite = composite;
                    unsigned char code[] = {0x00, 0x30, 0x31, 0xb1, 0, 8, 0x38};
                    unsigned char deltap[] = {0x00, 0x30, 0x31, 0xb2, 0x78, 0, 1, 0x5d};
                    if (!done) {
                        code[1] = code[2] = 0x00;
                        deltap[1] = deltap[2] = 0x00;
                    }
                    CHECK(cupidimage_pdf_tt_vm_run(&vm, delta ? deltap : code,
                                                   delta ? sizeof(deltap) : sizeof(code),
                                                   PDF_TT_VM_GLYPH, 100));
                    CHECK(vm.zones[1].points[0].current[1] ==
                          (!done && (touched || composite) ? 43 : 35));
                    CHECK(vm.stack_size == 0);
                    finish(&vm);
                }
            }
        }
    }
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_profile(&vm, PDF_TT_PROFILE_MODERN, 0));
    CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 1000, 1024, 768, 1, NULL));
    const unsigned char underflow[] = {0x30, 0x31, 0x80};
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, underflow, sizeof(underflow), PDF_TT_VM_GLYPH, 100));
    CHECK(vm.error == PDF_TT_VM_STACK);
    finish(&vm);
    init(&vm, NULL, 0);
    CHECK(!cupidimage_pdf_tt_vm_set_profile(&vm, 2, 0) && vm.error == PDF_TT_VM_PROGRAM);
    finish(&vm);
    init(&vm, NULL, 0);
    CHECK(!cupidimage_pdf_tt_vm_set_profile(&vm, 1, 2) && vm.error == PDF_TT_VM_PROGRAM);
    finish(&vm);
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_set_size(&vm, 1000, 1024, 768, 1, NULL));
    CHECK(!cupidimage_pdf_tt_vm_set_profile(&vm, 1, 0) && vm.error == PDF_TT_VM_PROGRAM);
    finish(&vm);
}
