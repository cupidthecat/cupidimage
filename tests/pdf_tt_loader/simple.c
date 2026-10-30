static void simple_loader_tests(void) {
    pdf_tt_font font = fixture_font();
    pdf_tt_hint hint;
    for (unsigned size = 1; size <= 96; size++) {
        CHECK(cupidimage_pdf_tt_hint_init(&hint, &font, (int32_t)size * 64, (int32_t)size * 64, 0,
                                          1000000));
        for (unsigned glyph = 0; glyph < 8; glyph++) {
            if (glyph == 4 || glyph == 5)
                continue;
            CHECK(cupidimage_pdf_tt_hint_glyph(&hint, glyph));
            pdf_tt_vm_zone *zone = &hint.vm.zones[1];
            CHECK(zone->count == zone->outline_count + 4);
            CHECK(zone->design_scale == hint.vm.size.scale);
            for (unsigned i = 0; i < zone->outline_count; i++) {
                CHECK(zone->design[i][0] == (int32_t)hint.raw.points[i].x);
                CHECK(zone->design[i][1] == (int32_t)hint.raw.points[i].y);
                CHECK(zone->points[i].touched == 0);
                CHECK(zone->points[i].on == hint.raw.points[i].on);
                CHECK(zone->points[i].end == hint.raw.points[i].end);
                for (unsigned axis = 0; axis < 2; axis++) {
                    int64_t product = (int64_t)zone->design[i][axis] * hint.vm.size.scale;
                    int32_t expected = (int32_t)(product < 0 ? -((-product + 32768) / 65536)
                                                             : (product + 32768) / 65536);
                    CHECK(zone->points[i].original[axis] == expected);
                    CHECK(zone->points[i].current[axis] == expected);
                }
            }
            unsigned n = zone->outline_count;
            CHECK(zone->design[n + 1][0] - zone->design[n][0] == 1000);
            CHECK(zone->design[n + 2][1] == 1000 && zone->design[n + 3][1] == -200);
            CHECK(zone->points[n].current[0] % 64 == 0 && zone->points[n + 1].current[0] % 64 == 0);
            CHECK(zone->points[n + 2].current[1] % 64 == 0 &&
                  zone->points[n + 3].current[1] % 64 == 0);
        }
        finish(&hint);
    }
    allocation_count = 0;
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &font, 1024, 1024, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, 1));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, 2));
    size_t allocations = allocation_count;
    finish(&hint);
    for (size_t failure = 1; failure <= allocations; failure++) {
        allocation_count = 0;
        fail_at = failure;
        int ok = cupidimage_pdf_tt_hint_init(&hint, &font, 1024, 1024, 0, 1000000);
        if (ok)
            ok = cupidimage_pdf_tt_hint_glyph(&hint, 1);
        if (ok)
            ok = cupidimage_pdf_tt_hint_glyph(&hint, 2);
        CHECK(!ok && hint.vm.error == PDF_TT_VM_MEMORY);
        fail_at = 0;
        finish(&hint);
    }
    for (unsigned invalid = 0; invalid < 5; invalid++) {
        pdf_tt_font changed = font;
        if (invalid == 0)
            changed.maxp.length = 6;
        if (invalid == 1)
            changed.cvt = (pdf_tt_table){test_truetype_font, 1};
        if (invalid < 2) {
            CHECK(!cupidimage_pdf_tt_hint_init(&hint, &changed, 1024, 1024, 0, 1000000));
            CHECK(hint.vm.error == PDF_TT_VM_PROGRAM);
        } else {
            CHECK(cupidimage_pdf_tt_hint_init(&hint, &changed, 1024, 1024, 0, 1000000));
            if (invalid == 4)
                hint.glyph_budget = 0;
            CHECK(!cupidimage_pdf_tt_hint_glyph(&hint, invalid == 2 ? 8 : invalid == 3 ? 999 : 1));
            CHECK(hint.vm.error == (invalid == 2   ? PDF_TT_VM_PROGRAM
                                    : invalid == 3 ? PDF_TT_VM_PROGRAM
                                                   : PDF_TT_VM_BUDGET));
        }
        finish(&hint);
    }
    unsigned char vhea[36] = {0}, vmtx[18] = {0};
    write16(vhea, 1);
    write16(vhea + 34, 1);
    write16(vmtx, 1200);
    for (unsigned i = 0; i < 8; i++)
        write16(vmtx + (i ? 4 + (i - 1) * 2 : 2), 100 + i);
    pdf_tt_font vertical = font;
    vertical.vhea = (pdf_tt_table){vhea, sizeof(vhea)};
    vertical.vmtx = (pdf_tt_table){vmtx, sizeof(vmtx)};
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &vertical, 1024, 1024, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, 1));
    unsigned n = hint.vm.zones[1].outline_count;
    CHECK(hint.vm.zones[1].design[n + 2][1] == 701);
    CHECK(hint.vm.zones[1].design[n + 3][1] == -499);
    finish(&hint);
    for (unsigned invalid = 0; invalid < 4; invalid++) {
        pdf_tt_font changed = vertical;
        if (invalid == 0)
            changed.vhea.length = 35;
        if (invalid == 1)
            changed.vmtx.length = 17;
        if (invalid == 2)
            write16(vhea + 34, 0);
        if (invalid == 3)
            write16(vhea + 34, 9);
        CHECK(cupidimage_pdf_tt_hint_init(&hint, &changed, 1024, 1024, 0, 1000000));
        CHECK(!cupidimage_pdf_tt_hint_glyph(&hint, 1));
        CHECK(hint.vm.error == PDF_TT_VM_PROGRAM);
        finish(&hint);
    }
    write16(vhea + 34, 1);
    write16(vhea + 2, 0x1000);
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &vertical, 1024, 1024, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, 1));
    finish(&hint);
    for (unsigned invalid = 0; invalid < 2; invalid++) {
        write16(vhea, invalid ? 1 : 2);
        write16(vhea + 2, invalid ? 1 : 0);
        CHECK(cupidimage_pdf_tt_hint_init(&hint, &vertical, 1024, 1024, 0, 1000000));
        CHECK(!cupidimage_pdf_tt_hint_glyph(&hint, 1));
        CHECK(hint.vm.error == PDF_TT_VM_PROGRAM);
        finish(&hint);
    }
    pdf_tt_font fallback = font;
    fallback.os2 = (pdf_tt_table){0};
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &fallback, 1024, 1024, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, 1));
    n = hint.vm.zones[1].outline_count;
    CHECK(hint.vm.zones[1].design[n + 2][1] == 1000 && hint.vm.zones[1].design[n + 3][1] == -200);
    finish(&hint);

    unsigned char legacy_os2[68] = {0}, fallback_hhea[36];
    memcpy(fallback_hhea, font.hhea.data, sizeof(fallback_hhea));
    write16(fallback_hhea + 4, 777);
    write16(fallback_hhea + 6, (unsigned)-111);
    fallback = font;
    fallback.os2 = (pdf_tt_table){legacy_os2, sizeof(legacy_os2)};
    fallback.hhea = (pdf_tt_table){fallback_hhea, sizeof(fallback_hhea)};
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &fallback, 1024, 1024, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, 1));
    n = hint.vm.zones[1].outline_count;
    CHECK(hint.vm.zones[1].design[n + 2][1] == 777 && hint.vm.zones[1].design[n + 3][1] == -111);
    finish(&hint);
    for (unsigned invalid = 0; invalid < 2; invalid++) {
        fallback.os2.length = invalid ? 68 : 67;
        write16(legacy_os2, invalid ? 1 : 0);
        CHECK(cupidimage_pdf_tt_hint_init(&hint, &fallback, 1024, 1024, 0, 1000000));
        CHECK(!cupidimage_pdf_tt_hint_glyph(&hint, 1));
        CHECK(hint.vm.error == PDF_TT_VM_PROGRAM);
        finish(&hint);
    }
    unsigned char maxp[32];
    memcpy(maxp, font.maxp.data, sizeof(maxp));
    for (unsigned invalid = 0; invalid < 2; invalid++) {
        pdf_tt_font changed = font;
        changed.maxp = (pdf_tt_table){maxp, sizeof(maxp)};
        write16(maxp + 14, invalid ? 3 : 0);
        CHECK(!cupidimage_pdf_tt_hint_init(&hint, &changed, 1024, 1024, 0, 1000000));
        CHECK(hint.vm.error == PDF_TT_VM_PROGRAM);
        finish(&hint);
    }
    write16(maxp + 14, 2);
    write16(maxp + 16, 2);
    write16(maxp + 18, 4);
    write16(maxp + 20, 2);
    write16(maxp + 24, 64);
    static const unsigned char cvt[] = {0, 64, 0, 128}, stream[] = {0xb0, 1, 0x21};
    pdf_tt_font programmed = font;
    programmed.maxp = (pdf_tt_table){maxp, sizeof(maxp)};
    programmed.cvt = (pdf_tt_table){cvt, sizeof(cvt)};
    programmed.fpgm = programmed.prep = (pdf_tt_table){stream, sizeof(stream)};
    CHECK(!cupidimage_pdf_tt_hint_init(&hint, &programmed, 1024, 1024, 0, 5));
    CHECK(hint.vm.error == PDF_TT_VM_BUDGET);
    finish(&hint);
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &programmed, 1024, 1024, 0, 6));
    CHECK(hint.vm.work_left == 0 && hint.vm.prepared && hint.vm.cvt[0] == 66);
    finish(&hint);
    allocation_count = 0;
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &programmed, 1024, 1024, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, 1));
    size_t prepared_allocations = allocation_count;
    finish(&hint);
    for (size_t failure = 1; failure <= prepared_allocations; failure++) {
        allocation_count = 0;
        fail_at = failure;
        int ok = cupidimage_pdf_tt_hint_init(&hint, &programmed, 1024, 1024, 0, 1000000);
        if (ok)
            ok = cupidimage_pdf_tt_hint_glyph(&hint, 1);
        CHECK(!ok && hint.vm.error == PDF_TT_VM_MEMORY);
        fail_at = 0;
        finish(&hint);
    }
    unsigned char hmtx[18];
    memcpy(hmtx, font.hmtx.data, sizeof(hmtx));
    write16(hmtx + 4, 17);
    programmed.hmtx = (pdf_tt_table){hmtx, sizeof(hmtx)};
    static const unsigned char inhibit[] = {0xb1, 1, 1, 0x8e};
    programmed.prep = (pdf_tt_table){inhibit, sizeof(inhibit)};
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &programmed, 1024, 1024, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, 1));
    n = hint.vm.zones[1].outline_count;
    CHECK(hint.vm.zones[1].points[n].current[0] == -17);
    CHECK(hint.vm.zones[1].points[n].original[0] == -17);
    finish(&hint);
    unsigned char empty[16] = {0}, locations[4] = {0, 0, 0, 8};
    write16(empty + 10, 4);
    memcpy(empty + 12, (unsigned char[]){0xb1, 0, 97, 0x48}, 4);
    programmed = font;
    programmed.maxp = (pdf_tt_table){maxp, sizeof(maxp)};
    programmed.glyf = (pdf_tt_table){empty, sizeof(empty)};
    programmed.loca = (pdf_tt_table){locations, sizeof(locations)};
    programmed.glyphs = 1;
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &programmed, 1024, 1024, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, 0));
    CHECK(hint.vm.zones[1].count == 4 && hint.vm.zones[1].outline_count == 0);
    CHECK(hint.vm.zones[1].points[0].current[0] == 97);
    finish(&hint);
    for (unsigned length = 0; length < sizeof(empty); length++) {
        programmed.glyf.length = length;
        /* Long loca offsets permit testing odd truncation lengths. */
        unsigned char long_locations[8] = {0, 0, 0, 0, 0, 0, 0, (unsigned char)length};
        programmed.location_size = 4;
        programmed.loca = (pdf_tt_table){long_locations, sizeof(long_locations)};
        CHECK(cupidimage_pdf_tt_hint_init(&hint, &programmed, 1024, 1024, 0, 1000000));
        int ok = cupidimage_pdf_tt_hint_glyph(&hint, 0);
        CHECK(ok == (length == 0 || length == 10));
        if (!ok)
            CHECK(hint.vm.error == PDF_TT_VM_PROGRAM);
        finish(&hint);
    }
    unsigned contours = 32767;
    size_t length = 12 + contours * 2 + ((contours + 255) / 256) * 2;
    unsigned char *many = calloc(length, 1);
    CHECK(many != NULL);
    write16(many, contours);
    for (unsigned i = 0; i < contours; i++)
        write16(many + 10 + i * 2, i);
    size_t at = 12 + contours * 2;
    for (unsigned left = contours; left;) {
        unsigned run = left < 256 ? left : 256;
        many[at++] = 0x39;
        many[at++] = (unsigned char)(run - 1);
        left -= run;
    }
    CHECK(at == length);
    pdf_tt_table view = {many, length}, instructions;
    pdf_tt_outline raw = {0};
    CHECK(cupidimage_pdf_tt_simple_points(&view, &raw, &instructions) == 1);
    CHECK(raw.count == contours && !instructions.length);
    for (unsigned i = 0; i < raw.count; i++)
        CHECK(raw.points[i].end && raw.points[i].on);
    cupidimage_pdf_tt_outline_free(&raw);
    CHECK(live_count == 0);
    unsigned char many_locations[4] = {0};
    write16(many_locations + 2, (unsigned)(length / 2));
    programmed = font;
    programmed.glyphs = 1;
    programmed.glyf = view;
    programmed.loca = (pdf_tt_table){many_locations, sizeof(many_locations)};
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &programmed, 1024, 1024, 0, length + 4));
    allocation_count = 0;
    CHECK(!cupidimage_pdf_tt_hint_glyph(&hint, 0));
    CHECK(hint.vm.error == PDF_TT_VM_BUDGET && allocation_count == 0);
    finish(&hint);
    free(many);
    allocations += prepared_allocations;
    printf("TrueType native loader: %u checks, %zu allocation failures, 0 failures\n", checks,
           allocations);
}
