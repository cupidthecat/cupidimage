#include "formats/pdf/fonts/type1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
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
    if (argc == 5 && !strcmp(argv[4], "--hints")) {
        printf("hints %u %u %u\n", font.hints.seen, font.hints.invalid, font.unresolved_hints);
        for (unsigned i = 0; i < 6; i++) {
            printf("hint-array %u", i);
            for (unsigned j = 0; j < font.hints.counts[i]; j++)
                printf(" %.17g", font.hints.arrays[i][j]);
            putchar('\n');
        }
        for (unsigned i = 0; i < 8; i++)
            printf("hint-scalar %u %.17g\n", i + 6, font.hints.scalars[i]);
    } else if (argc == 5) {
        for (unsigned i = 0; i < font.glyph_count; i++) {
            pdf_cff_outline out;
            pdf_type1_hint_trace trace = {0};
            int tracing = !strcmp(argv[4], "--hint-outline");
            if (!(tracing ? cupidimage_pdf_type1_outline_hints(&font, i, &out, &trace)
                          : cupidimage_pdf_type1_outline(&font, i, &out))) {
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
            if (tracing) {
                printf("hint-trace %u %u %u\n", i, trace.count, trace.diagnostics);
                for (unsigned j = 0; j < trace.count; j++) {
                    pdf_type1_hint_event *event = trace.events + j;
                    printf("hint-event %u %u %u %u", i, event->kind, event->segment,
                           event->vertical);
                    for (unsigned k = 0; k < 6; k++)
                        printf(" %.17g", event->values[k]);
                    putchar('\n');
                }
                if (out.composite)
                    printf("composite %u %u %u %.17g %.17g\n", i, out.base, out.accent,
                           out.accent_x, out.accent_y);
            }
            cupidimage_pdf_type1_trace_free(&trace);
            cupidimage_pdf_type2_free(&out);
        }
    }
    cupidimage_pdf_type1_free(&font);
    free(data);
    return 0;
}
