#include "formats/pdf/fonts/program.h"
#include "formats/pdf/fonts/substitute/cid_data.h"
#include "formats/pdf/fonts/substitute/cid_encoding.h"
#include "formats/pdf/pdf_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_collection_encodings(void) {
    const char *names[] = {"UniJIS-UTF16-H", "UniGB-UTF16-H", "UniCNS-UTF16-H", "UniKS-UTF16-H"};
    const char *orderings[] = {"Japan1", "GB1", "CNS1", "Korea1"};
    for (unsigned i = 0; i < 4; i++) {
        assert(cupidimage_pdf_cid_substitute_encoding(names[i], NULL) == PDF_CID_SUBSTITUTE_UTF16);
        for (unsigned j = 0; j < 4; j++)
            assert(cupidimage_pdf_cid_substitute_encoding(names[i], orderings[j]) ==
                   (i == j ? PDF_CID_SUBSTITUTE_UTF16 : PDF_CID_SUBSTITUTE_NONE));
        assert(cupidimage_pdf_cid_substitute_encoding("90ms-RKSJ-V", orderings[i]) ==
               (i == 0 ? PDF_CID_SUBSTITUTE_CP932 : PDF_CID_SUBSTITUTE_NONE));
    }
    assert(!cupidimage_pdf_cid_substitute_encoding("UniGB-UTF16-Hextra", "GB1"));
    assert(!cupidimage_pdf_cid_substitute_encoding("UniGB-UCS2", "GB1"));
}

int main(int argc, char **argv) {
    check_collection_encodings();
    if (argc != 4)
        return 2;
    unsigned encoding = !strcmp(argv[3], "cp932")   ? PDF_CID_SUBSTITUTE_CP932
                        : !strcmp(argv[3], "ucs2")  ? PDF_CID_SUBSTITUTE_UCS2
                        : !strcmp(argv[3], "utf16") ? PDF_CID_SUBSTITUTE_UTF16
                                                    : 0;
    int validate = !strcmp(argv[3], "outlines");
    if (!encoding && !validate)
        return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 2;
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return 2;
    }
    long length = ftell(file);
    if (length <= 0 || length > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
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
    const char *faces[PDF_CID_SOURCE_COUNT] = {"gothic",    "mincho",    "gothic_cn", "mincho_cn",
                                               "gothic_tw", "mincho_tw", "gothic_k1", "mincho_k1"};
    for (unsigned i = 0; i < PDF_CID_SOURCE_COUNT; i++)
        if (!strcmp(argv[2], faces[i]))
            program->cid_substitute = cupidimage_pdf_cid_sources[i];
    if (!program->cid_substitute) {
        fclose(file);
        free(program);
        return 2;
    }
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
    if (validate) {
        unsigned nonempty = 0, diagnostics = 0;
        for (unsigned glyph = 0; glyph < program->cff.chars.count; glyph++) {
            pdf_cff_outline outline = {0};
            pdf_ps_hint_program hints = {0};
            int valid = cupidimage_pdf_type2_outline_hints(&program->cff, glyph, &outline, &hints);
            nonempty += outline.count != 0;
            diagnostics += hints.diagnostics != 0;
            cupidimage_pdf_type2_free(&outline);
            cupidimage_pdf_ps_hint_program_free(&hints);
            if (!valid) {
                fprintf(stderr, "CID outline failed at glyph %u\n", glyph);
                cupidimage_pdf_font_program_free(program);
                return 3;
            }
        }
        printf("%u %u %u\n", program->cff.chars.count, nonempty, diagnostics);
        cupidimage_pdf_font_program_free(program);
        return 0;
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
