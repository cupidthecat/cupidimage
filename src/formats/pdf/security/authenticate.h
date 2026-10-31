#ifndef CUPIDIMAGE_PDF_SECURITY_AUTHENTICATE_H
#define CUPIDIMAGE_PDF_SECURITY_AUTHENTICATE_H

#include "crypto.h"

typedef struct {
    unsigned revision, key_length;
    uint32_t permissions;
    int encrypt_metadata;
    unsigned char owner[48], user[48], owner_key[32], user_key[32], perms[16];
    pdf_crypto_slice identifier;
} pdf_security_parameters;

/* Password bytes are already PDFDocEncoding (R2-4) or prepared UTF-8 (R5-6). */
int cupidimage_pdf_security_authenticate(const pdf_security_parameters *parameters,
                                         const unsigned char *password, size_t length,
                                         unsigned char key[32], int *owner);
int cupidimage_pdf_security_hash6(const unsigned char *password, size_t length,
                                  const unsigned char salt[8], const unsigned char *user,
                                  unsigned char output[32]);

#endif
