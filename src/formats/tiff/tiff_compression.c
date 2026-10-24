#include "tiff_internal.h"

static void ccitt_set_run(uint8_t *row, uint32_t start, uint32_t run, int color);
static int ccitt_get_bit(const uint8_t *row, uint32_t x);
static uint32_t ccitt_next_transition(const uint8_t *row, uint32_t width, uint32_t start, int color);

int cupidimage_tiff_packbits_decode(const uint8_t *src, size_t src_size,
                                uint8_t *dst, size_t dst_size,
                                char *err, size_t errcap) {
    size_t pos = 0;
    size_t out = 0;
    while (out < dst_size) {
        if (pos >= src_size) {
            cupidimage_set_err(err, errcap, "truncated PackBits data");
            return 0;
        }
        int8_t n = (int8_t)src[pos++];
        if (n >= 0) {
            size_t count = (size_t)n + 1u;
            if (pos + count > src_size || out + count > dst_size) {
                cupidimage_set_err(err, errcap, "invalid PackBits data");
                return 0;
            }
            memcpy(dst + out, src + pos, count);
            pos += count;
            out += count;
        } else if (n != -128) {
            size_t count = (size_t)(1 - n);
            if (pos >= src_size || out + count > dst_size) {
                cupidimage_set_err(err, errcap, "invalid PackBits data");
                return 0;
            }
            uint8_t v = src[pos++];
            for (size_t i = 0; i < count; i++) {
                dst[out++] = v;
            }
        }
    }
    return 1;
}

typedef struct tiff_lzw {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t bitbuf;
    int bitcount;
} tiff_lzw;

static int tiff_lzw_read_code(tiff_lzw *lz, int code_size, uint32_t *out) {
    while (lz->bitcount < code_size) {
        if (lz->pos >= lz->size) {
            return 0;
        }
        lz->bitbuf = (lz->bitbuf << 8) | lz->data[lz->pos++];
        lz->bitcount += 8;
    }
    lz->bitcount -= code_size;
    *out = (lz->bitbuf >> lz->bitcount) & ((1u << code_size) - 1u);
    return 1;
}

int cupidimage_tiff_lzw_decode(const uint8_t *src, size_t src_size,
                           uint8_t *dst, size_t dst_size,
                           char *err, size_t errcap) {
    uint16_t prefix[4096];
    uint8_t suffix[4096];
    uint8_t stack[4096];

    for (int i = 0; i < 4096; i++) {
        prefix[i] = 0;
        suffix[i] = 0;
    }

    const int clear_code = 256;
    const int end_code = 257;
    int code_size = 9;
    int next_code = 258;
    int early_change = 1;

    tiff_lzw lz;
    lz.data = src;
    lz.size = src_size;
    lz.pos = 0;
    lz.bitbuf = 0;
    lz.bitcount = 0;

    size_t out = 0;
    int old_code = -1;
    uint8_t first_char = 0;

    while (1) {
        uint32_t code = 0;
        if (!tiff_lzw_read_code(&lz, code_size, &code)) {
            break;
        }

        if ((int)code == clear_code) {
            code_size = 9;
            next_code = 258;
            old_code = -1;
            continue;
        }
        if ((int)code == end_code) {
            break;
        }

        uint32_t curr = code;
        int stack_top = 0;

        if (old_code == -1) {
            if (code >= 256) {
                cupidimage_set_err(err, errcap, "invalid LZW stream");
                return 0;
            }
            if (out >= dst_size) {
                cupidimage_set_err(err, errcap, "LZW output overflow");
                return 0;
            }
            dst[out++] = (uint8_t)code;
            first_char = (uint8_t)code;
            old_code = (int)code;
            continue;
        }

        if (code >= (uint32_t)next_code) {
            if ((int)code != next_code) {
                cupidimage_set_err(err, errcap, "invalid LZW code");
                return 0;
            }
            curr = (uint32_t)old_code;
            stack[stack_top++] = first_char;
        }

        while (curr >= 256) {
            if (stack_top >= 4096) {
                cupidimage_set_err(err, errcap, "LZW stack overflow");
                return 0;
            }
            stack[stack_top++] = suffix[curr];
            curr = prefix[curr];
        }
        if (stack_top >= 4096) {
            cupidimage_set_err(err, errcap, "LZW stack overflow");
            return 0;
        }
        stack[stack_top++] = (uint8_t)curr;
        first_char = (uint8_t)curr;

        while (stack_top > 0) {
            if (out >= dst_size) {
                cupidimage_set_err(err, errcap, "LZW output overflow");
                return 0;
            }
            dst[out++] = stack[--stack_top];
        }

        if (next_code < 4096) {
            prefix[next_code] = (uint16_t)old_code;
            suffix[next_code] = first_char;
            next_code++;
            if (next_code >= ((1 << code_size) - early_change) && code_size < 12) {
                code_size++;
            }
        }

        old_code = (int)code;
    }

    if (out != dst_size) {
        cupidimage_set_err(err, errcap, "LZW output size mismatch");
        return 0;
    }
    return 1;
}

typedef struct ccitt_code {
    uint16_t code;
    uint8_t length;
    uint16_t run;
} ccitt_code;

static const ccitt_code ccitt_white_term[] = {
    {0x35, 8, 0}, {0x7, 6, 1}, {0x7, 4, 2}, {0x8, 4, 3},
    {0xB, 4, 4}, {0xC, 4, 5}, {0xE, 4, 6}, {0xF, 4, 7},
    {0x13, 5, 8}, {0x14, 5, 9}, {0x7, 5, 10}, {0x8, 5, 11},
    {0x8, 6, 12}, {0x3, 6, 13}, {0x34, 6, 14}, {0x35, 6, 15},
    {0x2A, 6, 16}, {0x2B, 6, 17}, {0x27, 7, 18}, {0xC, 7, 19},
    {0x8, 7, 20}, {0x17, 7, 21}, {0x3, 7, 22}, {0x4, 7, 23},
    {0x28, 7, 24}, {0x2B, 7, 25}, {0x13, 7, 26}, {0x24, 7, 27},
    {0x18, 7, 28}, {0x2, 8, 29}, {0x3, 8, 30}, {0x1A, 8, 31},
    {0x1B, 8, 32}, {0x12, 8, 33}, {0x13, 8, 34}, {0x14, 8, 35},
    {0x15, 8, 36}, {0x16, 8, 37}, {0x17, 8, 38}, {0x28, 8, 39},
    {0x29, 8, 40}, {0x2A, 8, 41}, {0x2B, 8, 42}, {0x2C, 8, 43},
    {0x2D, 8, 44}, {0x4, 8, 45}, {0x5, 8, 46}, {0xA, 8, 47},
    {0xB, 8, 48}, {0x52, 8, 49}, {0x53, 8, 50}, {0x54, 8, 51},
    {0x55, 8, 52}, {0x24, 8, 53}, {0x25, 8, 54}, {0x58, 8, 55},
    {0x59, 8, 56}, {0x5A, 8, 57}, {0x5B, 8, 58}, {0x4A, 8, 59},
    {0x4B, 8, 60}, {0x32, 8, 61}, {0x33, 8, 62}, {0x34, 8, 63}
};

static const ccitt_code ccitt_white_makeup[] = {
    {0x1B, 5, 64}, {0x12, 5, 128}, {0x17, 6, 192}, {0x37, 7, 256},
    {0x36, 8, 320}, {0x37, 8, 384}, {0x64, 8, 448}, {0x65, 8, 512},
    {0x68, 8, 576}, {0x67, 8, 640}, {0xCC, 9, 704}, {0xCD, 9, 768},
    {0xD2, 9, 832}, {0xD3, 9, 896}, {0xD4, 9, 960}, {0xD5, 9, 1024},
    {0xD6, 9, 1088}, {0xD7, 9, 1152}, {0xD8, 9, 1216}, {0xD9, 9, 1280},
    {0xDA, 9, 1344}, {0xDB, 9, 1408}, {0x98, 9, 1472}, {0x99, 9, 1536},
    {0x9A, 9, 1600}, {0x18, 6, 1664}, {0x9B, 9, 1728}, {0x8, 11, 1792},
    {0xC, 11, 1856}, {0xD, 11, 1920}, {0x12, 12, 1984}, {0x13, 12, 2048},
    {0x14, 12, 2112}, {0x15, 12, 2176}, {0x16, 12, 2240}, {0x17, 12, 2304},
    {0x1C, 12, 2368}, {0x1D, 12, 2432}, {0x1E, 12, 2496}, {0x1F, 12, 2560}
};

static const ccitt_code ccitt_black_term[] = {
    {0x37, 10, 0}, {0x2, 3, 1}, {0x3, 2, 2}, {0x2, 2, 3},
    {0x3, 3, 4}, {0x3, 4, 5}, {0x2, 4, 6}, {0x3, 5, 7},
    {0x5, 6, 8}, {0x4, 6, 9}, {0x4, 7, 10}, {0x5, 7, 11},
    {0x7, 7, 12}, {0x4, 8, 13}, {0x7, 8, 14}, {0x18, 9, 15},
    {0x17, 10, 16}, {0x18, 10, 17}, {0x8, 10, 18}, {0x67, 11, 19},
    {0x68, 11, 20}, {0x6C, 11, 21}, {0x37, 11, 22}, {0x28, 11, 23},
    {0x17, 11, 24}, {0x18, 11, 25}, {0xCA, 12, 26}, {0xCB, 12, 27},
    {0xCC, 12, 28}, {0xCD, 12, 29}, {0x68, 12, 30}, {0x69, 12, 31},
    {0x6A, 12, 32}, {0x6B, 12, 33}, {0xD2, 12, 34}, {0xD3, 12, 35},
    {0xD4, 12, 36}, {0xD5, 12, 37}, {0xD6, 12, 38}, {0xD7, 12, 39},
    {0x6C, 12, 40}, {0x6D, 12, 41}, {0xDA, 12, 42}, {0xDB, 12, 43},
    {0x54, 12, 44}, {0x55, 12, 45}, {0x56, 12, 46}, {0x57, 12, 47},
    {0x64, 12, 48}, {0x65, 12, 49}, {0x52, 12, 50}, {0x53, 12, 51},
    {0x24, 12, 52}, {0x37, 12, 53}, {0x38, 12, 54}, {0x27, 12, 55},
    {0x28, 12, 56}, {0x58, 12, 57}, {0x59, 12, 58}, {0x2B, 12, 59},
    {0x2C, 12, 60}, {0x5A, 12, 61}, {0x66, 12, 62}, {0x67, 12, 63}
};

static const ccitt_code ccitt_black_makeup[] = {
    {0xF, 10, 64}, {0xC8, 12, 128}, {0xC9, 12, 192}, {0x5B, 12, 256},
    {0x33, 12, 320}, {0x34, 12, 384}, {0x35, 12, 448}, {0x6C, 13, 512},
    {0x6D, 13, 576}, {0x4A, 13, 640}, {0x4B, 13, 704}, {0x4C, 13, 768},
    {0x4D, 13, 832}, {0x72, 13, 896}, {0x73, 13, 960}, {0x74, 13, 1024},
    {0x75, 13, 1088}, {0x76, 13, 1152}, {0x77, 13, 1216}, {0x52, 13, 1280},
    {0x53, 13, 1344}, {0x54, 13, 1408}, {0x55, 13, 1472}, {0x5A, 13, 1536},
    {0x5B, 13, 1600}, {0x64, 13, 1664}, {0x65, 13, 1728}, {0x8, 11, 1792},
    {0xC, 11, 1856}, {0xD, 11, 1920}, {0x12, 12, 1984}, {0x13, 12, 2048},
    {0x14, 12, 2112}, {0x15, 12, 2176}, {0x16, 12, 2240}, {0x17, 12, 2304},
    {0x1C, 12, 2368}, {0x1D, 12, 2432}, {0x1E, 12, 2496}, {0x1F, 12, 2560}
};

typedef struct ccitt_bitstream {
    const uint8_t *data;
    size_t size;
    size_t pos;
    int bit;
    int fill_order;
} ccitt_bitstream;

static int ccitt_read_bit(ccitt_bitstream *bs, int *out) {
    if (bs->pos >= bs->size) {
        return 0;
    }
    uint8_t byte = bs->data[bs->pos];
    int shift = bs->fill_order == TIFF_FILL_ORDER_LSB2MSB ? bs->bit : (7 - bs->bit);
    *out = (byte >> shift) & 1;
    bs->bit++;
    if (bs->bit == 8) {
        bs->bit = 0;
        bs->pos++;
    }
    return 1;
}

static int ccitt_decode_single(ccitt_bitstream *bs,
                               const ccitt_code *term, size_t term_len,
                               const ccitt_code *makeup, size_t makeup_len,
                               uint32_t *run, int *is_makeup) {
    uint32_t code = 0;
    for (int len = 1; len <= 13; len++) {
        int bit = 0;
        if (!ccitt_read_bit(bs, &bit)) {
            return 0;
        }
        code = (code << 1) | (uint32_t)bit;
        for (size_t i = 0; i < term_len; i++) {
            if (term[i].length == len && term[i].code == code) {
                *run = term[i].run;
                *is_makeup = 0;
                return 1;
            }
        }
        for (size_t i = 0; i < makeup_len; i++) {
            if (makeup[i].length == len && makeup[i].code == code) {
                *run = makeup[i].run;
                *is_makeup = 1;
                return 1;
            }
        }
    }
    return 0;
}

static int ccitt_decode_run(ccitt_bitstream *bs, int color, uint32_t *run) {
    const ccitt_code *term = color ? ccitt_black_term : ccitt_white_term;
    const ccitt_code *makeup = color ? ccitt_black_makeup : ccitt_white_makeup;
    size_t term_len = color ? (sizeof(ccitt_black_term) / sizeof(ccitt_black_term[0]))
                            : (sizeof(ccitt_white_term) / sizeof(ccitt_white_term[0]));
    size_t makeup_len = color ? (sizeof(ccitt_black_makeup) / sizeof(ccitt_black_makeup[0]))
                              : (sizeof(ccitt_white_makeup) / sizeof(ccitt_white_makeup[0]));
    uint32_t total = 0;
    while (1) {
        uint32_t val = 0;
        int is_makeup = 0;
        if (!ccitt_decode_single(bs, term, term_len, makeup, makeup_len, &val, &is_makeup)) {
            return 0;
        }
        total += val;
        if (!is_makeup) {
            break;
        }
    }
    *run = total;
    return 1;
}

static void ccitt_set_run(uint8_t *row, uint32_t start, uint32_t run, int color) {
    for (uint32_t i = 0; i < run; i++) {
        uint32_t x = start + i;
        uint8_t mask = (uint8_t)(0x80u >> (x & 7u));
        if (color) {
            row[x / 8u] |= mask;
        } else {
            row[x / 8u] &= (uint8_t)~mask;
        }
    }
}

static int ccitt_get_bit(const uint8_t *row, uint32_t x) {
    uint8_t mask = (uint8_t)(0x80u >> (x & 7u));
    return (row[x / 8u] & mask) ? 1 : 0;
}

static uint32_t ccitt_next_transition(const uint8_t *row, uint32_t width, uint32_t start, int color) {
    uint32_t x = start;
    while (x < width) {
        if (ccitt_get_bit(row, x) != color) {
            break;
        }
        x++;
    }
    return x;
}

static int ccitt_skip_eol(ccitt_bitstream *bs) {
    int zeros = 0;
    for (;;) {
        int bit = 0;
        if (!ccitt_read_bit(bs, &bit)) {
            return 0;
        }
        if (bit == 0) {
            zeros++;
            if (zeros > 11) {
                zeros = 11;
            }
        } else {
            if (zeros >= 11) {
                return 1;
            }
            zeros = 0;
        }
    }
}

static int ccitt_decode_1d_line(ccitt_bitstream *bs, uint32_t width, uint8_t *row) {
    uint32_t x = 0;
    int color = 0;
    while (x < width) {
        uint32_t run = 0;
        if (!ccitt_decode_run(bs, color, &run)) {
            return 0;
        }
        if (x + run > width) {
            return 0;
        }
        ccitt_set_run(row, x, run, color);
        x += run;
        color = !color;
    }
    return 1;
}

enum {
    CCITT_2D_PASS = 0,
    CCITT_2D_HORIZ = 1,
    CCITT_2D_VERT = 2
};

static int ccitt_decode_2d_code(ccitt_bitstream *bs, int *type, int *val) {
    uint32_t code = 0;
    for (int len = 1; len <= 7; len++) {
        int bit = 0;
        if (!ccitt_read_bit(bs, &bit)) {
            return 0;
        }
        code = (code << 1) | (uint32_t)bit;
        if (len == 1 && code == 1) {
            *type = CCITT_2D_VERT;
            *val = 0;
            return 1;
        }
        if (len == 3 && code == 0x3) {
            *type = CCITT_2D_VERT;
            *val = 1;
            return 1;
        }
        if (len == 3 && code == 0x2) {
            *type = CCITT_2D_VERT;
            *val = -1;
            return 1;
        }
        if (len == 3 && code == 0x1) {
            *type = CCITT_2D_HORIZ;
            *val = 0;
            return 1;
        }
        if (len == 4 && code == 0x1) {
            *type = CCITT_2D_PASS;
            *val = 0;
            return 1;
        }
        if (len == 6 && code == 0x3) {
            *type = CCITT_2D_VERT;
            *val = 2;
            return 1;
        }
        if (len == 6 && code == 0x2) {
            *type = CCITT_2D_VERT;
            *val = -2;
            return 1;
        }
        if (len == 7 && code == 0x3) {
            *type = CCITT_2D_VERT;
            *val = 3;
            return 1;
        }
        if (len == 7 && code == 0x2) {
            *type = CCITT_2D_VERT;
            *val = -3;
            return 1;
        }
        if (len == 7 && code == 0x1) {
            return 0;
        }
    }
    return 0;
}

static int ccitt_decode_2d_line(ccitt_bitstream *bs, uint32_t width,
                                const uint8_t *ref, uint8_t *row) {
    uint32_t a0 = 0;
    int color = 0;
    while (a0 < width) {
        int type = 0;
        int val = 0;
        if (!ccitt_decode_2d_code(bs, &type, &val)) {
            return 0;
        }
        if (type == CCITT_2D_PASS) {
            uint32_t b1 = ccitt_next_transition(ref, width, a0, color);
            uint32_t b2 = ccitt_next_transition(ref, width, b1, !color);
            if (b2 < a0 || b2 > width) {
                return 0;
            }
            a0 = b2;
        } else if (type == CCITT_2D_HORIZ) {
            uint32_t run1 = 0;
            uint32_t run2 = 0;
            if (!ccitt_decode_run(bs, color, &run1) ||
                !ccitt_decode_run(bs, !color, &run2)) {
                return 0;
            }
            if (a0 + run1 + run2 > width) {
                return 0;
            }
            ccitt_set_run(row, a0, run1, color);
            a0 += run1;
            ccitt_set_run(row, a0, run2, !color);
            a0 += run2;
            color = color;
        } else {
            uint32_t b1 = ccitt_next_transition(ref, width, a0, color);
            int32_t a1 = (int32_t)b1 + val;
            if (a1 < 0) {
                a1 = 0;
            }
            if ((uint32_t)a1 > width) {
                return 0;
            }
            ccitt_set_run(row, a0, (uint32_t)a1 - a0, color);
            a0 = (uint32_t)a1;
            color = !color;
        }
    }
    return 1;
}

int cupidimage_tiff_ccitt_decode(const uint8_t *src, size_t src_size,
                             uint8_t *dst, size_t dst_size,
                             uint32_t width, uint32_t height,
                             uint16_t compression, uint32_t group3_options,
                             int fill_order, char *err, size_t errcap) {
    size_t row_bytes = ((size_t)width + 7u) / 8u;
    if (dst_size < row_bytes * (size_t)height) {
        cupidimage_set_err(err, errcap, "invalid CCITT buffer");
        return 0;
    }
    memset(dst, 0, row_bytes * (size_t)height);

    ccitt_bitstream bs;
    bs.data = src;
    bs.size = src_size;
    bs.pos = 0;
    bs.bit = 0;
    bs.fill_order = fill_order;

    int use_2d = 0;
    if (compression == TIFF_COMPRESSION_CCITT_G4) {
        use_2d = 1;
    } else if (compression == TIFF_COMPRESSION_CCITT_G3) {
        if (group3_options & 0x1u) {
            use_2d = 1;
        }
    }

    uint8_t *prev = (uint8_t *)malloc(row_bytes);
    if (!prev) {
        cupidimage_set_err(err, errcap, "out of memory");
        return 0;
    }
    memset(prev, 0, row_bytes);

    for (uint32_t y = 0; y < height; y++) {
        uint8_t *row = dst + (size_t)y * row_bytes;
        if (compression == TIFF_COMPRESSION_CCITT_G3 && (group3_options & 0x4u)) {
            if (!ccitt_skip_eol(&bs)) {
                free(prev);
                cupidimage_set_err(err, errcap, "invalid CCITT EOL");
                return 0;
            }
        }
        if (use_2d && y > 0) {
            if (!ccitt_decode_2d_line(&bs, width, prev, row)) {
                free(prev);
                cupidimage_set_err(err, errcap, "invalid CCITT 2D data");
                return 0;
            }
        } else {
            if (!ccitt_decode_1d_line(&bs, width, row)) {
                free(prev);
                cupidimage_set_err(err, errcap, "invalid CCITT data");
                return 0;
            }
        }
        memcpy(prev, row, row_bytes);
    }

    free(prev);
    return 1;
}
