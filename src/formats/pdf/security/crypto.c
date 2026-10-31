#include "crypto.h"
#include "crypto/namespace.h"
#include "third_party/bearssl/bearssl_block.h"
#include "third_party/bearssl/bearssl_hash.h"

void cupidimage_pdf_crypto_erase(void *data, size_t length) {
    volatile unsigned char *bytes = data;
    while (length--)
        *bytes++ = 0;
}

int cupidimage_pdf_crypto_equal(const unsigned char *a, const unsigned char *b, size_t length) {
    unsigned difference = 0;
    for (size_t i = 0; i < length; i++)
        difference |= a[i] ^ b[i];
    return difference == 0;
}

int cupidimage_pdf_crypto_digest(pdf_crypto_hash algorithm, const pdf_crypto_slice *parts,
                                 size_t count, unsigned char output[64]) {
    union {
        br_md5_context md5;
        br_sha256_context sha256;
        br_sha384_context sha384;
        br_sha512_context sha512;
    } context;

    if (!output || (count && !parts))
        return 0;
    for (size_t i = 0; i < count; i++) {
        if (parts[i].length && !parts[i].data)
            return 0;
    }
    switch (algorithm) {
        case PDF_CRYPTO_MD5:
            br_md5_init(&context.md5);
            for (size_t i = 0; i < count; i++)
                if (parts[i].length)
                    br_md5_update(&context.md5, parts[i].data, parts[i].length);
            br_md5_out(&context.md5, output);
            break;
        case PDF_CRYPTO_SHA256:
            br_sha256_init(&context.sha256);
            for (size_t i = 0; i < count; i++)
                if (parts[i].length)
                    br_sha256_update(&context.sha256, parts[i].data, parts[i].length);
            br_sha256_out(&context.sha256, output);
            break;
        case PDF_CRYPTO_SHA384:
            br_sha384_init(&context.sha384);
            for (size_t i = 0; i < count; i++)
                if (parts[i].length)
                    br_sha384_update(&context.sha384, parts[i].data, parts[i].length);
            br_sha384_out(&context.sha384, output);
            break;
        case PDF_CRYPTO_SHA512:
            br_sha512_init(&context.sha512);
            for (size_t i = 0; i < count; i++)
                if (parts[i].length)
                    br_sha512_update(&context.sha512, parts[i].data, parts[i].length);
            br_sha512_out(&context.sha512, output);
            break;
        default:
            return 0;
    }
    cupidimage_pdf_crypto_erase(&context, sizeof(context));
    return 1;
}

int cupidimage_pdf_crypto_aes(const unsigned char *key, size_t key_length, unsigned char iv[16],
                              unsigned char *data, size_t length, int encrypt) {
    if (!key || !iv || (length && !data) || length % 16 ||
        (key_length != 16 && key_length != 24 && key_length != 32) ||
        (encrypt != 0 && encrypt != 1))
        return 0;
    if (encrypt) {
        br_aes_ct_cbcenc_keys context;
        br_aes_ct_cbcenc_init(&context, key, key_length);
        br_aes_ct_cbcenc_run(&context, iv, data, length);
        cupidimage_pdf_crypto_erase(&context, sizeof(context));
    } else {
        br_aes_ct_cbcdec_keys context;
        br_aes_ct_cbcdec_init(&context, key, key_length);
        br_aes_ct_cbcdec_run(&context, iv, data, length);
        cupidimage_pdf_crypto_erase(&context, sizeof(context));
    }
    return 1;
}

/* RC4 exists only for the legacy PDF security revisions. */
int cupidimage_pdf_crypto_rc4(const unsigned char *key, size_t key_length, unsigned char *data,
                              size_t length) {
    if (!key || !key_length || key_length > 16 || (length && !data))
        return 0;
    unsigned char state[256];
    for (unsigned i = 0; i < 256; i++)
        state[i] = (unsigned char)i;
    unsigned j = 0;
    for (unsigned i = 0; i < 256; i++) {
        j = (j + state[i] + key[i % key_length]) & 255;
        unsigned char swap = state[i];
        state[i] = state[j];
        state[j] = swap;
    }
    unsigned i = 0;
    j = 0;
    for (size_t at = 0; at < length; at++) {
        i = (i + 1) & 255;
        j = (j + state[i]) & 255;
        unsigned char swap = state[i];
        state[i] = state[j];
        state[j] = swap;
        data[at] ^= state[(state[i] + state[j]) & 255];
    }
    cupidimage_pdf_crypto_erase(state, sizeof(state));
    return 1;
}
