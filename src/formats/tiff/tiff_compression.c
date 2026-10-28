#include "tiff_internal.h"

static void ccitt_set_run(uint8_t *row, uint32_t start, uint32_t run, int color);
static int ccitt_get_bit(const uint8_t *row, uint32_t x);
static uint32_t ccitt_next_transition(const uint8_t *row, uint32_t width, uint32_t start,
                                      int color);

int cupidimage_tiff_packbits_decode(const uint8_t *src, size_t src_size, uint8_t *dst,
                                    size_t dst_size, char *err, size_t errcap) {
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

int cupidimage_tiff_lzw_decode(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size,
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

#include "compression/ccitt_tables.h"

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

static int ccitt_decode_single(ccitt_bitstream *bs, const ccitt_code *term, size_t term_len,
                               const ccitt_code *makeup, size_t makeup_len, uint32_t *run,
                               int *is_makeup) {
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

static uint32_t ccitt_next_transition(const uint8_t *row, uint32_t width, uint32_t start,
                                      int color) {
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

enum { CCITT_2D_PASS = 0, CCITT_2D_HORIZ = 1, CCITT_2D_VERT = 2 };

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

static int ccitt_decode_2d_line(ccitt_bitstream *bs, uint32_t width, const uint8_t *ref,
                                uint8_t *row) {
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
            if (!ccitt_decode_run(bs, color, &run1) || !ccitt_decode_run(bs, !color, &run2)) {
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

int cupidimage_tiff_ccitt_decode(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size,
                                 uint32_t width, uint32_t height, uint16_t compression,
                                 uint32_t group3_options, int fill_order, char *err,
                                 size_t errcap) {
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
