static void nonuniform_loader_tests(void) {
    pdf_tt_font font = fixture_font();
    unsigned char head[54];
    memcpy(head, font.head.data, sizeof(head));
    font.head = (pdf_tt_table){head, sizeof(head)};
    const int32_t sizes[][2] = {
        {13 * 64 + 17, 37 * 64 + 47}, {37 * 64 + 11, 13 * 64 + 31}, {9 * 64 + 32, 9 * 64 + 20}};
    for (unsigned force = 0; force < 2; force++) {
        write16(head + 16, force ? 8 : 0);
        for (unsigned pair = 0; pair < sizeof(sizes) / sizeof(*sizes); pair++) {
            pdf_tt_hint hint;
            CHECK(cupidimage_pdf_tt_hint_init_xy(&hint, &font, sizes[pair][0], sizes[pair][1], 999,
                                                 1, PDF_TT_VM_ROTATED | PDF_TT_VM_STRETCHED,
                                                 1000000));
            CHECK(hint.vm.size.transform_flags == 3 && hint.vm.size.point_size == 999);
            for (unsigned axis = 0; axis < 2; axis++) {
                int32_t pixels = force ? (sizes[pair][axis] + 32) / 64 * 64 : sizes[pair][axis];
                int32_t expected =
                    (int32_t)(((int64_t)pixels * 65536 + font.units / 2) / font.units);
                CHECK(hint.vm.size.axis_scale[axis] == expected);
            }
            for (unsigned glyph = 0; glyph < 8; glyph++) {
                if (glyph == 4 || glyph == 5)
                    continue;
                CHECK(cupidimage_pdf_tt_hint_glyph(&hint, glyph));
                pdf_tt_vm_zone *zone = &hint.vm.zones[1];
                CHECK(zone->design_scale == hint.vm.size.axis_scale[0] &&
                      zone->design_scale_y == hint.vm.size.axis_scale[1]);
                for (unsigned i = 0; i < zone->count; i++)
                    for (unsigned axis = 0; axis < 2; axis++) {
                        int64_t product =
                            (int64_t)zone->design[i][axis] * hint.vm.size.axis_scale[axis];
                        int32_t expected = (int32_t)(product < 0 ? -((-product + 32768) / 65536)
                                                                 : (product + 32768) / 65536);
                        CHECK(zone->points[i].original[axis] == expected);
                        if (i < zone->outline_count)
                            CHECK(zone->points[i].current[axis] == expected);
                    }
            }
            finish(&hint);
        }
    }
    for (unsigned scaled = 0; scaled < 2; scaled++)
        for (unsigned rounded = 0; rounded < 2; rounded++)
            for (int sign = -1; sign <= 1; sign += 2) {
                pdf_tt_hint hint;
                CHECK(cupidimage_pdf_tt_hint_init_xy(&hint, &font, 13 * 64, 37 * 64, 768, 0, 0,
                                                     1000000));
                pdf_tt_component component = {.flags =
                                                  (scaled ? 0x800 : 0x1000) | (rounded ? 4 : 0),
                                              .argument = {sign * 100, sign * 200},
                                              .matrix = {16384, 8192, -4096, 16384}};
                int32_t offset[2];
                CHECK(tt_hint_offset(&hint, &component, offset));
                for (unsigned axis = 0; axis < 2; axis++) {
                    double design =
                        sign * (scaled ? (axis ? 250.0 : 50.0) : (axis ? 200.0 : 100.0));
                    double expected = round(design * hint.vm.size.axis_scale[axis] / 65536);
                    if (rounded)
                        expected = floor((expected + 32) / 64) * 64;
                    CHECK(offset[axis] == expected);
                }
                finish(&hint);
            }
    for (unsigned bad = 0; bad < 6; bad++) {
        pdf_tt_hint hint;
        pdf_tt_font changed = font;
        int32_t x = 1024, y = 768, points = 768;
        unsigned flags = 0;
        if (bad == 0)
            changed.head.length = 53;
        if (bad == 1)
            changed.head.data = NULL;
        if (bad == 2)
            x = 0;
        if (bad == 3)
            y = INT32_MAX;
        if (bad == 4)
            points = 0;
        if (bad == 5)
            flags = 4;
        CHECK(!cupidimage_pdf_tt_hint_init_xy(&hint, &changed, x, y, points, 0, flags, 1000000));
        CHECK(hint.vm.error == (bad < 2 ? PDF_TT_VM_PROGRAM : PDF_TT_VM_SIZE));
        finish(&hint);
    }
}
