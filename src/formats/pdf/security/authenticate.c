#include "authenticate.h"
#include <string.h>

static const unsigned char security_padding[32] = {
    0x28, 0xbf, 0x4e, 0x5e, 0x4e, 0x75, 0x8a, 0x41, 0x64, 0x00, 0x4e, 0x56, 0xff, 0xfa, 0x01, 0x08,
    0x2e, 0x2e, 0x00, 0xb6, 0xd0, 0x68, 0x3e, 0x80, 0x2f, 0x0c, 0xa9, 0xfe, 0x64, 0x53, 0x69, 0x7a};

static void security_padded(const unsigned char *password, size_t length, unsigned char out[32]) {
    if (length > 32)
        length = 32;
    if (length)
        memcpy(out, password, length);
    memcpy(out + length, security_padding, 32 - length);
}

static int security_legacy_user(const pdf_security_parameters *p, const unsigned char password[32],
                                unsigned char key[32]) {
    unsigned char permissions[4], digest[64], check[64], round_key[16];
    for (unsigned i = 0; i < 4; i++)
        permissions[i] = (unsigned char)(p->permissions >> (8 * i));
    static const unsigned char no_metadata[4] = {255, 255, 255, 255};
    pdf_crypto_slice parts[] = {{password, 32},
                                {p->owner, 32},
                                {permissions, 4},
                                p->identifier,
                                {no_metadata, p->revision >= 4 && !p->encrypt_metadata ? 4 : 0}};
    cupidimage_pdf_crypto_digest(PDF_CRYPTO_MD5, parts, 5, digest);
    pdf_crypto_slice previous = {digest, p->key_length};
    if (p->revision >= 3)
        for (unsigned i = 0; i < 50; i++)
            cupidimage_pdf_crypto_digest(PDF_CRYPTO_MD5, &previous, 1, digest);
    memcpy(key, digest, p->key_length);
    size_t checked = p->revision == 2 ? 32 : 16;
    if (p->revision == 2) {
        memcpy(check, security_padding, 32);
        cupidimage_pdf_crypto_rc4(key, p->key_length, check, 32);
    } else {
        pdf_crypto_slice values[] = {{security_padding, 32}, p->identifier};
        cupidimage_pdf_crypto_digest(PDF_CRYPTO_MD5, values, 2, check);
        for (unsigned round = 0; round < 20; round++) {
            for (unsigned i = 0; i < p->key_length; i++)
                round_key[i] = key[i] ^ round;
            cupidimage_pdf_crypto_rc4(round_key, p->key_length, check, 16);
        }
    }
    int valid = cupidimage_pdf_crypto_equal(check, p->user, checked);
    cupidimage_pdf_crypto_erase(digest, sizeof(digest));
    cupidimage_pdf_crypto_erase(check, sizeof(check));
    cupidimage_pdf_crypto_erase(round_key, sizeof(round_key));
    return valid;
}

static int security_legacy_authenticate(const pdf_security_parameters *p,
                                        const unsigned char *password, size_t length,
                                        unsigned char key[32], int *owner) {
    unsigned char input[32], digest[64], round_key[16], recovered[32];
    security_padded(password, length, input);
    pdf_crypto_slice value = {input, sizeof(input)};
    cupidimage_pdf_crypto_digest(PDF_CRYPTO_MD5, &value, 1, digest);
    value = (pdf_crypto_slice){digest, 16};
    if (p->revision >= 3)
        for (unsigned i = 0; i < 50; i++)
            cupidimage_pdf_crypto_digest(PDF_CRYPTO_MD5, &value, 1, digest);
    memcpy(recovered, p->owner, sizeof(recovered));
    for (int round = p->revision == 2 ? 0 : 19; round >= 0; round--) {
        for (unsigned i = 0; i < p->key_length; i++)
            round_key[i] = digest[i] ^ (unsigned)round;
        cupidimage_pdf_crypto_rc4(round_key, p->key_length, recovered, sizeof(recovered));
    }
    int valid = security_legacy_user(p, recovered, key);
    *owner = valid;
    if (!valid)
        valid = security_legacy_user(p, input, key);
    cupidimage_pdf_crypto_erase(input, sizeof(input));
    cupidimage_pdf_crypto_erase(digest, sizeof(digest));
    cupidimage_pdf_crypto_erase(round_key, sizeof(round_key));
    cupidimage_pdf_crypto_erase(recovered, sizeof(recovered));
    return valid;
}

int cupidimage_pdf_security_hash6(const unsigned char *password, size_t length,
                                  const unsigned char salt[8], const unsigned char *user,
                                  unsigned char output[32]) {
    if (length > 127 || (length && !password) || !salt || !output)
        return 0;
    unsigned char hash[64], block[64 * (127 + 64 + 48)], iv[16];
    pdf_crypto_slice initial[] = {{password, length}, {salt, 8}, {user, user ? 48 : 0}};
    cupidimage_pdf_crypto_digest(PDF_CRYPTO_SHA256, initial, 3, hash);
    size_t hash_length = 32;
    unsigned rounds = 0;
    unsigned char last;
    do {
        size_t unit = length + hash_length + (user ? 48 : 0);
        if (length)
            memcpy(block, password, length);
        memcpy(block + length, hash, hash_length);
        if (user)
            memcpy(block + length + hash_length, user, 48);
        for (unsigned i = 1; i < 64; i++)
            memcpy(block + i * unit, block, unit);
        memcpy(iv, hash + 16, sizeof(iv));
        cupidimage_pdf_crypto_aes(hash, 16, iv, block, unit * 64, 1);
        unsigned remainder = 0;
        for (unsigned i = 0; i < 16; i++)
            remainder = (remainder * 256 + block[i]) % 3;
        pdf_crypto_hash kind = remainder == 0   ? PDF_CRYPTO_SHA256
                               : remainder == 1 ? PDF_CRYPTO_SHA384
                                                : PDF_CRYPTO_SHA512;
        pdf_crypto_slice encrypted = {block, unit * 64};
        last = block[unit * 64 - 1];
        cupidimage_pdf_crypto_digest(kind, &encrypted, 1, hash);
        hash_length = remainder == 0 ? 32 : remainder == 1 ? 48 : 64;
        rounds++;
    } while (rounds < 64 || last > rounds - 32);
    memcpy(output, hash, 32);
    cupidimage_pdf_crypto_erase(hash, sizeof(hash));
    cupidimage_pdf_crypto_erase(block, sizeof(block));
    cupidimage_pdf_crypto_erase(iv, sizeof(iv));
    return 1;
}

static void security_modern_hash(unsigned revision, const unsigned char *password, size_t length,
                                 const unsigned char salt[8], const unsigned char *user,
                                 unsigned char output[64]) {
    if (revision == 6)
        cupidimage_pdf_security_hash6(password, length, salt, user, output);
    else {
        pdf_crypto_slice parts[] = {{password, length}, {salt, 8}, {user, user ? 48 : 0}};
        cupidimage_pdf_crypto_digest(PDF_CRYPTO_SHA256, parts, 3, output);
    }
}

static int security_modern_authenticate(const pdf_security_parameters *p,
                                        const unsigned char *password, size_t length,
                                        unsigned char key[32], int *owner) {
    unsigned char hash[64], iv[16] = {0}, perms[16];
    if (length > 127)
        length = 127;
    security_modern_hash(p->revision, password, length, p->owner + 32, p->user, hash);
    int valid = cupidimage_pdf_crypto_equal(hash, p->owner, 32);
    *owner = valid;
    if (valid) {
        security_modern_hash(p->revision, password, length, p->owner + 40, p->user, hash);
        memcpy(key, p->owner_key, 32);
    } else {
        security_modern_hash(p->revision, password, length, p->user + 32, NULL, hash);
        valid = cupidimage_pdf_crypto_equal(hash, p->user, 32);
        if (valid) {
            security_modern_hash(p->revision, password, length, p->user + 40, NULL, hash);
            memcpy(key, p->user_key, 32);
        }
    }
    if (valid) {
        cupidimage_pdf_crypto_aes(hash, 32, iv, key, 32, 0);
        memset(iv, 0, sizeof(iv));
        memcpy(perms, p->perms, sizeof(perms));
        /* One CBC block with a zero IV is the required ECB operation. */
        cupidimage_pdf_crypto_aes(key, 32, iv, perms, sizeof(perms), 0);
        uint32_t permissions = (uint32_t)perms[0] | (uint32_t)perms[1] << 8 |
                               (uint32_t)perms[2] << 16 | (uint32_t)perms[3] << 24;
        valid = permissions == p->permissions && perms[4] == 255 && perms[5] == 255 &&
                perms[6] == 255 && perms[7] == 255 &&
                perms[8] == (p->encrypt_metadata ? 'T' : 'F') && perms[9] == 'a' &&
                perms[10] == 'd' && perms[11] == 'b';
    }
    cupidimage_pdf_crypto_erase(hash, sizeof(hash));
    cupidimage_pdf_crypto_erase(iv, sizeof(iv));
    cupidimage_pdf_crypto_erase(perms, sizeof(perms));
    return valid;
}

int cupidimage_pdf_security_authenticate(const pdf_security_parameters *p,
                                         const unsigned char *password, size_t length,
                                         unsigned char key[32], int *owner) {
    if (!p || !key || !owner)
        return 0;
    memset(key, 0, 32);
    *owner = 0;
    if ((length && !password) || p->revision < 2 || p->revision > 6 ||
        (p->encrypt_metadata != 0 && p->encrypt_metadata != 1) ||
        (p->revision == 2 && p->key_length != 5) ||
        (p->revision >= 3 && p->revision <= 4 && (p->key_length < 5 || p->key_length > 16)) ||
        (p->revision >= 5 && p->key_length != 32) ||
        (p->revision <= 4 && p->identifier.length && !p->identifier.data))
        return 0;
    int valid = p->revision <= 4 ? security_legacy_authenticate(p, password, length, key, owner)
                                 : security_modern_authenticate(p, password, length, key, owner);
    if (!valid) {
        cupidimage_pdf_crypto_erase(key, 32);
        *owner = 0;
    }
    return valid;
}
