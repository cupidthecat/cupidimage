typedef struct {
    pdf_tt_font font;
    unsigned char data[65536], locations[33 * 4], metrics[32 * 4], profile[32];
    unsigned glyphs;
    size_t length;
} composite_fixture;

static void composite_u32(unsigned char *p, size_t value) {
    p[0] = (unsigned char)(value >> 24);
    p[1] = (unsigned char)(value >> 16);
    p[2] = (unsigned char)(value >> 8);
    p[3] = (unsigned char)value;
}

static void composite_init(composite_fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->font = fixture_font();
    memcpy(fixture->profile, fixture->font.maxp.data, sizeof(fixture->profile));
    write16(fixture->profile + 24, 64);
    fixture->font.maxp = (pdf_tt_table){fixture->profile, sizeof(fixture->profile)};
    fixture->font.glyf = (pdf_tt_table){fixture->data, 0};
    fixture->font.loca = (pdf_tt_table){fixture->locations, sizeof(fixture->locations)};
    fixture->font.location_size = 4;
    fixture->font.hmtx = (pdf_tt_table){fixture->metrics, sizeof(fixture->metrics)};
}

static unsigned composite_add(composite_fixture *fixture, const unsigned char *data,
                              size_t length) {
    CHECK(fixture->glyphs < 32 && length <= sizeof(fixture->data) - fixture->length);
    unsigned index = fixture->glyphs++;
    composite_u32(fixture->locations + index * 4, fixture->length);
    if (length)
        memcpy(fixture->data + fixture->length, data, length);
    fixture->length += length;
    composite_u32(fixture->locations + fixture->glyphs * 4, fixture->length);
    fixture->font.glyphs = fixture->font.hmetrics = fixture->glyphs;
    fixture->font.glyf.length = fixture->length;
    write16(fixture->profile + 4, fixture->glyphs);
    write16(fixture->metrics + index * 4, 1000);
    return index;
}

static unsigned composite_simple(composite_fixture *fixture, const unsigned char *code,
                                 size_t length) {
    pdf_tt_font original = fixture_font();
    pdf_tt_table view;
    CHECK(cupidimage_pdf_tt_glyph_data(&original, 1, &view));
    unsigned char data[512];
    CHECK(view.length + length <= sizeof(data));
    memcpy(data, view.data, 12);
    write16(data + 12, (unsigned)length);
    if (length)
        memcpy(data + 14, code, length);
    memcpy(data + 14 + length, view.data + 14, view.length - 14);
    return composite_add(fixture, data, view.length + length);
}

static unsigned composite_parent(composite_fixture *fixture, const pdf_tt_component *records,
                                 unsigned count, const unsigned char *code, size_t length) {
    unsigned char data[4096] = {0};
    write16(data, 65535);
    write16(data + 6, 2000);
    write16(data + 8, 2000);
    size_t at = 10;
    int instructions = 0;
    for (unsigned i = 0; i < count; i++) {
        const pdf_tt_component *r = records + i;
        unsigned flags = r->flags | (i + 1 < count ? 32 : 0);
        CHECK(at + 20 <= sizeof(data));
        write16(data + at, flags);
        write16(data + at + 2, r->glyph);
        at += 4;
        for (unsigned a = 0; a < 2; a++) {
            if (flags & 1) {
                write16(data + at, (unsigned)r->argument[a]);
                at += 2;
            } else
                data[at++] = (unsigned char)r->argument[a];
        }
        if (flags & 8) {
            write16(data + at, (unsigned)r->matrix[0]);
            at += 2;
        } else if (flags & 64) {
            write16(data + at, (unsigned)r->matrix[0]);
            write16(data + at + 2, (unsigned)r->matrix[3]);
            at += 4;
        } else if (flags & 128) {
            for (unsigned a = 0; a < 4; a++)
                write16(data + at + a * 2, (unsigned)r->matrix[a]);
            at += 8;
        }
        instructions |= flags & 256;
    }
    if (instructions) {
        CHECK(at + 2 + length <= sizeof(data));
        write16(data + at, (unsigned)length);
        at += 2;
        if (length)
            memcpy(data + at, code, length);
        at += length;
    }
    return composite_add(fixture, data, at);
}

static void composite_loader_tests(void) {
    unsigned before = checks;
    pdf_tt_hint hint;
    composite_fixture fixture;
    const int32_t box[][2] = {{0, 0}, {0, 600}, {400, 600}, {400, 0}};
    for (unsigned flags = 0; flags < 4; flags++) {
        composite_init(&fixture);
        composite_add(&fixture, NULL, 0);
        composite_simple(&fixture, NULL, 0);
        pdf_tt_component r = {.flags = 3 | 128 |
                                       (flags == 1   ? 0x800
                                        : flags == 2 ? 0x1800
                                        : flags == 3 ? 4
                                                     : 0),
                              .glyph = 1,
                              .argument = {100, 200},
                              .matrix = {16384, 8192, -4096, 16384}};
        unsigned glyph = composite_parent(&fixture, &r, 1, NULL, 0);
        if (flags == 2)
            write16(fixture.data + tt_location(&fixture.font, glyph), 65534);
        CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000));
        CHECK(cupidimage_pdf_tt_hint_glyph(&hint, glyph));
        int32_t dx = flags == 1 ? 50 : flags == 3 ? 128 : 100;
        int32_t dy = flags == 1 ? 250 : flags == 3 ? 192 : 200;
        for (unsigned i = 0; i < 4; i++) {
            CHECK(hint.vm.zones[1].points[i].current[0] == box[i][0] - box[i][1] / 4 + dx);
            CHECK(hint.vm.zones[1].points[i].current[1] == box[i][0] / 2 + box[i][1] + dy);
            CHECK(hint.vm.zones[1].points[i].touched == 0);
        }
        CHECK(!hint.vm.zones[1].design && hint.vm.zones[1].outline_count == 4);
        finish(&hint);
        pdf_tt_outline unhinted = {0};
        CHECK(cupidimage_pdf_tt_outline(&fixture.font, glyph, &unhinted));
        CHECK(unhinted.points[0].x == (flags == 1 ? 50 : 100));
        CHECK(unhinted.points[0].y == (flags == 1 ? 250 : 200));
        cupidimage_pdf_tt_outline_free(&unhinted);
        CHECK(live_count == 0);
    }
    for (unsigned transform = 0; transform < 2; transform++) {
        composite_init(&fixture);
        composite_add(&fixture, NULL, 0);
        composite_simple(&fixture, NULL, 0);
        pdf_tt_component r = {.flags = 2 | (transform ? 64 : 8),
                              .glyph = 1,
                              .argument = {-97, 33},
                              .matrix = {8192, 0, 0, 12288}};
        unsigned glyph = composite_parent(&fixture, &r, 1, NULL, 0);
        CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000));
        CHECK(cupidimage_pdf_tt_hint_glyph(&hint, glyph));
        CHECK(hint.vm.zones[1].points[0].current[0] == -97 &&
              hint.vm.zones[1].points[0].current[1] == 33);
        CHECK(hint.vm.zones[1].points[1].current[1] == (transform ? 483 : 333));
        CHECK(hint.vm.zones[1].points[3].current[0] == 103);
        int32_t transformed[2];
        const int32_t matrix[] = {12288, 4096, 8192, 20480};
        CHECK(tt_hint_transform(&hint, matrix, (int32_t[]){6, 7}, transformed));
        CHECK(transformed[0] == 9 && transformed[1] == 11);
        CHECK(tt_hint_transform(&hint, matrix, (int32_t[]){-6, -7}, transformed));
        CHECK(transformed[0] == -9 && transformed[1] == -11);
        finish(&hint);
    }
    for (unsigned kind = 0; kind < 3; kind++) {
        composite_init(&fixture);
        composite_add(&fixture, NULL, 0);
        composite_simple(&fixture, NULL, 0);
        pdf_tt_component r[2] = {{.flags = 3 | (kind == 2 ? 512 : 0), .glyph = 1},
                                 {.flags = 1 | 4, .glyph = 1}};
        r[1].argument[0] = kind == 0 ? 1 : 9;
        r[1].argument[1] = kind == 0 ? 5 : 0;
        unsigned glyph = composite_parent(&fixture, r, 2, NULL, 0);
        CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000));
        CHECK(cupidimage_pdf_tt_hint_glyph(&hint, glyph));
        CHECK(hint.vm.zones[1].outline_count == 8);
        CHECK(hint.vm.zones[1].points[4].current[0] == (kind == 0   ? -1024
                                                        : kind == 1 ? 1000
                                                                    : 1024));
        CHECK(hint.vm.zones[1].points[4].current[1] == (kind == 0 ? 600 : 0));
        finish(&hint);
    }
    composite_init(&fixture);
    composite_add(&fixture, NULL, 0);
    static const unsigned char child_code[] = {0xb1, 0, 97, 0x48};
    composite_simple(&fixture, child_code, sizeof(child_code));
    pdf_tt_component r[2] = {{.flags = 3 | 256, .glyph = 1},
                             {.flags = 3, .glyph = 1, .argument = {500, 0}}};
    static const unsigned char parent_code[] = {0xb1, 0, 128, 0x48, 0xb0, 1, 0xc0};
    unsigned glyph = composite_parent(&fixture, r, 2, parent_code, sizeof(parent_code));
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, glyph));
    CHECK(hint.vm.zones[1].points[0].current[0] == 128 &&
          hint.vm.zones[1].points[1].current[0] == 31);
    CHECK(hint.vm.zones[1].points[4].current[0] == 597);
    CHECK(hint.vm.zones[1].points[4].touched == 0);
    finish(&hint);
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, glyph));
    size_t spent = 1000000 - hint.vm.work_left;
    finish(&hint);
    for (unsigned shortfall = 0; shortfall < 2; shortfall++) {
        CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, spent - shortfall));
        CHECK(cupidimage_pdf_tt_hint_glyph(&hint, glyph) == !shortfall);
        if (shortfall)
            CHECK(hint.vm.error == PDF_TT_VM_BUDGET);
        else
            CHECK(hint.vm.work_left == 0);
        finish(&hint);
    }
    allocation_count = 0;
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, glyph));
    size_t allocations = allocation_count;
    finish(&hint);
    for (size_t failure = 1; failure <= allocations; failure++) {
        allocation_count = 0;
        fail_at = failure;
        int ok = cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000);
        if (ok)
            ok = cupidimage_pdf_tt_hint_glyph(&hint, glyph);
        CHECK(!ok && hint.vm.error == PDF_TT_VM_MEMORY);
        fail_at = 0;
        finish(&hint);
    }
    pdf_tt_component nested = {.flags = 3, .glyph = glyph, .argument = {33, 0}};
    unsigned nested_glyph = composite_parent(&fixture, &nested, 1, NULL, 0);
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, nested_glyph));
    CHECK(hint.vm.zones[1].points[0].current[0] == 161 &&
          hint.vm.zones[1].points[1].current[0] == 64);
    finish(&hint);
    composite_init(&fixture);
    composite_add(&fixture, NULL, 0);
    composite_simple(&fixture, NULL, 0);
    unsigned last = 1;
    for (unsigned depth = 1; depth <= 16; depth++) {
        pdf_tt_component item = {.flags = 3, .glyph = last};
        last = composite_parent(&fixture, &item, 1, NULL, 0);
        CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000));
        CHECK(cupidimage_pdf_tt_hint_glyph(&hint, last) == (depth < 16));
        if (depth == 16)
            CHECK(hint.vm.error == PDF_TT_VM_BUDGET);
        finish(&hint);
    }
    composite_init(&fixture);
    composite_add(&fixture, NULL, 0);
    pdf_tt_component empty = {.flags = 3 | 256, .glyph = 0};
    glyph = composite_parent(&fixture, &empty, 1, child_code, sizeof(child_code));
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, glyph));
    CHECK(hint.vm.zones[1].outline_count == 0 && hint.vm.zones[1].points[0].current[0] == 97);
    finish(&hint);
    for (unsigned program = 0; program < 2; program++) {
        composite_init(&fixture);
        composite_add(&fixture, NULL, 0);
        composite_simple(&fixture, NULL, 0);
        pdf_tt_component item = {.flags = 3 | (program ? 256 : 0), .glyph = 1};
        static const unsigned char harmless[] = {0xb0, 0, 0x21};
        unsigned parent = composite_parent(&fixture, &item, 1, harmless, sizeof(harmless));
        write16(fixture.metrics + parent * 4 + 2, 19);
        CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000));
        CHECK(cupidimage_pdf_tt_hint_glyph(&hint, parent));
        CHECK(hint.vm.zones[1].points[4].original[0] == -19);
        CHECK(hint.vm.zones[1].points[4].current[0] == (program ? 0 : -19));
        finish(&hint);
    }
    composite_init(&fixture);
    composite_add(&fixture, NULL, 0);
    composite_simple(&fixture, child_code, sizeof(child_code));
    static const unsigned char inhibit[] = {0xb1, 1, 1, 0x8e};
    fixture.font.prep = (pdf_tt_table){inhibit, sizeof(inhibit)};
    pdf_tt_component inhibited = {.flags = 3 | 4 | 256, .glyph = 1, .argument = {33, -17}};
    glyph = composite_parent(&fixture, &inhibited, 1, parent_code, sizeof(parent_code));
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, glyph));
    CHECK(hint.vm.zones[1].points[0].current[0] == 33 &&
          hint.vm.zones[1].points[0].current[1] == -17);
    finish(&hint);
    composite_init(&fixture);
    composite_add(&fixture, NULL, 0);
    static const unsigned char shifted_metric[] = {0xb1, 4, 97, 0x48};
    composite_simple(&fixture, shifted_metric, sizeof(shifted_metric));
    pdf_tt_component inherited = {.flags = 3 | 512, .glyph = 1};
    glyph = composite_parent(&fixture, &inherited, 1, NULL, 0);
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, glyph));
    CHECK(hint.vm.zones[1].points[0].current[0] == 0 &&
          hint.vm.zones[1].points[4].current[0] == 97);
    finish(&hint);
    composite_init(&fixture);
    composite_add(&fixture, NULL, 0);
    composite_simple(&fixture, NULL, 0);
    unsigned exponential = 1;
    for (unsigned depth = 0; depth < 12; depth++) {
        pdf_tt_component branches[2] = {{.flags = 3, .glyph = exponential},
                                        {.flags = 3, .glyph = exponential}};
        exponential = composite_parent(&fixture, branches, 2, NULL, 0);
    }
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000));
    allocation_count = 0;
    CHECK(!cupidimage_pdf_tt_hint_glyph(&hint, exponential));
    CHECK(hint.vm.error == PDF_TT_VM_BUDGET && allocation_count == 0);
    finish(&hint);
    composite_init(&fixture);
    composite_add(&fixture, NULL, 0);
    unsigned char large[270] = {0};
    write16(large, 1);
    write16(large + 10, 32767);
    for (unsigned i = 14; i < sizeof(large); i += 2) {
        large[i] = 0x39;
        large[i + 1] = 255;
    }
    composite_add(&fixture, large, sizeof(large));
    pdf_tt_component too_many[3] = {
        {.flags = 3, .glyph = 1}, {.flags = 3, .glyph = 1}, {.flags = 3, .glyph = 1}};
    glyph = composite_parent(&fixture, too_many, 3, NULL, 0);
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000));
    allocation_count = 0;
    CHECK(!cupidimage_pdf_tt_hint_glyph(&hint, glyph));
    CHECK(hint.vm.error == PDF_TT_VM_POINT && allocation_count == 0);
    finish(&hint);
    composite_init(&fixture);
    composite_add(&fixture, NULL, 0);
    static const unsigned char shared_cvt[] = {0, 64};
    static const unsigned char writer[] = {0xb1, 0, 97, 0x44, 0xb1, 0, 145, 0x42};
    static const unsigned char reader[] = {0xb1, 0, 0, 0x45, 0x48, 0xb1, 1, 0, 0x43, 0x48};
    unsigned writer_glyph = composite_simple(&fixture, writer, sizeof(writer));
    unsigned reader_glyph = composite_simple(&fixture, reader, sizeof(reader));
    fixture.font.cvt = (pdf_tt_table){shared_cvt, sizeof(shared_cvt)};
    write16(fixture.profile + 18, 2);
    pdf_tt_component shared[2] = {{.flags = 3 | 256, .glyph = writer_glyph},
                                  {.flags = 3, .glyph = reader_glyph, .argument = {500, 0}}};
    glyph = composite_parent(&fixture, shared, 2, reader, sizeof(reader));
    CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000));
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, glyph));
    CHECK(hint.vm.zones[1].points[0].current[0] == 97 &&
          hint.vm.zones[1].points[1].current[0] == 145);
    CHECK(hint.vm.zones[1].points[4].current[0] == 597 &&
          hint.vm.zones[1].points[5].current[0] == 645);
    CHECK(hint.vm.cvt[0] == 97 && hint.vm.storage[0] == 145);
    CHECK(hint.vm.prepared_cvt[0] == 64 && hint.vm.prepared_storage[0] == 0);
    CHECK(cupidimage_pdf_tt_hint_glyph(&hint, reader_glyph));
    CHECK(hint.vm.zones[1].points[0].current[0] == 64 &&
          hint.vm.zones[1].points[1].current[0] == 0);
    finish(&hint);
    for (unsigned invalid = 0; invalid < 8; invalid++) {
        composite_init(&fixture);
        composite_add(&fixture, NULL, 0);
        composite_simple(&fixture, NULL, 0);
        pdf_tt_component bad[2] = {{.flags = 3, .glyph = 1},
                                   {.flags = 1, .glyph = 1, .argument = {1, 0}}};
        if (invalid == 0)
            bad[0].glyph = 2;
        if (invalid == 1)
            bad[0].glyph = 99;
        if (invalid == 2)
            bad[0].flags = 1;
        if (invalid == 3)
            bad[1].argument[0] = 4;
        if (invalid == 4)
            bad[1].argument[1] = 8;
        if (invalid == 5)
            bad[0].flags |= 0x2000;
        if (invalid == 6)
            bad[0].flags |= 8 | 64;
        unsigned bad_glyph = composite_parent(&fixture, bad, 2, NULL, 0);
        if (invalid == 7) {
            fixture.font.glyf.length--;
            composite_u32(fixture.locations + fixture.glyphs * 4, fixture.font.glyf.length);
        }
        CHECK(cupidimage_pdf_tt_hint_init(&hint, &fixture.font, 1000, 1000, 0, 1000000));
        CHECK(!cupidimage_pdf_tt_hint_glyph(&hint, bad_glyph));
        CHECK(hint.vm.error ==
              (invalid == 3 || invalid == 4 ? PDF_TT_VM_POINT : PDF_TT_VM_PROGRAM));
        finish(&hint);
    }
    composite_init(&fixture);
    composite_add(&fixture, NULL, 0);
    composite_simple(&fixture, NULL, 0);
    write16(fixture.metrics, 333);
    write16(fixture.metrics + 2, 19);
    write16(fixture.metrics + 4, 512);
    pdf_tt_component advance_records[2] = {{.flags = 3 | 512, .glyph = 0},
                                           {.flags = 3, .glyph = 1}};
    glyph = composite_parent(&fixture, advance_records, 2, NULL, 0);
    write16(fixture.metrics + glyph * 4, 700);
    pdf_tt_outline inherited_outline = {0};
    CHECK(cupidimage_pdf_tt_outline(&fixture.font, glyph, &inherited_outline));
    CHECK(inherited_outline.advance == 333 && inherited_outline.points[0].x == 19);
    CHECK(cupidimage_pdf_tt_advance(&fixture.font, glyph) == 700);
    cupidimage_pdf_tt_outline_free(&inherited_outline);
    advance_records[1].flags |= 512;
    unsigned last_metrics = composite_parent(&fixture, advance_records, 2, NULL, 0);
    CHECK(cupidimage_pdf_tt_outline(&fixture.font, last_metrics, &inherited_outline));
    CHECK(inherited_outline.advance == 512 && inherited_outline.points[0].x == 0);
    cupidimage_pdf_tt_outline_free(&inherited_outline);
    pdf_tt_component nested_advance = {.flags = 3 | 512, .glyph = last_metrics};
    unsigned nested_metrics = composite_parent(&fixture, &nested_advance, 1, NULL, 0);
    CHECK(cupidimage_pdf_tt_outline(&fixture.font, nested_metrics, &inherited_outline));
    CHECK(inherited_outline.advance == 512);
    cupidimage_pdf_tt_outline_free(&inherited_outline);
    CHECK(live_count == 0);
    printf("TrueType composites: %u checks, %zu allocation failures, 0 failures\n", checks - before,
           allocations);
}
