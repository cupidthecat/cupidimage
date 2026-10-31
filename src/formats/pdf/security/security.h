#ifndef CUPIDIMAGE_PDF_SECURITY_H
#define CUPIDIMAGE_PDF_SECURITY_H

#include "../pdf_internal.h"
#include "authenticate.h"

typedef enum {
    PDF_CRYPT_IDENTITY,
    PDF_CRYPT_RC4,
    PDF_CRYPT_AES128,
    PDF_CRYPT_AES256
} pdf_crypt_method;

struct pdf_security {
    pdf_security_parameters parameters;
    pdf_object_id encryption_object, metadata_object;
    pdf_object_view dictionary;
    unsigned char key[32];
    pdf_crypt_method strings, streams, embedded;
    int authenticated, owner, embedded_deferred;
};

int cupidimage_pdf_security_open(pdf_xref *xref, const cupidimage_pdf_password *password,
                                 char *error, size_t capacity);
void cupidimage_pdf_security_close(pdf_xref *xref);
int cupidimage_pdf_security_filter(const pdf_security *security, const char *name,
                                   pdf_crypt_method *method);
int cupidimage_pdf_security_decrypt(const pdf_security *security, pdf_object_id identity,
                                    pdf_crypt_method method, const unsigned char *input,
                                    size_t length, unsigned char **output, size_t *output_length);
int cupidimage_pdf_security_stream_method(const pdf_object_view *view, const char *explicit_name,
                                          pdf_crypt_method *method);
int cupidimage_pdf_security_strings(pdf_object_view *view, size_t *work);

#endif
