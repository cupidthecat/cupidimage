#include "formats/pdf/fonts/encoding.h"
#include "formats/pdf/fonts/truetype.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 3)
        return 1;
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
    int ok = fread(data, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    pdf_tt_font font;
    if (!ok || !cupidimage_pdf_tt_init(&font, data, (size_t)length)) {
        free(data);
        return 2;
    }
    if (!strcmp(argv[2], "map-list") && argc >= 6) {
        for (int i = 5; i < argc; i++)
            printf("%u\n", cupidimage_pdf_tt_map(&font, (uint32_t)strtoul(argv[i], NULL, 0),
                                                 atoi(argv[3]), atoi(argv[4])));
        free(data);
        return 0;
    }
    if (!strcmp(argv[2], "map") && argc == 6) {
        printf("%u\n", cupidimage_pdf_tt_map(&font, (uint32_t)strtoul(argv[5], NULL, 0),
                                             atoi(argv[3]), atoi(argv[4])));
        free(data);
        return 0;
    }
    if (!strcmp(argv[2], "names")) {
        char name[128];
        while (scanf("%127s", name) == 1)
            printf("%u\n", cupidimage_pdf_glyph_unicode(name));
        free(data);
        return 0;
    }
    if (!strcmp(argv[2], "encoding") && argc == 4) {
        for (unsigned i = 0; i < 256; i++)
            puts(cupidimage_pdf_encoding_name(argv[3], i));
        free(data);
        return 0;
    }
    if (!strcmp(argv[2], "post-list") && argc >= 4) {
        for (int i = 3; i < argc; i++)
            printf("%u\n", cupidimage_pdf_tt_name(&font, argv[i]));
        free(data);
        return 0;
    }
    if (!strcmp(argv[2], "post") && argc == 4) {
        printf("%u\n", cupidimage_pdf_tt_name(&font, argv[3]));
        free(data);
        return 0;
    }
    unsigned glyph = (unsigned)strtoul(argv[2], NULL, 0);
    pdf_tt_outline outline = {0};
    ok = cupidimage_pdf_tt_outline(&font, glyph, &outline);
    if (ok) {
        printf("%u %u %u %u\n", font.units, font.glyphs, outline.advance, outline.count);
        for (unsigned i = 0; i < outline.count; i++)
            printf("%.17g %.17g %u %u\n", outline.points[i].x, outline.points[i].y,
                   outline.points[i].on, outline.points[i].end);
    }
    cupidimage_pdf_tt_outline_free(&outline);
    free(data);
    return ok ? 0 : 3;
}
