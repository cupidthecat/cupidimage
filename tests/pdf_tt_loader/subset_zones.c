static void subset_zone_tests(void) {
    unsigned char data[sizeof(test_truetype_font)];
    memcpy(data, test_truetype_font, sizeof(data));
    pdf_tt_font original = fixture_font(), font;
    size_t offset = (size_t)(original.maxp.data - test_truetype_font);
    write16(data + offset + 14, 0);
    write16(data + offset + 24, 16);
    uint32_t *ends = NULL;
    CHECK(cupidimage_pdf_tt_init(&font, data, sizeof(data)));
    CHECK(!font.recovered_zero_zones);
    pdf_tt_hint hint;
    CHECK(!cupidimage_pdf_tt_hint_init(&hint, &font, 1024, 1024, 0, 1000000));
    CHECK(hint.vm.error == PDF_TT_VM_PROGRAM);
    finish(&hint);
    CHECK(cupidimage_pdf_tt_init_subset(&font, data, sizeof(data), &ends));
    CHECK(font.recovered_zero_zones && !ends);
    static const unsigned char prep[] = {0xb1, 48, 0, 0xb8, 1, 36, 0x18, 0x85, 0x8d, 0x1d};
    font.prep = (pdf_tt_table){prep, sizeof(prep)};
    for (unsigned gray = 0; gray < 2; gray++) {
        for (unsigned size = 1; size <= 96; size++) {
            CHECK(cupidimage_pdf_tt_hint_init(&hint, &font, size * 64, size * 64, gray, 1000000));
            CHECK(hint.vm.prepared_graphics.scan_flags == 292 &&
                  hint.vm.prepared_graphics.scan_type == 0 &&
                  hint.vm.prepared_graphics.cvt_cut_in == 48);
            CHECK(hint.vm.zones[0].count == 0);
            CHECK(cupidimage_pdf_tt_hint_glyph(&hint, 1));
            CHECK(hint.vm.zones[1].outline_count == 4);
            finish(&hint);
        }
    }
    size_t total = 0;
    for (size_t failure = 0; failure <= total; failure++) {
        allocation_count = 0;
        fail_at = failure;
        int ok = cupidimage_pdf_tt_hint_init(&hint, &font, 1024, 1024, 0, 1000000);
        if (ok)
            ok = cupidimage_pdf_tt_hint_glyph(&hint, 1);
        if (!failure)
            total = allocation_count;
        CHECK(ok == !failure);
        fail_at = 0;
        finish(&hint);
    }
    /* A recovered declaration does not create addressable twilight points. */
    static const unsigned char twilight[] = {0xb0, 0, 0x16, 0xb0, 0, 0x2f};
    font.prep = (pdf_tt_table){twilight, sizeof(twilight)};
    CHECK(!cupidimage_pdf_tt_hint_init(&hint, &font, 1024, 1024, 0, 1000000));
    CHECK(hint.vm.error != PDF_TT_VM_OK);
    finish(&hint);
    for (unsigned zones = 0; zones <= 3; zones++) {
        write16(data + offset + 14, zones);
        write16(data + offset + 16, zones == 0 ? 1 : 0);
        CHECK(cupidimage_pdf_tt_init_subset(&font, data, sizeof(data), &ends));
        CHECK(!font.recovered_zero_zones && !ends);
        int ok = cupidimage_pdf_tt_hint_init(&hint, &font, 1024, 1024, 0, 1000000);
        CHECK(ok == (zones == 1 || zones == 2));
        finish(&hint);
    }
}
