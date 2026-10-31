#include "formats/pdf/pdf_internal.h"
#include "formats/pdf/security/security.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 4)
        return 2;
    unsigned char password_bytes[4097], *document = NULL, *decoded = NULL;
    size_t password_length = fread(password_bytes, 1, sizeof(password_bytes), stdin);
    size_t length = 0, eof, offset, stream_length = 0, decoded_length = 0;
    if (password_length > 4096 || ferror(stdin))
        return 2;
    cupidimage_pdf_password password = {password_bytes, password_length, 0};
    pdf_xref index = {0};
    pdf_object_view view = {0};
    char error[256] = {0};
    const unsigned char *stream = NULL;
    int ok =
        cupidimage_read_file_bytes(argv[1], &document, &length, error, sizeof(error)) &&
        cupidimage_pdf_find_eof_marker(document, length, &eof) &&
        cupidimage_pdf_find_startxref(document, eof, &offset) &&
        cupidimage_pdf_parse_xref_table(document, length, offset, &index, error, sizeof(error)) &&
        cupidimage_pdf_security_open(&index, &password, error, sizeof(error)) &&
        cupidimage_pdf_lookup_object(&index, document, length,
                                     PDF_OBJECT_ID((unsigned)strtoul(argv[2], NULL, 10),
                                                   (unsigned)strtoul(argv[3], NULL, 10)),
                                     &view) == 1 &&
        cupidimage_pdf_object_stream(document, length, &view, &stream, &stream_length) &&
        cupidimage_pdf_decode_stream(&view, stream, stream_length, &decoded, &decoded_length);
    if (ok)
        ok = fwrite(decoded, 1, decoded_length, stdout) == decoded_length;
    else
        fprintf(stderr, "PDF stream failure: %s\n", error);
    cupidimage_pdf_crypto_erase(password_bytes, sizeof(password_bytes));
    free(decoded);
    cupidimage_pdf_object_view_release(&view);
    cupidimage_pdf_security_close(&index);
    free(index.entries);
    free(document);
    return ok ? 0 : 3;
}
