static void type1_flex_ownership(void) {
    pdf_ps_private_hints priv;
    cupidimage_pdf_ps_hints_init(&priv);
    pdf_cff_segment segments[] = {{{100, -10}, PDF_CFF_MOVE},
                                    {{115, -10, 125, 0, 150, 0}, PDF_CFF_CURVE},
                                    {{175, 0, 185, -10, 200, -10}, PDF_CFF_CURVE},
                                    {{200, -110}, PDF_CFF_LINE},
                                    {{100, -110}, PDF_CFF_LINE},
                                    {{100, -10}, PDF_CFF_CLOSE}};
    pdf_cff_outline source = {.segments = segments, .count = 6, .width = 600};
    pdf_type1_hint_event event = {.kind = PDF_T1_HINT_FLEX, .segment = 1,
                                   .values = {50, 200, -10, 150, -10, 0}};
    pdf_type1_hint_trace trace = {.events = &event, .count = 1};
    for (unsigned profile = 0; profile < 3; profile++) {
        pdf_ps_gridfit_size size = {.x_scale = profile ? 8192 : 1049,
                                     .y_scale = profile ? 8192 : 1049};
        if (profile == 2) {
            size.flex_transform_set = 1;
            size.flex_transform[0] = size.flex_transform[3] = .125;
            size.flex_offset[1] = .25;
        }
        size_t total = 0;
        pdf_cff_segment expected[16];
        unsigned count = 0;
        for (size_t failure = 0; failure <= total + 1; failure++) {
            fail_at = failure;
            allocation_count = 0;
            size_t work = 100000;
            pdf_cff_outline output = {0};
            int ok = cupidimage_pdf_ps_gridfit_type1(&priv, &trace, &source, &size, &output, &work);
            if (!failure) {
                total = allocation_count;
                count = output.count;
                CHECK(count <= 16);
                memcpy(expected, output.segments, count * sizeof(*expected));
            }
            CHECK(ok == (!failure || failure > total));
            if (ok) {
                CHECK(output.width == 600 && output.count == count);
                CHECK(!memcmp(output.segments, expected, count * sizeof(*expected)));
            } else
                CHECK(!output.segments && !output.count);
            cupidimage_pdf_type2_free(&output);
            CHECK(!live_count);
        }
        fail_at = 0;
        for (size_t budget = 0; budget < 12000; budget += 37) {
            size_t work = budget;
            pdf_cff_outline output = {0};
            int ok = cupidimage_pdf_ps_gridfit_type1(&priv, &trace, &source, &size, &output, &work);
            CHECK(work <= budget);
            if (ok) {
                CHECK(output.count == count);
                CHECK(!memcmp(output.segments, expected, count * sizeof(*expected)));
            } else
                CHECK(!output.segments && !output.count);
            cupidimage_pdf_type2_free(&output);
            CHECK(!live_count);
        }
        for (unsigned bad = 0; bad < 10; bad++) {
            pdf_type1_hint_event events[2] = {event, event};
            pdf_type1_hint_trace invalid = {.events = events, .count = 1};
            pdf_ps_gridfit_size transform = size;
            if (bad == 0) events[0].segment = 0;
            if (bad == 1) events[0].segment = 5;
            if (bad == 2) events[0].values[3] = NAN;
            if (bad == 3) events[0].values[4] = 4097;
            if (bad == 4) events[0].values[5] = 1;
            if (bad == 5) invalid.count = 2;
            if (bad == 6) {
                invalid.count = 2;
                events[1].kind = PDF_T1_HINT_DOTSECTION;
                events[1].segment = 2;
            }
            if (bad == 7) { transform.flex_transform_set = 1; transform.flex_offset[0] = INFINITY; }
            if (bad == 8) { transform.flex_transform_set = 1; memset(transform.flex_transform, 0, sizeof(transform.flex_transform)); }
            if (bad == 9) invalid.events = NULL;
            size_t work = 100000;
            pdf_cff_outline output = {0};
            CHECK(!cupidimage_pdf_ps_gridfit_type1(&priv, &invalid, &source, &transform, &output, &work));
            CHECK(!output.segments && !output.count && !live_count);
        }
    }
}
