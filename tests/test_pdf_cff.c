#include "formats/pdf/fonts/cff.h"
#include "formats/pdf/fonts/type2.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, failures;
#define CHECK(expr)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(expr)) {                                                                             \
            failures++;                                                                            \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr);                                \
        }                                                                                          \
    } while (0)

static int dictionary(const unsigned char *data, size_t n, pdf_cff_dict *dict, int priv) {
    return cupidimage_pdf_cff_dict((pdf_cff_slice){data, n}, dict, priv);
}

int main(int argc, char **argv) {
    if (argc > 1) {
        FILE *file = fopen(argv[1], "rb");
        if (!file)
            return 1;
        if (fseek(file, 0, SEEK_END)) {
            fclose(file);
            return 1;
        }
        long length = ftell(file);
        if (length < 0 || length > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
            fclose(file);
            return 1;
        }
        unsigned char *data = malloc((size_t)length + 1);
        if (!data) {
            fclose(file);
            return 1;
        }
        int read_ok = fread(data, 1, (size_t)length, file) == (size_t)length;
        fclose(file);
        pdf_cff_font font;
        if (!read_ok || !cupidimage_pdf_cff_init(&font, data, (size_t)length)) {
            free(data);
            return 2;
        }
        printf("%u %u %u\n", font.chars.count, font.top.cid, font.private_count);
        printf("names %u %u %u\n", cupidimage_pdf_cff_name(&font, "space"),
               cupidimage_pdf_cff_name(&font, "A"), cupidimage_pdf_cff_name(&font, "customShape"));
        for (unsigned i = 0; i < 256; i++)
            if (font.encoding[i])
                printf("code %u %u\n", i, font.encoding[i]);
        for (unsigned i = 0; i < font.private_count; i++)
            printf("private %u %.9g %.9g %u\n", i, font.private_dicts[i].default_width,
                   font.private_dicts[i].nominal_width, font.private_dicts[i].subrs.count);
        if (argc > 2 && !strcmp(argv[2], "--hints")) {
            for (unsigned i = 0; i < font.private_count; i++) {
                const pdf_ps_private_hints *hints = &font.private_dicts[i].hints;
                printf("hints %u %u %u\n", i, hints->seen, hints->invalid);
                for (unsigned j = 0; j < 6; j++) {
                    printf("hint-array %u %u", i, j);
                    for (unsigned k = 0; k < hints->counts[j]; k++)
                        printf(" %.17g", hints->arrays[j][k]);
                    putchar('\n');
                }
                for (unsigned j = 0; j < 8; j++)
                    printf("hint-scalar %u %u %.17g\n", i, j, hints->scalars[j]);
            }
        }
        if (font.top.cid)
            for (unsigned i = 0; i < font.chars.count; i++)
                printf("fd %u %u\n", i, font.fdselect[i]);
        if (argc > 2 && (!strcmp(argv[2], "--outline") || !strcmp(argv[2], "--hint-outline"))) {
            for (unsigned gid = 0; gid < font.chars.count; gid++) {
                pdf_cff_outline outline;
                pdf_ps_hint_program hints = {0};
                int trace = !strcmp(argv[2], "--hint-outline");
                if (!cupidimage_pdf_type2_outline_hints(&font, gid, &outline,
                                                        trace ? &hints : NULL)) {
                    cupidimage_pdf_cff_free(&font);
                    free(data);
                    return 3;
                }
                printf("outline %u %.9g %u %d %u %u %.9g %.9g\n", gid, outline.width, outline.count,
                       outline.composite, outline.base, outline.accent, outline.accent_x,
                       outline.accent_y);
                for (unsigned j = 0; j < outline.count; j++) {
                    pdf_cff_segment *seg = outline.segments + j;
                    printf("segment %u", seg->kind);
                    for (unsigned k = 0; k < 6; k++)
                        printf(" %.9g", seg->points[k]);
                    putchar('\n');
                }
                if (trace) {
                    printf("hint-program %u %u %u\n", gid, hints.stem_count, hints.diagnostics);
                    for (unsigned j = 0; j < hints.stem_count; j++)
                        printf("stem %u %u %.17g %.17g\n", gid, hints.stems[j].vertical,
                               hints.stems[j].start, hints.stems[j].width);
                    for (unsigned j = 0; j < hints.event_count; j++) {
                        const pdf_ps_hint_event *event = hints.events + j;
                        printf("hint-event %u %u %u %u", gid, event->segment, event->stem_count,
                               event->counter);
                        for (unsigned k = 0; k < (event->stem_count + 7) / 8; k++)
                            printf(" %02x", event->mask[k]);
                        putchar('\n');
                    }
                }
                cupidimage_pdf_ps_hint_program_free(&hints);
                cupidimage_pdf_type2_free(&outline);
            }
        }
        cupidimage_pdf_cff_free(&font);
        free(data);
        return 0;
    }
    pdf_cff_index index;
    pdf_cff_slice item;
    size_t pos = 0;
    unsigned char data[] = {0, 2, 1, 1, 3, 6, 'a', 'b', 'c', 'd', 'e', 99};
    CHECK(cupidimage_pdf_cff_index((pdf_cff_slice){data, sizeof(data)}, &pos, &index));
    CHECK(pos == 11 && index.count == 2 && index.objects.length == 5);
    CHECK(cupidimage_pdf_cff_item(&index, 0, &item) && item.length == 2 &&
          !memcmp(item.data, "ab", 2));
    CHECK(cupidimage_pdf_cff_item(&index, 1, &item) && item.length == 3 &&
          !memcmp(item.data, "cde", 3));
    CHECK(!cupidimage_pdf_cff_item(&index, 2, &item));
    for (size_t n = 0; n < 11; n++) {
        pos = 0;
        CHECK(!cupidimage_pdf_cff_index((pdf_cff_slice){data, n}, &pos, &index));
    }
    unsigned char empty[] = {0, 0};
    pos = 0;
    CHECK(cupidimage_pdf_cff_index((pdf_cff_slice){empty, 2}, &pos, &index) && pos == 2 &&
          !index.count);
    unsigned char zero_item[] = {0, 1, 1, 1, 1};
    pos = 0;
    CHECK(cupidimage_pdf_cff_index((pdf_cff_slice){zero_item, 5}, &pos, &index));
    CHECK(cupidimage_pdf_cff_item(&index, 0, &item) && !item.length);
    const unsigned char invalid[][7] = {{0, 1, 0, 1, 1, 0, 0}, {0, 1, 5, 1, 1, 0, 0},
                                        {0, 1, 1, 0, 1, 0, 0}, {0, 1, 1, 2, 2, 0, 0},
                                        {0, 2, 1, 1, 3, 2, 0}, {0, 1, 1, 1, 255, 0, 0}};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        pos = 0;
        CHECK(!cupidimage_pdf_cff_index((pdf_cff_slice){invalid[i], 7}, &pos, &index));
    }
    for (unsigned size = 1; size <= 4; size++) {
        unsigned char multi[16] = {0, 1, 0};
        multi[2] = (unsigned char)size;
        multi[2 + size] = 1;
        multi[2 + 2 * size] = 2;
        multi[3 + 2 * size] = 'X';
        pos = 0;
        CHECK(cupidimage_pdf_cff_index((pdf_cff_slice){multi, 4 + 2 * size}, &pos, &index));
        CHECK(cupidimage_pdf_cff_item(&index, 0, &item) && item.length == 1 && *item.data == 'X');
    }
    pdf_cff_dict dict;
    CHECK(dictionary(NULL, 0, &dict, 0) && dict.matrix[0] == .001 && dict.matrix[3] == .001);
    CHECK(dict.hints.scalars[0] == .039625 && dict.hints.scalars[1] == 7 &&
          dict.hints.scalars[2] == 1 && dict.hints.scalars[7] == .06);
    CHECK(!dict.hints.seen && !dict.hints.invalid);
    unsigned char blues[] = {129, 149, 239, 149, 6, 119, 144, 7};
    CHECK(dictionary(blues, sizeof(blues), &dict, 1));
    CHECK(dict.hints.counts[0] == 4 && dict.hints.arrays[0][0] == -10 &&
          dict.hints.arrays[0][1] == 0 && dict.hints.arrays[0][2] == 100 &&
          dict.hints.arrays[0][3] == 110);
    CHECK(dict.hints.counts[1] == 2 && dict.hints.arrays[1][0] == -20 &&
          dict.hints.arrays[1][1] == -15 && !dict.hints.invalid);
    unsigned char unordered[] = {119, 144, 7, 129, 149, 6};
    CHECK(dictionary(unordered, sizeof(unordered), &dict, 1));
    CHECK(dict.hints.invalid == (1u << PDF_PS_OTHER_BLUES) && !dict.hints.counts[1]);
    unsigned char repaired[] = {139, 6, 129, 149, 6, 140, 12, 14, 141, 12, 17};
    CHECK(dictionary(repaired, sizeof(repaired), &dict, 1));
    CHECK(dict.hints.counts[0] == 2 && dict.hints.scalars[5] == 1 && dict.hints.scalars[6] == 0 &&
          dict.hints.invalid == (1u << PDF_PS_LANGUAGE_GROUP));
    unsigned char invalid_bool[] = {30, 0x1f, 12, 14};
    CHECK(dictionary(invalid_bool, sizeof(invalid_bool), &dict, 1));
    CHECK(dict.hints.invalid == (1u << PDF_PS_FORCE_BOLD) && dict.hints.scalars[5] == 0);
    for (unsigned field = 0; field < PDF_PS_HINT_FIELDS; field++) {
        double values[48];
        for (unsigned i = 0; i < 48; i++)
            values[i] = i ? 1 : 0;
        for (unsigned count = 0; count <= 48; count++) {
            cupidimage_pdf_ps_hints_init(&dict.hints);
            cupidimage_pdf_ps_hint_set(&dict.hints, field, values, count, 1, 1);
            static const unsigned maximum[6] = {14, 10, 14, 10, 12, 12};
            int valid =
                field < 6 ? count <= maximum[field] && (field >= 4 || !(count % 2)) : count == 1;
            CHECK(!!dict.hints.invalid == !valid);
            CHECK(dict.hints.seen == (1u << field));
            if (valid && field < 6 && count)
                CHECK(dict.hints.arrays[field][count - 1] == count - 1);
        }
        double invalid_value = INFINITY;
        cupidimage_pdf_ps_hint_set(&dict.hints, field, &invalid_value, 1, 0, 1);
        CHECK(dict.hints.invalid & (1u << field));
    }
    unsigned char offsets[] = {239, 15, 247, 0, 16, 28, 1, 0, 17, 149, 28, 2, 0, 18};
    CHECK(dictionary(offsets, sizeof(offsets), &dict, 0));
    CHECK(dict.charset == 100 && dict.encoding == 108 && dict.charstrings == 256 &&
          dict.private_size == 10 && dict.private_offset == 512);
    unsigned char widths[] = {28, 0xff, 0x38, 20, 29, 0, 1, 0x86, 0xa0, 21};
    CHECK(dictionary(widths, sizeof(widths), &dict, 1) && dict.default_width == -200 &&
          dict.nominal_width == 100000);
    unsigned char real[] = {30, 0xe2, 0xa2, 0x5f, 20, 30, 0x0a, 0x14, 0x05, 0x41, 0xc3, 0xff, 21};
    CHECK(dictionary(real, sizeof(real), &dict, 1));
    CHECK(dict.default_width == -2.25 && fabs(dict.nominal_width - .000140541) < 1e-15);
    unsigned char matrix[] = {140, 139, 139, 140, 149, 159, 12, 7};
    CHECK(dictionary(matrix, sizeof(matrix), &dict, 0) && dict.has_matrix && dict.matrix[0] == 1 &&
          dict.matrix[4] == 10 && dict.matrix[5] == 20);
    unsigned char cid[] = {139, 140, 141, 12, 30, 239, 12, 36, 247, 0, 12, 37};
    CHECK(dictionary(cid, sizeof(cid), &dict, 0) && dict.cid && dict.fdarray == 100 &&
          dict.fdselect == 108);
    unsigned char bad[][8] = {
        {138, 15},           {139, 139, 17}, {28}, {29, 0, 0}, {30, 0xff, 20}, {30, 0x1d, 0xff, 20},
        {30, 0x1f, 21, 139}, {139, 12},      {22}, {255},      {140, 12, 6},   {139, 12, 20},
        {30, 0x1a, 0x5f, 17}};
    const size_t lengths[] = {2, 3, 1, 3, 3, 4, 4, 2, 1, 1, 3, 3, 4};
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(*lengths); i++)
        CHECK(!dictionary(bad[i], lengths[i], &dict, 0));
    unsigned char overflow[50];
    memset(overflow, 139, sizeof(overflow));
    overflow[49] = 17;
    CHECK(!dictionary(overflow, sizeof(overflow), &dict, 0));
    pos = sizeof(data) + 1;
    CHECK(!cupidimage_pdf_cff_index((pdf_cff_slice){data, sizeof(data)}, &pos, &index));
    {
        const unsigned char program[] = {139, 159, 18, 19, 128, 139, 139, 21, 149, 139, 5, 14};
        const unsigned char offsets[] = {1, sizeof(program) + 1};
        pdf_cff_private private_dict = {0};
        pdf_cff_font font = {0};
        font.chars = (pdf_cff_index){{offsets, sizeof(offsets)}, {program, sizeof(program)}, 1, 1};
        font.private_dicts = &private_dict;
        font.private_count = 1;
        for (size_t budget = 0; budget <= 12; budget++) {
            pdf_cff_outline outline;
            pdf_ps_hint_program hints;
            size_t work = budget;
            int ok = cupidimage_pdf_type2_outline_budget(&font, 0, &outline, &hints, &work);
            CHECK(ok == (budget >= 11));
            CHECK(work == (budget >= 11 ? budget - 11 : 0));
            if (!ok)
                CHECK(!outline.segments && !outline.count && !hints.events && !hints.event_count);
            cupidimage_pdf_type2_free(&outline);
            cupidimage_pdf_ps_hint_program_free(&hints);
        }
        unsigned char *subr = malloc(60001);
        CHECK(subr != NULL);
        if (subr) {
            for (unsigned j = 0; j < 20000; j++) {
                subr[j * 3] = 139;
                subr[j * 3 + 1] = 12;
                subr[j * 3 + 2] = 18;
            }
            subr[60000] = 11;
            const unsigned char sub_offsets[] = {0, 0, 0, 1, 0, 0, 0xea, 0x62};
            const unsigned char calls[] = {32, 10, 32, 10, 14};
            const unsigned char call_offsets[] = {1, sizeof(calls) + 1};
            private_dict.subrs =
                (pdf_cff_index){{sub_offsets, sizeof(sub_offsets)}, {subr, 60001}, 1, 4};
            font.chars =
                (pdf_cff_index){{call_offsets, sizeof(call_offsets)}, {calls, sizeof(calls)}, 1, 1};
            pdf_cff_outline outline;
            pdf_ps_hint_program hints;
            size_t work = 100000;
            CHECK(!cupidimage_pdf_type2_outline_budget(&font, 0, &outline, &hints, &work));
            CHECK(work == 100000 - 65536);
            CHECK(!outline.segments && !hints.events);
            cupidimage_pdf_type2_free(&outline);
            cupidimage_pdf_ps_hint_program_free(&hints);
            free(subr);
        }
    }
    printf("CFF data primitives: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
