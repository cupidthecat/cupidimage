#include "formats/pdf/fonts/type1.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5)
        return 1;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 1;
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return 1;
    }
    long n = ftell(file);
    if (n < 0 || n > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return 1;
    }
    unsigned char *data = malloc((size_t)n + 1);
    if (!data) {
        fclose(file);
        return 1;
    }
    int read_ok = fread(data, 1, (size_t)n, file) == (size_t)n;
    fclose(file);
    pdf_type1_font font;
    if (!read_ok ||
        !cupidimage_pdf_type1_init(&font, data, (size_t)n, (size_t)strtoul(argv[2], NULL, 10),
                                   (size_t)strtoul(argv[3], NULL, 10))) {
        free(data);
        return 2;
    }
    printf("%u %u %d\n", font.glyph_count, font.subr_count, font.len_iv);
    printf("matrix");
    for (unsigned i = 0; i < 6; i++)
        printf(" %.9g", font.matrix[i]);
    putchar('\n');
    for (unsigned i = 0; i < 256; i++)
        if (font.encoding[i])
            printf("code %u %u\n", i, font.encoding[i]);
    for (unsigned i = 0; i < font.glyph_count; i++) {
        printf("glyph %u %s ", i, font.glyphs[i].name);
        for (size_t j = 0; j < font.glyphs[i].code.length; j++)
            printf("%02x", font.glyphs[i].code.data[j]);
        putchar('\n');
    }
    for (unsigned i = 0; i < font.subr_count; i++) {
        printf("subr %u ", i);
        for (size_t j = 0; j < font.subrs[i].length; j++)
            printf("%02x", font.subrs[i].data[j]);
        putchar('\n');
    }
    if (argc == 5) {
        for (unsigned i = 0; i < font.glyph_count; i++) {
            pdf_cff_outline out;
            if (!cupidimage_pdf_type1_outline(&font, i, &out)) {
                cupidimage_pdf_type1_free(&font);
                free(data);
                return 3;
            }
            printf("outline %u %.9g %.9g %u\n", i, out.width, out.width_y, out.count);
            for (unsigned j = 0; j < out.count; j++) {
                printf("segment %u", out.segments[j].kind);
                for (unsigned k = 0; k < 6; k++)
                    printf(" %.9g", out.segments[j].points[k]);
                putchar('\n');
            }
            cupidimage_pdf_type2_free(&out);
        }
    }
    cupidimage_pdf_type1_free(&font);
    free(data);
    return 0;
}
