#include "security.h"
#include <stdlib.h>
#include <string.h>

int cupidimage_pdf_security_decrypt(const pdf_security *security, pdf_object_id identity,
                                    pdf_crypt_method method, const unsigned char *input,
                                    size_t length, unsigned char **output, size_t *output_length) {
    *output = NULL;
    *output_length = 0;
    if (length > 64u * 1024u * 1024u || (length && !input) || method < PDF_CRYPT_IDENTITY ||
        method > PDF_CRYPT_AES256 ||
        (method != PDF_CRYPT_IDENTITY && (!security || !security->authenticated)))
        return 0;
    unsigned char digest[64] = {0}, suffix[5], iv[16];
    size_t key_length = 0, skip = 0;
    if (method == PDF_CRYPT_AES128 || method == PDF_CRYPT_AES256) {
        if (length < 32 || length % 16)
            return 0;
        memcpy(iv, input, 16);
        skip = 16;
    }
    if (method == PDF_CRYPT_AES256) {
        key_length = 32;
        memcpy(digest, security->key, key_length);
    } else if (method != PDF_CRYPT_IDENTITY) {
        uint32_t number = PDF_OBJECT_NUMBER(identity), generation = PDF_OBJECT_GENERATION(identity);
        suffix[0] = (unsigned char)number;
        suffix[1] = (unsigned char)(number >> 8);
        suffix[2] = (unsigned char)(number >> 16);
        suffix[3] = (unsigned char)generation;
        suffix[4] = (unsigned char)(generation >> 8);
        pdf_crypto_slice parts[] = {{security->key, security->parameters.key_length},
                                    {suffix, 5},
                                    {"sAlT", method == PDF_CRYPT_AES128 ? 4 : 0}};
        cupidimage_pdf_crypto_digest(PDF_CRYPTO_MD5, parts, 3, digest);
        key_length = security->parameters.key_length + 5;
        if (key_length > 16)
            key_length = 16;
    }
    size_t decoded_length = length - skip;
    unsigned char *decoded = malloc(decoded_length ? decoded_length : 1);
    int ok = decoded != NULL;
    if (ok && decoded_length)
        memcpy(decoded, input + skip, decoded_length);
    if (ok && method == PDF_CRYPT_RC4)
        ok = cupidimage_pdf_crypto_rc4(digest, key_length, decoded, decoded_length);
    else if (ok && skip) {
        ok = cupidimage_pdf_crypto_aes(digest, key_length, iv, decoded, decoded_length, 0);
        unsigned padding = decoded[decoded_length - 1], mismatch = 0;
        for (unsigned i = 0; i < 16; i++)
            mismatch |= i < padding ? decoded[decoded_length - 1 - i] ^ padding : 0;
        ok = ok && padding > 0 && padding <= 16 && !mismatch;
        if (ok)
            decoded_length -= padding;
    }
    cupidimage_pdf_crypto_erase(digest, sizeof(digest));
    cupidimage_pdf_crypto_erase(iv, sizeof(iv));
    if (!ok) {
        if (decoded)
            cupidimage_pdf_crypto_erase(decoded, length - skip);
        free(decoded);
        return 0;
    }
    *output = decoded;
    *output_length = decoded_length;
    return 1;
}
