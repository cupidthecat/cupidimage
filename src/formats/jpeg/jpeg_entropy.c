#include "jpeg_internal.h"
#include <limits.h>
#include <string.h>

const uint8_t cupidimage_jpeg_zigzag[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

static const uint8_t std_dc_luminance_bits[16] = {0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01,
                                                  0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static const uint8_t std_dc_luminance_val[12] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                                 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B};

static const uint8_t std_ac_luminance_bits[16] = {0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03,
                                                  0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D};

static const uint8_t std_ac_luminance_val[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61,
    0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52,
    0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25,
    0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64,
    0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
    0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
    0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3,
    0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8,
    0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA};

static const uint8_t std_dc_chrominance_bits[16] = {0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
                                                    0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};

static const uint8_t std_dc_chrominance_val[12] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
                                                   0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B};

static const uint8_t std_ac_chrominance_bits[16] = {0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04,
                                                    0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77};

static const uint8_t std_ac_chrominance_val[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61,
    0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33,
    0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18,
    0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63,
    0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4,
    0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA,
    0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7,
    0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA};

int cupidimage_jpeg_huff_build(jpeg_huff *h, const uint8_t *bits, const uint8_t *vals, int num) {
    if (num < 1 || num > 256)
        return 0;
    int slots = 1;
    int total = 0;
    for (int i = 0; i < 16; i++) {
        slots = slots * 2 - bits[i];
        total += bits[i];
        /* JPEG reserves the all-ones code for entropy padding. */
        if (slots <= 0)
            return 0;
    }
    if (total != num)
        return 0;
    memcpy(h->bits, bits, 16);
    memcpy(h->huffval, vals, (size_t)num);
    h->num = num;

    int code = 0;
    int k = 0;
    for (int i = 1; i <= 16; i++) {
        int count = bits[i - 1];
        if (count == 0) {
            h->mincode[i] = -1;
            h->maxcode[i] = -1;
        } else {
            h->mincode[i] = code;
            h->valptr[i] = k;
            code += count;
            h->maxcode[i] = code - 1;
            k += count;
        }
        code <<= 1;
    }
    h->maxcode[17] = 0xFFFF;
    h->valid = 1;
    return 1;
}

void cupidimage_jpeg_default_huffman(jpeg_huff dc_tables[4], jpeg_huff ac_tables[4]) {
    if (!dc_tables[0].valid) {
        cupidimage_jpeg_huff_build(
            &dc_tables[0], std_dc_luminance_bits, std_dc_luminance_val,
            (int)(sizeof(std_dc_luminance_val) / sizeof(std_dc_luminance_val[0])));
    }
    if (!ac_tables[0].valid) {
        cupidimage_jpeg_huff_build(
            &ac_tables[0], std_ac_luminance_bits, std_ac_luminance_val,
            (int)(sizeof(std_ac_luminance_val) / sizeof(std_ac_luminance_val[0])));
    }
    if (!dc_tables[1].valid) {
        cupidimage_jpeg_huff_build(
            &dc_tables[1], std_dc_chrominance_bits, std_dc_chrominance_val,
            (int)(sizeof(std_dc_chrominance_val) / sizeof(std_dc_chrominance_val[0])));
    }
    if (!ac_tables[1].valid) {
        cupidimage_jpeg_huff_build(
            &ac_tables[1], std_ac_chrominance_bits, std_ac_chrominance_val,
            (int)(sizeof(std_ac_chrominance_val) / sizeof(std_ac_chrominance_val[0])));
    }
}

static int bs_read_byte_raw(jpeg_bitstream *bs) {
    if (bs->pos >= bs->size) {
        return -1;
    }
    return bs->data[bs->pos++];
}

static int bs_read_byte_stuffed(jpeg_bitstream *bs) {
    if (bs->pos >= bs->size) {
        return -1;
    }
    int b = bs->data[bs->pos++];
    if (b != 0xFF) {
        return b;
    }
    if (bs->pos >= bs->size) {
        return -1;
    }
    int b2 = bs->data[bs->pos++];
    while (b2 == 0xFF) {
        if (bs->pos >= bs->size) {
            return -1;
        }
        b2 = bs->data[bs->pos++];
    }
    if (b2 == 0x00) {
        return 0xFF;
    }
    bs->marker = b2;
    return -1;
}

static int bs_read_bit(jpeg_bitstream *bs) {
    if (bs->bitcount == 0) {
        int byte = bs_read_byte_stuffed(bs);
        if (byte < 0) {
            return -1;
        }
        bs->bitbuf = (uint32_t)byte;
        bs->bitcount = 8;
    }
    int bit = (bs->bitbuf >> 7) & 1u;
    bs->bitbuf <<= 1;
    bs->bitcount--;
    return bit;
}

static int bs_read_bits(jpeg_bitstream *bs, int n) {
    int val = 0;
    for (int i = 0; i < n; i++) {
        int bit = bs_read_bit(bs);
        if (bit < 0) {
            return -1;
        }
        val = (val << 1) | bit;
    }
    return val;
}

static int huff_decode(jpeg_bitstream *bs, const jpeg_huff *h, int *sym) {
    int code = 0;
    for (int i = 1; i <= 16; i++) {
        int bit = bs_read_bit(bs);
        if (bit < 0) {
            return 0;
        }
        code = (code << 1) | bit;
        if (h->maxcode[i] >= 0 && code <= h->maxcode[i]) {
            int idx = h->valptr[i] + (code - h->mincode[i]);
            if (idx < 0 || idx >= h->num) {
                return 0;
            }
            *sym = h->huffval[idx];
            return 1;
        }
    }
    return 0;
}

static int receive_extend(jpeg_bitstream *bs, int s, int *ok) {
    if (s < 0 || s > 11) {
        *ok = 0;
        return 0;
    }
    if (s == 0) {
        *ok = 1;
        return 0;
    }
    int v = bs_read_bits(bs, s);
    if (v < 0) {
        *ok = 0;
        return 0;
    }
    int vt = 1 << (s - 1);
    if (v < vt) {
        v -= (1 << s) - 1;
    }
    *ok = 1;
    return v;
}

int cupidimage_jpeg_decode_block(jpeg_bitstream *bs, const jpeg_huff *dc, const jpeg_huff *ac,
                                 const int *qt, int *dc_pred, int32_t *block) {
    for (int i = 0; i < 64; i++) {
        block[i] = 0;
    }

    int sym = 0;
    if (!huff_decode(bs, dc, &sym) || sym > 11) {
        return 0;
    }
    int ok = 0;
    int diff = receive_extend(bs, sym, &ok);
    if (!ok) {
        return 0;
    }
    int64_t predicted = (int64_t)*dc_pred + diff;
    if (predicted < INT_MIN || predicted > INT_MAX ||
        !cupidimage_jpeg_dequantize((int32_t)predicted, qt ? qt[0] : 1, &block[0]))
        return 0;
    *dc_pred = (int)predicted;

    int k = 1;
    while (k < 64) {
        if (!huff_decode(bs, ac, &sym)) {
            return 0;
        }
        if (sym == 0) {
            break;
        }
        if (sym == 0xF0) {
            if (64 - k < 16)
                return 0;
            k += 16;
            continue;
        }
        int run = sym >> 4;
        int size = sym & 0x0F;
        if (size == 0 || size > 10)
            return 0;
        k += run;
        if (k >= 64) {
            return 0;
        }
        int coeff = receive_extend(bs, size, &ok);
        if (!ok) {
            return 0;
        }
        if (!cupidimage_jpeg_dequantize(coeff, qt ? qt[cupidimage_jpeg_zigzag[k]] : 1,
                                        &block[cupidimage_jpeg_zigzag[k]]))
            return 0;
        k++;
    }

    return 1;
}

int cupidimage_jpeg_consume_restart(jpeg_bitstream *bs, int expected) {
    bs->bitcount = 0;
    bs->bitbuf = 0;

    int b = 0;
    do {
        b = bs_read_byte_raw(bs);
        if (b < 0) {
            return 0;
        }
    } while (b != 0xFF);

    do {
        b = bs_read_byte_raw(bs);
        if (b < 0) {
            return 0;
        }
    } while (b == 0xFF);

    if (b < 0xD0 || b > 0xD7) {
        return 0;
    }
    if (expected >= 0 && b != expected) {
        return 0;
    }
    return 1;
}

static int decode_dc_first(jpeg_bitstream *bs, jpeg_comp *c, const jpeg_huff *dc, int al,
                           int32_t *block) {
    int sym = 0;
    if (!huff_decode(bs, dc, &sym) || sym > 11) {
        return 0;
    }
    int ok = 0;
    int diff = receive_extend(bs, sym, &ok);
    if (!ok) {
        return 0;
    }
    int64_t predicted = (int64_t)c->dc_pred + diff;
    int64_t scaled = predicted * (INT64_C(1) << al);
    if (predicted < INT_MIN || predicted > INT_MAX || scaled < INT32_MIN || scaled > INT32_MAX)
        return 0;
    c->dc_pred = (int)predicted;
    block[0] = (int32_t)scaled;
    return 1;
}

static int decode_dc_refine(jpeg_bitstream *bs, int al, int32_t *block) {
    int bit = bs_read_bit(bs);
    if (bit < 0) {
        return 0;
    }
    if (bit) {
        int32_t add = (int32_t)1 << al;
        /* DC refinement appends a bit to the signed point transform. */
        block[0] |= add;
    }
    return 1;
}

static int decode_ac_first(jpeg_bitstream *bs, const jpeg_huff *ac, int ss, int se, int al,
                           int32_t *block, int *eobrun) {
    int k = ss;
    if (*eobrun > 0) {
        (*eobrun)--;
        return 1;
    }
    while (k <= se) {
        int sym = 0;
        if (!huff_decode(bs, ac, &sym)) {
            return 0;
        }
        int run = sym >> 4;
        int size = sym & 0x0F;
        if (size > 10)
            return 0;
        if (size == 0) {
            if (run == 15) {
                if (se - k + 1 < 16)
                    return 0;
                k += 16;
                continue;
            }
            int extra = 0;
            if (run > 0) {
                extra = bs_read_bits(bs, run);
                if (extra < 0) {
                    return 0;
                }
            }
            *eobrun = (1 << run) + extra - 1;
            break;
        }
        k += run;
        if (k > se) {
            return 0;
        }
        int ok = 0;
        int coeff = receive_extend(bs, size, &ok);
        if (!ok) {
            return 0;
        }
        block[cupidimage_jpeg_zigzag[k]] = (int32_t)coeff * (1 << al);
        k++;
    }
    return 1;
}

static int refine_coeff(jpeg_bitstream *bs, int32_t *coef, int p1) {
    int bit = bs_read_bit(bs);
    if (bit < 0) {
        return 0;
    }
    if (bit) {
        int64_t refined = (int64_t)*coef + ((*coef >= 0) ? p1 : -p1);
        if (refined < INT32_MIN || refined > INT32_MAX)
            return 0;
        *coef = (int32_t)refined;
    }
    return 1;
}

static int decode_ac_refine(jpeg_bitstream *bs, const jpeg_huff *ac, int ss, int se, int al,
                            int32_t *block, int *eobrun) {
    int p1 = 1 << al;
    int p2 = -p1;

    int k = ss;
    if (*eobrun > 0) {
        for (; k <= se; k++) {
            int idx = cupidimage_jpeg_zigzag[k];
            if (block[idx] != 0) {
                if (!refine_coeff(bs, &block[idx], p1)) {
                    return 0;
                }
            }
        }
        (*eobrun)--;
        return 1;
    }

    while (k <= se) {
        int sym = 0;
        if (!huff_decode(bs, ac, &sym)) {
            return 0;
        }
        int run = sym >> 4;
        int size = sym & 0x0F;
        if (size == 0) {
            if (run == 15) {
                /* ZRL counts sixteen zero coefficients, not sixteen spectral
                 * positions. Existing nonzero coefficients consume refinement
                 * bits but do not shorten the zero run (T.81, G.1.2.3). */
                int zeros = 16;
                while (zeros && k <= se) {
                    int idx = cupidimage_jpeg_zigzag[k++];
                    if (block[idx] != 0) {
                        if (!refine_coeff(bs, &block[idx], p1))
                            return 0;
                    } else {
                        zeros--;
                    }
                }
                if (zeros)
                    return 0;
                continue;
            }
            int extra = 0;
            if (run > 0) {
                extra = bs_read_bits(bs, run);
                if (extra < 0) {
                    return 0;
                }
            }
            *eobrun = (1 << run) + extra - 1;
            for (; k <= se; k++) {
                int idx = cupidimage_jpeg_zigzag[k];
                if (block[idx] != 0) {
                    if (!refine_coeff(bs, &block[idx], p1)) {
                        return 0;
                    }
                }
            }
            break;
        }
        if (size != 1) {
            return 0;
        }
        int bit = bs_read_bit(bs);
        if (bit < 0) {
            return 0;
        }
        int32_t newcoef = bit ? p1 : p2;
        int inserted = 0;
        while (k <= se) {
            int idx = cupidimage_jpeg_zigzag[k];
            if (block[idx] != 0) {
                if (!refine_coeff(bs, &block[idx], p1)) {
                    return 0;
                }
            } else if (run == 0) {
                block[idx] = newcoef;
                inserted = 1;
                k++;
                break;
            } else {
                run--;
            }
            k++;
        }
        if (!inserted)
            return 0;
    }
    return 1;
}

static int jpeg_decode_coefficients(jpeg_bitstream *stream, jpeg_comp *component,
                                    const jpeg_huff *dc, const jpeg_huff *ac, int ss, int se,
                                    int ah, int al, int *eobrun, int32_t *block, int sequential) {
    if (sequential)
        return cupidimage_jpeg_decode_block(stream, dc, ac, NULL, &component->dc_pred, block);
    if (!ss && !se)
        return ah ? decode_dc_refine(stream, al, block)
                  : decode_dc_first(stream, component, dc, al, block);
    return ah ? decode_ac_refine(stream, ac, ss, se, al, block, eobrun)
              : decode_ac_first(stream, ac, ss, se, al, block, eobrun);
}

int cupidimage_jpeg_decode_coefficient_scan(
    const uint8_t *data, size_t size, jpeg_comp *comps, int num_comp, const int *scan_comp,
    int scan_count, int ss, int se, int ah, int al, int width, int height, int mcu_cols,
    int mcu_rows, int hmax, int vmax, const jpeg_huff dc_tables[4], const jpeg_huff ac_tables[4],
    int restart_interval, size_t *consumed, char *err, size_t errcap, int sequential) {
    jpeg_bitstream bs;
    bs.data = data;
    bs.size = size;
    bs.pos = 0;
    bs.bitbuf = 0;
    bs.bitcount = 0;
    bs.marker = 0;

    for (int i = 0; i < scan_count; i++) {
        int ci = scan_comp[i];
        if (ci < 0 || ci >= num_comp) {
            cupidimage_jpeg_set_error(err, errcap, "invalid scan component");
            return 0;
        }
        if (comps[ci].dc_table < 0 || comps[ci].dc_table > 3 || comps[ci].ac_table < 0 ||
            comps[ci].ac_table > 3) {
            cupidimage_jpeg_set_error(err, errcap, "invalid huffman table");
            return 0;
        }
        if (sequential &&
            (!dc_tables[comps[ci].dc_table].valid || !ac_tables[comps[ci].ac_table].valid)) {
            cupidimage_jpeg_set_error(err, errcap, "missing sequential Huffman table");
            return 0;
        }
        if (ss == 0 && se == 0) {
            if (!dc_tables[comps[ci].dc_table].valid) {
                cupidimage_jpeg_set_error(err, errcap, "missing huffman table");
                return 0;
            }
        } else {
            if (!ac_tables[comps[ci].ac_table].valid) {
                cupidimage_jpeg_set_error(err, errcap, "missing huffman table");
                return 0;
            }
        }
    }
    if (sequential || (ss == 0 && ah == 0)) {
        for (int i = 0; i < scan_count; i++) {
            comps[scan_comp[i]].dc_pred = 0;
        }
    }

    int eobrun = 0;
    int mcu_count = 0;
    int rst_index = 0;

    if (scan_count == 1) {
        jpeg_comp *c = &comps[scan_comp[0]];
        int blocks_w = (width * c->h + hmax * 8 - 1) / (hmax * 8);
        int blocks_h = (height * c->v + vmax * 8 - 1) / (vmax * 8);
        for (int by = 0; by < blocks_h; by++) {
            for (int bx = 0; bx < blocks_w; bx++) {
                int32_t *block = c->coeffs + ((by * c->blocks_w + bx) * 64);
                if (!jpeg_decode_coefficients(&bs, c, &dc_tables[c->dc_table],
                                              &ac_tables[c->ac_table], ss, se, ah, al, &eobrun,
                                              block, sequential)) {
                    cupidimage_jpeg_set_error(err, errcap, "jpeg decode failed");
                    return 0;
                }

                mcu_count++;
                if (restart_interval > 0 && (mcu_count % restart_interval) == 0 &&
                    (by + 1 < blocks_h || bx + 1 < blocks_w)) {
                    int expected = 0xD0 + (rst_index & 7);
                    if (!cupidimage_jpeg_consume_restart(&bs, expected)) {
                        cupidimage_jpeg_set_error(err, errcap, "jpeg restart marker error");
                        return 0;
                    }
                    rst_index++;
                    eobrun = 0;
                    for (int i = 0; i < num_comp; i++) {
                        comps[i].dc_pred = 0;
                    }
                }
            }
        }
    } else {
        for (int my = 0; my < mcu_rows; my++) {
            for (int mx = 0; mx < mcu_cols; mx++) {
                for (int si = 0; si < scan_count; si++) {
                    jpeg_comp *c = &comps[scan_comp[si]];
                    for (int by = 0; by < c->v; by++) {
                        for (int bx = 0; bx < c->h; bx++) {
                            int block_x = mx * c->h + bx;
                            int block_y = my * c->v + by;
                            int32_t *block = c->coeffs + ((block_y * c->blocks_w + block_x) * 64);
                            if (!jpeg_decode_coefficients(&bs, c, &dc_tables[c->dc_table],
                                                          &ac_tables[c->ac_table], ss, se, ah, al,
                                                          &eobrun, block, sequential)) {
                                cupidimage_jpeg_set_error(err, errcap, "jpeg decode failed");
                                return 0;
                            }
                        }
                    }
                }

                mcu_count++;
                if (restart_interval > 0 && (mcu_count % restart_interval) == 0 &&
                    (my + 1 < mcu_rows || mx + 1 < mcu_cols)) {
                    int expected = 0xD0 + (rst_index & 7);
                    if (!cupidimage_jpeg_consume_restart(&bs, expected)) {
                        cupidimage_jpeg_set_error(err, errcap, "jpeg restart marker error");
                        return 0;
                    }
                    rst_index++;
                    eobrun = 0;
                    for (int i = 0; i < num_comp; i++) {
                        comps[i].dc_pred = 0;
                    }
                }
            }
        }
    }

    if (bs.marker != 0 && bs.pos >= 2) {
        *consumed = bs.pos - 2;
    } else {
        *consumed = bs.pos;
    }
    return 1;
}
