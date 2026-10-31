#ifndef CUPIDIMAGE_PDF_SECURITY_CRYPTO_H
#define CUPIDIMAGE_PDF_SECURITY_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const void *data;
    size_t length;
} pdf_crypto_slice;

typedef enum {
    PDF_CRYPTO_MD5,
    PDF_CRYPTO_SHA256,
    PDF_CRYPTO_SHA384,
    PDF_CRYPTO_SHA512
} pdf_crypto_hash;

/* Callers supply 64 output bytes; only the selected digest length is written. */
int cupidimage_pdf_crypto_digest(pdf_crypto_hash algorithm, const pdf_crypto_slice *parts,
                                 size_t count, unsigned char output[64]);
int cupidimage_pdf_crypto_aes(const unsigned char *key, size_t key_length, unsigned char iv[16],
                              unsigned char *data, size_t length, int encrypt);
int cupidimage_pdf_crypto_rc4(const unsigned char *key, size_t key_length, unsigned char *data,
                              size_t length);
int cupidimage_pdf_crypto_equal(const unsigned char *a, const unsigned char *b, size_t length);
void cupidimage_pdf_crypto_erase(void *data, size_t length);

#endif
