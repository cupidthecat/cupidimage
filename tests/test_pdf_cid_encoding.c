#include "formats/pdf/fonts/program.h"
#include "formats/pdf/fonts/substitute/cid_data.h"
#include "formats/pdf/fonts/substitute/cid_encoding.h"
#include "formats/pdf/pdf_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 4)
        return 2;
    unsigned encoding = !strcmp(argv[3], "cp932")   ? PDF_CID_SUBSTITUTE_CP932
                        : !strcmp(argv[3], "ucs2")  ? PDF_CID_SUBSTITUTE_UCS2
                        : !strcmp(argv[3], "utf16") ? PDF_CID_SUBSTITUTE_UTF16
                                                    : 0;
    if (!encoding)
        return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 2;
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return 2;
    }
    long length = ftell(file);
    if (length <= 0 || length > 8 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return 2;
    }
    pdf_font_program *program = calloc(1, sizeof(*program));
    if (!program) {
        fclose(file);
        return 2;
    }
    program->references = 1;
    program->is_cff = program->cid = 1;
    program->cid_substitute =
        !strcmp(argv[2], "mincho") ? &cupidimage_pdf_cid_mincho : &cupidimage_pdf_cid_gothic;
    program->length = (size_t)length;
    program->bytes = malloc(program->length);
    int ok = program->bytes && fread(program->bytes, 1, program->length, file) == program->length;
    fclose(file);
    pdf_cff_slice cff = {0};
    if (!ok || !cupidimage_pdf_cff_sfnt_table(program->bytes, program->length, &cff) ||
        !cupidimage_pdf_cff_init(&program->cff, cff.data, cff.length) ||
        !cupidimage_pdf_cid_substitute_cmap(program)) {
        cupidimage_pdf_font_program_free(program);
        return 2;
    }
    unsigned code;
    unsigned long bytes;
    size_t count = 0;
    while (count++ < 100000 && scanf("%8x %2lu", &code, &bytes) == 2)
        printf("%u %u\n", cupidimage_pdf_cid_substitute_glyph(program, encoding, code, bytes, 0),
               cupidimage_pdf_cid_substitute_glyph(program, encoding, code, bytes, 1));
    cupidimage_pdf_font_program_free(program);
    return count > 100000 ? 2 : 0;
}
