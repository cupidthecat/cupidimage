static void numeric_tests(void) {
    pdf_tt_vm vm;
    init(&vm, NULL, 0);
    const unsigned modes[] = {0x18, 0x19, 0x3d, 0x7a, 0x7c, 0x7d};
    for (unsigned m = 0; m < sizeof(modes) / sizeof(*modes); m++) {
        CHECK(cupidimage_pdf_tt_vm_numeric_op(&vm, modes[m]));
        for (int n = -4096; n <= 4096; n++) {
            double magnitude = fabs((double)n);
            double rounded;
            switch (m) {
                case 0:
                    rounded = floor(magnitude / 64 + 0.5) * 64;
                    break;
                case 1:
                    rounded = floor(magnitude / 64) * 64 + 32;
                    break;
                case 2:
                    rounded = floor(magnitude / 32 + 0.5) * 32;
                    break;
                case 3:
                    rounded = magnitude;
                    break;
                case 4:
                    rounded = ceil(magnitude / 64) * 64;
                    break;
                default:
                    rounded = floor(magnitude / 64) * 64;
                    break;
            }
            int32_t expected = (int32_t)(n < 0 ? -rounded : rounded);
            for (unsigned color = 0; color < 3; color++) {
                vm.stack_size = 0;
                CHECK(cupidimage_pdf_tt_vm_push(&vm, n));
                CHECK(cupidimage_pdf_tt_vm_numeric_op(&vm, 0x68 + color));
                CHECK(vm.stack_size == 1 && vm.stack[0] == expected);
                CHECK(cupidimage_pdf_tt_vm_numeric_op(&vm, 0x6c + color));
                CHECK(vm.stack[0] == expected);
            }
            for (unsigned parity = 0; parity < 2; parity++) {
                vm.stack_size = 0;
                CHECK(cupidimage_pdf_tt_vm_push(&vm, n));
                CHECK(cupidimage_pdf_tt_vm_numeric_op(&vm, 0x56 + parity));
                int32_t remainder = expected % 128;
                int match = parity ? remainder == 0 : remainder == 64 || remainder == -64;
                CHECK(vm.stack[0] == match);
            }
        }
    }
    for (unsigned diagonal = 0; diagonal < 2; diagonal++)
        for (unsigned selector = 0; selector < 192; selector++) {
            vm.stack_size = 0;
            CHECK(cupidimage_pdf_tt_vm_push(&vm, selector | 0x100));
            CHECK(cupidimage_pdf_tt_vm_numeric_op(&vm, 0x76 + diagonal));
            int fine_period =
                (int)floor((diagonal ? 11585.0 : 16384.0) * pow(2.0, (int)(selector >> 6) - 1));
            int period = (int)floor(fine_period / 256.0);
            int phase = (int)floor(floor(((selector >> 4) & 3) * fine_period / 4.0) / 256.0);
            int threshold = selector & 15;
            threshold = threshold ? (int)floor(trunc((threshold - 4) * fine_period / 8.0) / 256.0)
                                  : (fine_period - 1) / 256;
            for (int n = -1024; n <= 1024; n++) {
                /* Locate the containing threshold interval by enumeration, without
                 * sharing the implementation's signed division/floor calculation. */
                int magnitude = n < 0 ? -n : n;
                int expected = phase;
                while (expected + period - threshold <= magnitude)
                    expected += period;
                if (n < 0)
                    expected = -expected;
                vm.stack_size = 0;
                CHECK(cupidimage_pdf_tt_vm_push(&vm, n));
                CHECK(cupidimage_pdf_tt_vm_numeric_op(&vm, 0x68));
                CHECK(vm.stack[0] == expected);
            }
        }
    finish(&vm);
    for (unsigned i = 0; i < 4000; i++) {
        int32_t a = random_value(), b = random_value();
        int32_t values[] = {a, b};
        unsigned char code[] = {0xb0, 0, 0x45, 0xb0, 1, 0x45, 0};
        for (unsigned op = 0x62; op <= 0x63; op++) {
            if (op == 0x62 && !b)
                continue;
            code[6] = (unsigned char)op;
            int64_t expected = op == 0x62 ? (int64_t)trunc((double)a * 64 / b)
                                          : (int64_t)round((double)a * b / 64);
            init(&vm, values, 2);
            CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
            CHECK(vm.stack_size == 1 && vm.stack[0] == expected);
            finish(&vm);
        }
    }
    const int32_t boundaries[][4] = {{1, 32, 0x63, 1},
                                     {-1, 32, 0x63, -1},
                                     {1, -32, 0x63, -1},
                                     {-1, -32, 0x63, 1},
                                     {1, 31, 0x63, 0},
                                     {-1, 31, 0x63, 0},
                                     {INT32_MIN, 64, 0x63, INT32_MIN},
                                     {INT32_MAX, 64, 0x63, INT32_MAX},
                                     {INT32_MIN, 64, 0x62, INT32_MIN},
                                     {INT32_MAX, 64, 0x62, INT32_MAX},
                                     {-1, 100, 0x62, 0},
                                     {1, -100, 0x62, 0},
                                     {100, -3, 0x62, -2133}};
    for (unsigned i = 0; i < sizeof(boundaries) / sizeof(*boundaries); i++) {
        init(&vm, boundaries[i], 2);
        unsigned char code[] = {0xb0, 0, 0x45, 0xb0, 1, 0x45, (unsigned char)boundaries[i][2]};
        CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
        CHECK(vm.stack_size == 1 && vm.stack[0] == boundaries[i][3]);
        finish(&vm);
    }
    const int32_t failures[][3] = {
        {0, 0, 0x62},           {INT32_MIN, -64, 0x62},       {INT32_MAX, 1, 0x62},
        {INT32_MIN, -64, 0x63}, {INT32_MAX, INT32_MAX, 0x63}, {INT32_MIN, INT32_MIN, 0x63}};
    for (unsigned i = 0; i < sizeof(failures) / sizeof(*failures); i++) {
        init(&vm, failures[i], 2);
        unsigned char code[] = {0xb0, 0, 0x45, 0xb0, 1, 0x45, (unsigned char)failures[i][2]};
        CHECK(!cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
        CHECK(vm.error == PDF_TT_VM_ARITHMETIC);
        finish(&vm);
    }
    for (unsigned op = 0x76; op <= 0x77; op++)
        for (unsigned selector = 192; selector < 256; selector++) {
            unsigned char code[] = {0xb0, (unsigned char)selector, (unsigned char)op};
            error(code, sizeof(code), PDF_TT_VM_PROGRAM, 100);
        }
    const unsigned char underflows[] = {0x56, 0x57, 0x62, 0x63, 0x68, 0x69,
                                        0x6a, 0x6c, 0x6d, 0x6e, 0x76, 0x77};
    for (unsigned i = 0; i < sizeof(underflows); i++)
        error(underflows + i, 1, PDF_TT_VM_STACK, 100);
    const unsigned char reserved[] = {0x6b, 0x6f};
    for (unsigned i = 0; i < sizeof(reserved); i++)
        error(reserved + i, 1, PDF_TT_VM_UNSUPPORTED, 100);

    for (unsigned op = 0x18; op <= 0x18; op++) {
        int32_t extremes[] = {INT32_MAX};
        init(&vm, extremes, 1);
        unsigned char code[] = {(unsigned char)op, 0xb0, 0, 0x45, 0x68};
        CHECK(!cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
        CHECK(vm.error == PDF_TT_VM_ARITHMETIC);
        finish(&vm);
    }
    static const unsigned char set_half[] = {0x19};
    static const unsigned char round_zero[] = {0xb0, 0, 0x68};
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_run(&vm, set_half, sizeof(set_half), 0, 100));
    CHECK(cupidimage_pdf_tt_vm_run(&vm, round_zero, sizeof(round_zero), 0, 100));
    CHECK(vm.stack_size == 1 && vm.stack[0] == 32);
    finish(&vm);
}
