#include "formats/pdf/security/crypto.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int digit(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static size_t unhex(const char *text, unsigned char *bytes, size_t capacity) {
    if (!strcmp(text, "-"))
        return 0;
    size_t length = strlen(text);
    assert(!(length % 2) && length / 2 <= capacity);
    for (size_t i = 0; i < length / 2; i++) {
        int high = digit(text[i * 2]), low = digit(text[i * 2 + 1]);
        assert(high >= 0 && low >= 0);
        bytes[i] = (unsigned char)(high * 16 + low);
    }
    return length / 2;
}

static void boundaries(void) {
    unsigned char key[32] = {0}, iv[16] = {0}, data[64] = {0};
    assert(!cupidimage_pdf_crypto_aes(key, 15, iv, data, 16, 1));
    assert(!cupidimage_pdf_crypto_aes(key, 16, iv, data, 15, 1));
    assert(!cupidimage_pdf_crypto_aes(key, 16, iv, data, 16, 2));
    assert(!cupidimage_pdf_crypto_aes(NULL, 16, iv, data, 16, 1));
    assert(!cupidimage_pdf_crypto_aes(key, 16, NULL, data, 16, 1));
    assert(!cupidimage_pdf_crypto_aes(key, 16, iv, NULL, 16, 1));
    assert(cupidimage_pdf_crypto_aes(key, 16, iv, NULL, 0, 1));
    assert(!cupidimage_pdf_crypto_rc4(key, 0, data, 16));
    assert(!cupidimage_pdf_crypto_rc4(key, 17, data, 16));
    assert(!cupidimage_pdf_crypto_rc4(NULL, 5, data, 16));
    assert(!cupidimage_pdf_crypto_rc4(key, 5, NULL, 16));
    assert(cupidimage_pdf_crypto_rc4(key, 5, NULL, 0));
    assert(!cupidimage_pdf_crypto_digest(PDF_CRYPTO_MD5, NULL, 1, data));
    assert(!cupidimage_pdf_crypto_digest(PDF_CRYPTO_MD5, NULL, 0, NULL));
    assert(!cupidimage_pdf_crypto_digest((pdf_crypto_hash)99, NULL, 0, data));
    pdf_crypto_slice invalid = {NULL, 1};
    assert(!cupidimage_pdf_crypto_digest(PDF_CRYPTO_MD5, &invalid, 1, data));
    memset(data, 0xa5, sizeof(data));
    cupidimage_pdf_crypto_erase(data, sizeof(data));
    for (size_t i = 0; i < sizeof(data); i++)
        assert(!data[i]);
    assert(cupidimage_pdf_crypto_equal(key, data, sizeof(key)));
    for (size_t i = 0; i < sizeof(key); i++) {
        data[i] = 1;
        assert(!cupidimage_pdf_crypto_equal(key, data, sizeof(key)));
        data[i] = 0;
    }
    assert(cupidimage_pdf_crypto_equal(NULL, NULL, 0));
}

int main(void) {
    boundaries();
    char operation[16], key_hex[129], iv_hex[129], data_hex[65537];
    unsigned char key[64], iv[64], data[32768], output[64];
    size_t split;
    while (scanf("%15s %128s %128s %65536s %zu", operation, key_hex, iv_hex, data_hex, &split) ==
           5) {
        size_t key_length = unhex(key_hex, key, sizeof(key));
        size_t iv_length = unhex(iv_hex, iv, sizeof(iv));
        size_t length = unhex(data_hex, data, sizeof(data));
        if (!strcmp(operation, "aesenc") || !strcmp(operation, "aesdec")) {
            assert(iv_length == 16);
            assert(cupidimage_pdf_crypto_aes(key, key_length, iv, data, length,
                                             !strcmp(operation, "aesenc")));
        } else if (!strcmp(operation, "rc4")) {
            assert(cupidimage_pdf_crypto_rc4(key, key_length, data, length));
        } else {
            pdf_crypto_hash kind = !strcmp(operation, "md5")      ? PDF_CRYPTO_MD5
                                   : !strcmp(operation, "sha256") ? PDF_CRYPTO_SHA256
                                   : !strcmp(operation, "sha384") ? PDF_CRYPTO_SHA384
                                                                  : PDF_CRYPTO_SHA512;
            assert(split <= length);
            pdf_crypto_slice parts[] = {{data, split}, {NULL, 0}, {data + split, length - split}};
            assert(cupidimage_pdf_crypto_digest(kind, parts, 3, output));
            length = kind == PDF_CRYPTO_MD5      ? 16
                     : kind == PDF_CRYPTO_SHA256 ? 32
                     : kind == PDF_CRYPTO_SHA384 ? 48
                                                 : 64;
            memcpy(data, output, length);
        }
        for (size_t i = 0; i < length; i++)
            printf("%02x", data[i]);
        putchar('\n');
    }
    return ferror(stdin) ? 1 : 0;
}
