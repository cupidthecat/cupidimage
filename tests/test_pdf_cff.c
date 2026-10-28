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
        if (font.top.cid)
            for (unsigned i = 0; i < font.chars.count; i++)
                printf("fd %u %u\n", i, font.fdselect[i]);
        if (argc > 2 && !strcmp(argv[2], "--outline")) {
            for (unsigned gid = 0; gid < font.chars.count; gid++) {
                pdf_cff_outline outline;
                if (!cupidimage_pdf_type2_outline(&font, gid, &outline)) {
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
    printf("CFF data primitives: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
