static void device_shape_tests(void) {
    pdf_tt_font font = fixture_font();
    const int32_t matrices[][4] = {{65536, 0, 0, 65536},          {-65536, 0, 0, 65536},
                                   {0, 65536, -65536, 0},         {46341, 46341, -46341, 46341},
                                   {98304, 32768, -16384, 49152}, {0, 0, 0, 0}};
    const int32_t offset[] = {17, -29};
    for (unsigned m = 0; m < sizeof(matrices) / sizeof(*matrices); m++) {
        pdf_tt_hint hint;
        CHECK(cupidimage_pdf_tt_hint_init_xy(&hint, &font, 13 * 64, 37 * 64, 768, 0, 0, 1000000));
        CHECK(cupidimage_pdf_tt_hint_glyph(&hint, 1));
        pdf_tt_vm_zone *zone = &hint.vm.zones[1];
        size_t work = hint.vm.work_left;
        pdf_tt_hint_shape shape = {0};
        CHECK(cupidimage_pdf_tt_hint_shape_build(&hint, matrices[m], offset, &shape));
        CHECK(shape.count == zone->outline_count && hint.vm.work_left == work - shape.count);
        for (unsigned i = 0; i < shape.count; i++) {
            double x =
                (double)zone->points[i].current[0] - zone->points[zone->outline_count].current[0];
            double y = zone->points[i].current[1];
            double a =
                round(x * matrices[m][0] / 65536) + round(y * matrices[m][2] / 65536) + offset[0];
            double b =
                round(x * matrices[m][1] / 65536) + round(y * matrices[m][3] / 65536) + offset[1];
            CHECK(shape.points[i].x == a / 64 && shape.points[i].y == b / 64);
            CHECK(shape.points[i].on == zone->points[i].on &&
                  shape.points[i].end == zone->points[i].end);
        }
        cupidimage_pdf_tt_hint_shape_free(&shape);
        CHECK(!shape.points && !shape.count);
        finish(&hint);
    }
    for (unsigned bad = 0; bad < 5; bad++) {
        pdf_tt_hint hint;
        CHECK(cupidimage_pdf_tt_hint_init(&hint, &font, 1024, 1024, 0, 1000000));
        CHECK(cupidimage_pdf_tt_hint_glyph(&hint, 1));
        pdf_tt_hint_shape shape = {0};
        int32_t matrix[] = {65536, 0, 0, 65536};
        size_t live = live_count;
        if (bad == 0)
            hint.vm.work_left = 0;
        if (bad == 1)
            fail_at = allocation_count + 1;
        if (bad == 2) {
            hint.vm.zones[1].points[0].current[0] = INT32_MAX;
            matrix[0] = INT32_MAX;
        }
        CHECK(!cupidimage_pdf_tt_hint_shape_build(&hint, bad == 3 ? NULL : matrix, offset,
                                                  bad == 4 ? NULL : &shape));
        CHECK(hint.vm.error == (bad == 0   ? PDF_TT_VM_BUDGET
                                : bad == 1 ? PDF_TT_VM_MEMORY
                                : bad == 2 ? PDF_TT_VM_ARITHMETIC
                                           : PDF_TT_VM_PROGRAM));
        CHECK(!shape.points && !shape.count && live_count == live);
        fail_at = 0;
        finish(&hint);
    }
    pdf_tt_hint owned;
    CHECK(cupidimage_pdf_tt_hint_init(&owned, &font, 1024, 1024, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&owned, 1));
    pdf_tt_hint_shape retained = {0};
    CHECK(cupidimage_pdf_tt_hint_shape_build(&owned, matrices[0], offset, &retained));
    CHECK(retained.count > 0);
    double retained_x = retained.points[0].x;
    cupidimage_pdf_tt_hint_free(&owned);
    CHECK(live_count == 1 && retained.points[0].x == retained_x);
    cupidimage_pdf_tt_hint_shape_free(&retained);
    CHECK(live_count == 0);

    pdf_tt_hint hint;
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &font, 1024, 1024, 0, 1000000));
    pdf_tt_vm_point phantoms[4] = {0};
    CHECK(cupidimage_pdf_tt_vm_begin_glyph(&hint.vm, phantoms, 4, 0));
    hint.vm.work_left = 0;
    pdf_tt_hint_shape shape = {0};
    CHECK(cupidimage_pdf_tt_hint_shape_build(&hint, matrices[0], offset, &shape));
    CHECK(!shape.count && !shape.points);
    cupidimage_pdf_tt_hint_shape_free(&shape);
    finish(&hint);
}
