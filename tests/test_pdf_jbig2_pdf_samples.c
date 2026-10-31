#include "formats/pdf/pdf_internal.h"
#include "formats/pdf/security/security.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 6)
        return 2;
    unsigned char *document = NULL, *decoded = NULL;
    size_t length = 0, eof, offset, stream_length = 0, decoded_length = 0;
    pdf_xref index = {0};
    pdf_object_view view = {0};
    char error[256] = {0};
    const unsigned char *stream = NULL;
    int ok =
        cupidimage_read_file_bytes(argv[1], &document, &length, error, sizeof(error)) &&
        cupidimage_pdf_find_eof_marker(document, length, &eof) &&
        cupidimage_pdf_find_startxref(document, eof, &offset) &&
        cupidimage_pdf_parse_xref_table(document, length, offset, &index, error, sizeof(error)) &&
        cupidimage_pdf_security_open(&index, NULL, error, sizeof(error)) &&
        cupidimage_pdf_lookup_object(&index, document, length,
                                     PDF_OBJECT_ID((unsigned)strtoul(argv[2], NULL, 10),
                                                   (unsigned)strtoul(argv[3], NULL, 10)),
                                     &view) == 1 &&
        cupidimage_pdf_object_stream(document, length, &view, &stream, &stream_length);
    pdf_image_stream_profile profile = {
        (int)strtol(argv[4], NULL, 10), (int)strtol(argv[5], NULL, 10), 1, 1, document, length, 0};
    ok = ok && cupidimage_pdf_decode_image_stream(&view, stream, stream_length, &decoded,
                                                  &decoded_length, &profile);
    if (ok)
        ok = fwrite(decoded, 1, decoded_length, stdout) == decoded_length;
    else
        fprintf(stderr, "PDF image sample failure: %s\n", error);
    free(decoded);
    cupidimage_pdf_object_view_release(&view);
    cupidimage_pdf_security_close(&index);
    free(index.entries);
    free(document);
    return ok ? 0 : 3;
}
