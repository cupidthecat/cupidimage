#include "formats/pdf/pdf_icc_lut.h"
#include "formats/pdf/pdf_icc_mab.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 8)
        return 1;
    unsigned outputs = (unsigned)atoi(argv[3]);
    if (outputs != 1 && outputs != 3 && outputs != 4)
        return 1;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 2;
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return 2;
    }
    long length = ftell(file);
    if (length < 0 || length > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return 2;
    }
    unsigned char *data = malloc((size_t)length + 1);
    int ok = data && fread(data, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    double values[4] = {0}, guarded[6] = {12345, 12345, 12345, 12345, 12345, 12345};
    for (unsigned i = 0; i < 3; i++)
        values[i] = strtod(argv[i + 5], NULL);
    pdf_icc_lut lut;
    pdf_icc_mab mba;
    int reverse_stages = !strcmp(argv[2], "mba");
    ok = ok && (reverse_stages ? cupidimage_pdf_icc_mba_parse(&mba, data, (size_t)length, outputs)
                               : cupidimage_pdf_icc_lut_parse_reverse(&lut, data, (size_t)length,
                                                                      outputs, atoi(argv[4])));
    if (ok)
        ok = reverse_stages ? cupidimage_pdf_icc_mba_value(&mba, values, guarded + 1)
                            : cupidimage_pdf_icc_lut_value(&lut, values, guarded + 1);
    if (ok) {
        ok = guarded[0] == 12345 && guarded[outputs + 1] == 12345;
        ok = ok && (reverse_stages ? cupidimage_pdf_icc_mba_value(&mba, values, values)
                                   : cupidimage_pdf_icc_lut_value(&lut, values, values));
        for (unsigned i = 0; ok && i < outputs; i++)
            ok = values[i] == guarded[i + 1];
    }
    if (ok) {
        for (unsigned i = 0; i < outputs; i++)
            printf("%s%.17g", i ? " " : "", guarded[i + 1]);
        putchar('\n');
    }
    free(data);
    return ok ? 0 : 3;
}
