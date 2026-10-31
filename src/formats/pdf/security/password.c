#include "password.h"
#include "crypto.h"
#include "password_data.inc"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PASSWORD_COUNT(a) (sizeof(a) / sizeof(*(a)))
#define PASSWORD_INPUT_LIMIT 4096u

typedef struct {
    uint32_t scalar;
    unsigned ccc;
} password_character;

static unsigned password_properties(uint32_t scalar) {
    size_t lo = 0, hi = PASSWORD_COUNT(password_flags);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (password_flags[mid][1] < scalar)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < PASSWORD_COUNT(password_flags) && password_flags[lo][0] <= scalar
               ? password_flags[lo][2]
               : 0;
}

static unsigned password_combining(uint32_t scalar) {
    size_t lo = 0, hi = PASSWORD_COUNT(password_ccc);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (password_ccc[mid][0] < scalar)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < PASSWORD_COUNT(password_ccc) && password_ccc[lo][0] == scalar ? password_ccc[lo][1]
                                                                              : 0;
}

static int password_read_utf8(const unsigned char *input, size_t length, size_t *at,
                              uint32_t *out) {
    unsigned char lead = input[(*at)++];
    if (lead < 128) {
        *out = lead;
        return 1;
    }
    unsigned count = lead >= 0xc2 && lead <= 0xdf   ? 1
                     : lead >= 0xe0 && lead <= 0xef ? 2
                     : lead >= 0xf0 && lead <= 0xf4 ? 3
                                                    : 0;
    if (!count || count > length - *at)
        return 0;
    uint32_t scalar = lead & ((1u << (6 - count)) - 1);
    for (unsigned i = 0; i < count; i++) {
        unsigned char byte = input[(*at)++];
        if ((byte & 0xc0) != 0x80)
            return 0;
        scalar = scalar * 64 + (byte & 63);
    }
    if ((count == 1 && scalar < 0x80) || (count == 2 && scalar < 0x800) ||
        (count == 3 && scalar < 0x10000) || scalar > 0x10ffff ||
        (scalar >= 0xd800 && scalar <= 0xdfff))
        return 0;
    *out = scalar;
    return 1;
}

static int password_legacy_byte(uint32_t scalar) {
    static const uint16_t low[] = {0x2d8, 0x2c7, 0x2c6, 0x2d9, 0x2dd, 0x2db, 0x2da, 0x2dc};
    static const uint16_t high[] = {0x2022, 0x2020, 0x2021, 0x2026, 0x2014, 0x2013, 0x192,  0x2044,
                                    0x2039, 0x203a, 0x2212, 0x2030, 0x201e, 0x201c, 0x201d, 0x2018,
                                    0x2019, 0x201a, 0x2122, 0xfb01, 0xfb02, 0x141,  0x152,  0x160,
                                    0x178,  0x17d,  0x131,  0x142,  0x153,  0x161,  0x17e};
    if ((scalar >= 32 && scalar <= 126) || (scalar >= 161 && scalar <= 255 && scalar != 173) ||
        scalar == 9 || scalar == 10 || scalar == 13)
        return (int)scalar;
    for (unsigned i = 0; i < PASSWORD_COUNT(low); i++)
        if (low[i] == scalar)
            return 24 + (int)i;
    for (unsigned i = 0; i < PASSWORD_COUNT(high); i++)
        if (high[i] == scalar)
            return 128 + (int)i;
    return scalar == 0x20ac ? 160 : -1;
}

static void password_append(password_character *characters, size_t *count, uint32_t scalar) {
    unsigned ccc = password_combining(scalar);
    size_t at = (*count)++;
    while (at && ccc && characters[at - 1].ccc > ccc) {
        characters[at] = characters[at - 1];
        at--;
    }
    characters[at] = (password_character){scalar, ccc};
}

static void password_append_decomposed(password_character *characters, size_t *count,
                                       uint32_t scalar) {
    if (scalar >= 0xac00 && scalar < 0xd7a4) {
        unsigned offset = scalar - 0xac00;
        password_append(characters, count, 0x1100 + offset / 588);
        password_append(characters, count, 0x1161 + (offset % 588) / 28);
        if (offset % 28)
            password_append(characters, count, 0x11a7 + offset % 28);
        return;
    }
    size_t lo = 0, hi = PASSWORD_COUNT(password_decompose);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (password_decompose[mid][0] < scalar)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < PASSWORD_COUNT(password_decompose) && password_decompose[lo][0] == scalar) {
        for (unsigned i = 0; i < password_decompose[lo][2]; i++)
            password_append(characters, count, password_decomposed[password_decompose[lo][1] + i]);
    } else
        password_append(characters, count, scalar);
}

static uint32_t password_compose_pair(uint32_t first, uint32_t second) {
    if (first >= 0x1100 && first < 0x1113 && second >= 0x1161 && second < 0x1176)
        return 0xac00 + ((first - 0x1100) * 21 + second - 0x1161) * 28;
    if (first >= 0xac00 && first < 0xd7a4 && (first - 0xac00) % 28 == 0 && second > 0x11a7 &&
        second < 0x11c3)
        return first + second - 0x11a7;
    size_t lo = 0, hi = PASSWORD_COUNT(password_compose);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (password_compose[mid][0] < first ||
            (password_compose[mid][0] == first && password_compose[mid][1] < second))
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < PASSWORD_COUNT(password_compose) && password_compose[lo][0] == first &&
                   password_compose[lo][1] == second
               ? password_compose[lo][2]
               : 0;
}

static size_t password_write_utf8(uint32_t scalar, unsigned char out[4]) {
    if (scalar < 0x80) {
        out[0] = (unsigned char)scalar;
        return 1;
    }
    size_t count = scalar < 0x800 ? 2 : scalar < 0x10000 ? 3 : 4;
    for (size_t i = count - 1; i; i--) {
        out[i] = (unsigned char)(0x80 | (scalar & 63));
        scalar >>= 6;
    }
    out[0] = (unsigned char)((count == 2 ? 0xc0 : count == 3 ? 0xe0 : 0xf0) | scalar);
    return count;
}

int cupidimage_pdf_security_password(unsigned revision, const unsigned char *input, size_t length,
                                     int raw_legacy, unsigned char output[127],
                                     size_t *output_length) {
    if (!output || !output_length)
        return 0;
    *output_length = 0;
    if (revision < 2 || revision > 6 || length > PASSWORD_INPUT_LIMIT || (length && !input) ||
        (raw_legacy != 0 && raw_legacy != 1) || (raw_legacy && revision >= 5))
        return 0;
    if (raw_legacy) {
        *output_length = length < 32 ? length : 32;
        if (*output_length)
            memcpy(output, input, *output_length);
        return 1;
    }
    /* Unicode 3.2 NFKD has at most 18 scalars per input character. */
    size_t capacity = length * 18;
    password_character *characters =
        revision >= 5 && length ? malloc(capacity * sizeof(*characters)) : NULL;
    if (revision >= 5 && length && !characters)
        return 0;
    size_t at = 0, count = 0, used = 0;
    int valid = 1;
    while (at < length) {
        uint32_t scalar;
        if (!password_read_utf8(input, length, &at, &scalar)) {
            valid = 0;
            break;
        }
        if (revision <= 4) {
            int byte = password_legacy_byte(scalar);
            if (byte < 0) {
                valid = 0;
                break;
            }
            if (used < 32)
                output[used++] = (unsigned char)byte;
        } else if (revision >= 5) {
            unsigned flags = password_properties(scalar);
            if (flags & 1)
                continue;
            password_append_decomposed(characters, &count, flags & 2 ? 32 : scalar);
        }
    }
    if (valid && revision >= 5) {
        size_t composed = 0, starter = SIZE_MAX;
        unsigned previous_ccc = 0;
        for (size_t i = 0; i < count; i++) {
            password_character current = characters[i];
            uint32_t combination =
                starter != SIZE_MAX && (!previous_ccc || previous_ccc < current.ccc)
                    ? password_compose_pair(characters[starter].scalar, current.scalar)
                    : 0;
            if (combination)
                characters[starter].scalar = combination;
            else {
                if (!current.ccc)
                    starter = composed;
                characters[composed++] = current;
                previous_ccc = current.ccc;
            }
        }
        unsigned bidi = 0;
        for (size_t i = 0; i < composed; i++) {
            unsigned flags = password_properties(characters[i].scalar);
            if (flags & (4 | 2))
                valid = 0;
            bidi |= flags;
            unsigned char encoded[4];
            size_t bytes = password_write_utf8(characters[i].scalar, encoded);
            if (bytes > 127 - used)
                bytes = 127 - used;
            if (bytes)
                memcpy(output + used, encoded, bytes);
            used += bytes;
        }
        if (bidi & 8)
            valid = valid && !(bidi & 16) && (password_properties(characters[0].scalar) & 8) &&
                    (password_properties(characters[composed - 1].scalar) & 8);
    }
    if (characters) {
        cupidimage_pdf_crypto_erase(characters, capacity * sizeof(*characters));
        free(characters);
    }
    if (!valid) {
        cupidimage_pdf_crypto_erase(output, used);
        return 0;
    }
    *output_length = used;
    return 1;
}
