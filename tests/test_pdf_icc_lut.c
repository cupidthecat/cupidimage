#include "formats/pdf/pdf_icc_lut.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 5)
        return 1;
    unsigned inputs = (unsigned)atoi(argv[2]);
    if (inputs > 4 || argc != (int)inputs + 4)
        return 1;
    FILE *file = fopen(argv[1], "rb");
    if (!file || fseek(file, 0, SEEK_END)) {
        if (file)
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
    pdf_icc_lut lut;
    double values[4] = {0}, output[3];
    for (unsigned i = 0; i < inputs; i++)
        values[i] = strtod(argv[i + 4], NULL);
    ok = ok && cupidimage_pdf_icc_lut_parse(&lut, data, (size_t)length, inputs, atoi(argv[3])) &&
         cupidimage_pdf_icc_lut_value(&lut, values, output);
    if (ok)
        printf("%.17g %.17g %.17g\n", output[0], output[1], output[2]);
    free(data);
    return ok ? 0 : 3;
}
