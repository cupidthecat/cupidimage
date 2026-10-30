#include "core/internal.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *out;
    size_t outcap;
    size_t pos;
    uint32_t bitbuf;
    int bitcount;
    int overflow;
} deflate_writer;

static void deflate_put_bits(deflate_writer *w, uint32_t bits, int nbits) {
    w->bitbuf |= bits << w->bitcount;
    w->bitcount += nbits;

    while (w->bitcount >= 8) {
        if (w->pos >= w->outcap) {
            w->overflow = 1;
            return;
        }
        w->out[w->pos++] = (uint8_t)(w->bitbuf & 0xFF);
        w->bitbuf >>= 8;
        w->bitcount -= 8;
    }
}

static void deflate_flush_bits(deflate_writer *w) {
    if (w->bitcount > 0) {
        if (w->pos >= w->outcap) {
            w->overflow = 1;
            return;
        }
        w->out[w->pos++] = (uint8_t)(w->bitbuf & 0xFF);
    }
    w->bitbuf = 0;
    w->bitcount = 0;
}

/* Static Huffman codes for literals (RFC 1951) */
static const uint16_t static_lit_codes[288] = {
    /* 0-143: 8 bits, 00110000-10111111 */
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0038, 0x0039, 0x003A, 0x003B,
    0x003C, 0x003D, 0x003E, 0x003F, 0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F, 0x0050, 0x0051, 0x0052, 0x0053,
    0x0054, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067, 0x0068, 0x0069, 0x006A, 0x006B,
    0x006C, 0x006D, 0x006E, 0x006F, 0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x007F, 0x0080, 0x0081, 0x0082, 0x0083,
    0x0084, 0x0085, 0x0086, 0x0087, 0x0088, 0x0089, 0x008A, 0x008B, 0x008C, 0x008D, 0x008E, 0x008F,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097, 0x0098, 0x0099, 0x009A, 0x009B,
    0x009C, 0x009D, 0x009E, 0x009F, 0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
    0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF, 0x00B0, 0x00B1, 0x00B2, 0x00B3,
    0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
    /* 144-255: 9 bits, 110010000-111111111 */
    0x0190, 0x0191, 0x0192, 0x0193, 0x0194, 0x0195, 0x0196, 0x0197, 0x0198, 0x0199, 0x019A, 0x019B,
    0x019C, 0x019D, 0x019E, 0x019F, 0x01A0, 0x01A1, 0x01A2, 0x01A3, 0x01A4, 0x01A5, 0x01A6, 0x01A7,
    0x01A8, 0x01A9, 0x01AA, 0x01AB, 0x01AC, 0x01AD, 0x01AE, 0x01AF, 0x01B0, 0x01B1, 0x01B2, 0x01B3,
    0x01B4, 0x01B5, 0x01B6, 0x01B7, 0x01B8, 0x01B9, 0x01BA, 0x01BB, 0x01BC, 0x01BD, 0x01BE, 0x01BF,
    0x01C0, 0x01C1, 0x01C2, 0x01C3, 0x01C4, 0x01C5, 0x01C6, 0x01C7, 0x01C8, 0x01C9, 0x01CA, 0x01CB,
    0x01CC, 0x01CD, 0x01CE, 0x01CF, 0x01D0, 0x01D1, 0x01D2, 0x01D3, 0x01D4, 0x01D5, 0x01D6, 0x01D7,
    0x01D8, 0x01D9, 0x01DA, 0x01DB, 0x01DC, 0x01DD, 0x01DE, 0x01DF, 0x01E0, 0x01E1, 0x01E2, 0x01E3,
    0x01E4, 0x01E5, 0x01E6, 0x01E7, 0x01E8, 0x01E9, 0x01EA, 0x01EB, 0x01EC, 0x01ED, 0x01EE, 0x01EF,
    0x01F0, 0x01F1, 0x01F2, 0x01F3, 0x01F4, 0x01F5, 0x01F6, 0x01F7, 0x01F8, 0x01F9, 0x01FA, 0x01FB,
    0x01FC, 0x01FD, 0x01FE, 0x01FF,
    /* 256-279: 7 bits, 0000000-0010111 */
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x000A, 0x000B,
    0x000C, 0x000D, 0x000E, 0x000F, 0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
    /* 280-287: 8 bits, 11000000-11000111 */
    0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7};

static const uint8_t static_lit_lens[288] = {
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8};

/* Reverse bits for Huffman coding */
static uint32_t reverse_bits(uint32_t val, int nbits) {
    uint32_t result = 0;
    for (int i = 0; i < nbits; i++) {
        result = (result << 1) | (val & 1);
        val >>= 1;
    }
    return result;
}

/* Calculate Adler-32 checksum */
static uint32_t adler32(const uint8_t *data, size_t len) {
    uint32_t a = 1, b = 0;
    const uint32_t MOD_ADLER = 65521;

    for (size_t i = 0; i < len; i++) {
        a = (a + data[i]) % MOD_ADLER;
        b = (b + a) % MOD_ADLER;
    }

    return (b << 16) | a;
}

int cupidimage_deflate_compress(const uint8_t *data, size_t size, uint8_t *out, size_t outcap,
                                size_t *outlen) {
    if (outcap < 6) {
        return 0;
    }

    /* Zlib header */
    out[0] = 0x78; /* CMF: deflate, 32k window */
    out[1] = 0x9C; /* FLG: default compression, checksum */

    deflate_writer w = {0};
    w.out = out;
    w.outcap = outcap - 4; /* reserve 4 bytes for Adler32 */
    w.pos = 2;

    /* DEFLATE block header: BFINAL=1, BTYPE=01 (static Huffman) */
    deflate_put_bits(&w, 0x01, 1); /* final block */
    deflate_put_bits(&w, 0x01, 2); /* static Huffman */

    /* Encode literals */
    for (size_t i = 0; i < size; i++) {
        uint8_t sym = data[i];
        uint32_t code = reverse_bits(static_lit_codes[sym], static_lit_lens[sym]);
        deflate_put_bits(&w, code, static_lit_lens[sym]);
        if (w.overflow) {
            return 0;
        }
    }

    /* End of block (code 256) */
    uint32_t eob_code = reverse_bits(static_lit_codes[256], static_lit_lens[256]);
    deflate_put_bits(&w, eob_code, static_lit_lens[256]);

    deflate_flush_bits(&w);

    if (w.overflow) {
        return 0;
    }

    /* Adler32 checksum (big-endian) */
    if (w.pos + 4 > outcap) {
        return 0;
    }
    uint32_t checksum = adler32(data, size);
    out[w.pos++] = (uint8_t)((checksum >> 24) & 0xFF);
    out[w.pos++] = (uint8_t)((checksum >> 16) & 0xFF);
    out[w.pos++] = (uint8_t)((checksum >> 8) & 0xFF);
    out[w.pos++] = (uint8_t)(checksum & 0xFF);

    *outlen = w.pos;
    return 1;
}

/* RFC 1950/1951 decoder shared by PNG, TIFF and PDF. */
struct bitstream {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t bitbuf;
    int bitcount;
    int failed;
    size_t window;
};

static int bs_fill(struct bitstream *bs, int need) {
    while (bs->bitcount < need && bs->pos < bs->size) {
        bs->bitbuf |= (uint32_t)bs->data[bs->pos++] << bs->bitcount;
        bs->bitcount += 8;
    }
    return bs->bitcount >= need;
}

static uint32_t bs_read(struct bitstream *bs, int bits) {
    if (!bs_fill(bs, bits)) {
        bs->failed = 1;
        return 0;
    }
    uint32_t val = bs->bitbuf & ((1u << bits) - 1u);
    bs->bitbuf >>= bits;
    bs->bitcount -= bits;
    return val;
}

static void bs_align(struct bitstream *bs) {
    /* Return complete lookahead bytes to the byte reader; discard only padding. */
    bs->pos -= (size_t)(bs->bitcount / 8);
    bs->bitbuf = 0;
    bs->bitcount = 0;
}

typedef struct huff_table {
    int maxbits;
    uint16_t *table;
} huff_table;

static void huff_free(huff_table *ht) {
    free(ht->table);
    ht->table = NULL;
    ht->maxbits = 0;
}

static int huff_build(huff_table *ht, const uint8_t *lengths, int num, int maxbits, int kind) {
    int count[16] = {0};
    int maxlen = 0;
    for (int i = 0; i < num; i++) {
        if (lengths[i] > maxbits) {
            return 0;
        }
        if (lengths[i]) {
            count[lengths[i]]++;
            if (lengths[i] > maxlen) {
                maxlen = lengths[i];
            }
        }
    }
    if (maxlen == 0)
        return kind == 2; /* A literal-only block has no distances. */
    int left = 1;
    for (int bits = 1; bits <= maxbits; bits++) {
        left = (left << 1) - count[bits];
        if (left < 0)
            return 0;
    }
    /* Code-length alphabets must be complete. The data alphabets may use
       exactly one one-bit code, with the other bit pattern unused. */
    if (left && (kind == 0 || maxlen != 1))
        return 0;

    int next_code[16];
    int code = 0;
    count[0] = 0;
    for (int bits = 1; bits <= maxbits; bits++) {
        code = (code + count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    ht->maxbits = maxlen;
    ht->table = (uint16_t *)malloc(((size_t)1u << maxlen) * sizeof(uint16_t));
    if (!ht->table) {
        return 0;
    }
    size_t table_size = (size_t)1u << maxlen;
    for (size_t i = 0; i < table_size; i++) {
        ht->table[i] = 0xFFFFu;
    }

    for (int sym = 0; sym < num; sym++) {
        int len = lengths[sym];
        if (!len) {
            continue;
        }
        int sym_code = next_code[len]++;
        unsigned rev = reverse_bits((unsigned)sym_code, len);
        unsigned fill = 1u << (maxlen - len);
        for (unsigned i = 0; i < fill; i++) {
            unsigned idx = (i << len) | rev;
            ht->table[idx] = (uint16_t)((len << 9) | sym);
        }
    }

    return 1;
}

static int huff_decode(struct bitstream *bs, const huff_table *ht, int *sym) {
    if (!ht->table || bs->failed)
        return 0;
    /* A final short code need not have maxbits of lookahead remaining. */
    (void)bs_fill(bs, ht->maxbits);
    uint32_t idx = bs->bitbuf & ((1u << ht->maxbits) - 1u);
    uint16_t val = ht->table[idx];
    if (val == 0xFFFFu) {
        return 0;
    }
    int len = val >> 9;
    if (len > bs->bitcount)
        return 0;
    *sym = val & 0x1FF;
    bs->bitbuf >>= len;
    bs->bitcount -= len;
    return 1;
}

static int build_fixed_tables(huff_table *litlen, huff_table *dist) {
    uint8_t litlen_lengths[288];
    uint8_t dist_lengths[32];

    for (int i = 0; i <= 143; i++) {
        litlen_lengths[i] = 8;
    }
    for (int i = 144; i <= 255; i++) {
        litlen_lengths[i] = 9;
    }
    for (int i = 256; i <= 279; i++) {
        litlen_lengths[i] = 7;
    }
    for (int i = 280; i <= 287; i++) {
        litlen_lengths[i] = 8;
    }
    for (int i = 0; i < 32; i++) {
        dist_lengths[i] = 5;
    }

    if (!huff_build(litlen, litlen_lengths, 288, 15, 1)) {
        return 0;
    }
    if (!huff_build(dist, dist_lengths, 32, 15, 2)) {
        huff_free(litlen);
        return 0;
    }
    return 1;
}

static int build_dynamic_tables(struct bitstream *bs, huff_table *litlen, huff_table *dist) {
    int hlit = (int)bs_read(bs, 5) + 257;
    int hdist = (int)bs_read(bs, 5) + 1;
    int hclen = (int)bs_read(bs, 4) + 4;
    if (bs->failed || hlit > 286 || hdist > 32) {
        return 0;
    }

    static const int order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    uint8_t clen_lengths[19];
    for (int i = 0; i < 19; i++) {
        clen_lengths[i] = 0;
    }
    for (int i = 0; i < hclen; i++) {
        clen_lengths[order[i]] = (uint8_t)bs_read(bs, 3);
    }

    if (bs->failed)
        return 0;
    huff_table clen_table = {0, NULL};
    if (!huff_build(&clen_table, clen_lengths, 19, 7, 0)) {
        return 0;
    }

    int total = hlit + hdist;
    uint8_t lengths[318];
    int idx = 0;
    while (idx < total) {
        int sym = 0;
        if (!huff_decode(bs, &clen_table, &sym)) {
            huff_free(&clen_table);
            return 0;
        }
        if (sym <= 15) {
            lengths[idx++] = (uint8_t)sym;
        } else if (sym == 16) {
            if (idx == 0) {
                huff_free(&clen_table);
                return 0;
            }
            int repeat = (int)bs_read(bs, 2) + 3;
            uint8_t prev = lengths[idx - 1];
            if (bs->failed || repeat > total - idx) {
                huff_free(&clen_table);
                return 0;
            }
            for (int i = 0; i < repeat; i++) {
                lengths[idx++] = prev;
            }
        } else if (sym == 17) {
            int repeat = (int)bs_read(bs, 3) + 3;
            if (bs->failed || repeat > total - idx) {
                huff_free(&clen_table);
                return 0;
            }
            for (int i = 0; i < repeat; i++) {
                lengths[idx++] = 0;
            }
        } else if (sym == 18) {
            int repeat = (int)bs_read(bs, 7) + 11;
            if (bs->failed || repeat > total - idx) {
                huff_free(&clen_table);
                return 0;
            }
            for (int i = 0; i < repeat; i++) {
                lengths[idx++] = 0;
            }
        } else {
            huff_free(&clen_table);
            return 0;
        }
    }

    huff_free(&clen_table);

    if (!lengths[256] || bs->failed)
        return 0;
    if (!huff_build(litlen, lengths, hlit, 15, 1)) {
        return 0;
    }
    if (!huff_build(dist, lengths + hlit, hdist, 15, 2)) {
        huff_free(litlen);
        return 0;
    }
    return 1;
}

static int deflate_decode(struct bitstream *bs, uint8_t *out, size_t outcap, size_t *outlen) {
    static const int length_base[29] = {3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                        15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                        67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const int length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                         2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const int dist_base[30] = {
        1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const int dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                       6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

    size_t outpos = 0;
    int final_block = 0;

    while (!final_block) {
        if (!bs_fill(bs, 3)) {
            return 0;
        }
        final_block = (int)bs_read(bs, 1);
        int btype = (int)bs_read(bs, 2);

        if (btype == 0) {
            bs_align(bs);
            if (bs->size - bs->pos < 4) {
                return 0;
            }
            uint16_t len = (uint16_t)(bs->data[bs->pos] | (bs->data[bs->pos + 1] << 8));
            uint16_t nlen = (uint16_t)(bs->data[bs->pos + 2] | (bs->data[bs->pos + 3] << 8));
            bs->pos += 4;
            if ((uint16_t)~len != nlen) {
                return 0;
            }
            if ((size_t)len > bs->size - bs->pos) {
                return 0;
            }
            if ((size_t)len > outcap - outpos) {
                return 0;
            }
            memcpy(out + outpos, bs->data + bs->pos, len);
            bs->pos += len;
            outpos += len;
        } else if (btype == 1 || btype == 2) {
            huff_table litlen = {0, NULL};
            huff_table dist = {0, NULL};
            int ok = 0;
            if (btype == 1) {
                ok = build_fixed_tables(&litlen, &dist);
            } else {
                ok = build_dynamic_tables(bs, &litlen, &dist);
            }
            if (!ok) {
                huff_free(&litlen);
                huff_free(&dist);
                return 0;
            }

            while (1) {
                int sym = 0;
                if (!huff_decode(bs, &litlen, &sym)) {
                    huff_free(&litlen);
                    huff_free(&dist);
                    return 0;
                }
                if (sym < 256) {
                    if (outpos >= outcap) {
                        huff_free(&litlen);
                        huff_free(&dist);
                        return 0;
                    }
                    out[outpos++] = (uint8_t)sym;
                } else if (sym == 256) {
                    break;
                } else if (sym <= 285) {
                    int len_idx = sym - 257;
                    int length = length_base[len_idx];
                    int extra = length_extra[len_idx];
                    if (extra) {
                        length += (int)bs_read(bs, extra);
                    }

                    int dist_sym = 0;
                    if (!huff_decode(bs, &dist, &dist_sym)) {
                        huff_free(&litlen);
                        huff_free(&dist);
                        return 0;
                    }
                    if (dist_sym > 29) {
                        huff_free(&litlen);
                        huff_free(&dist);
                        return 0;
                    }
                    int distance = dist_base[dist_sym];
                    int dist_ext = dist_extra[dist_sym];
                    if (dist_ext) {
                        distance += (int)bs_read(bs, dist_ext);
                    }
                    if (bs->failed || distance <= 0 || (size_t)distance > outpos ||
                        (size_t)distance > bs->window) {
                        huff_free(&litlen);
                        huff_free(&dist);
                        return 0;
                    }
                    if ((size_t)length > outcap - outpos) {
                        huff_free(&litlen);
                        huff_free(&dist);
                        return 0;
                    }
                    for (int i = 0; i < length; i++) {
                        out[outpos] = out[outpos - (size_t)distance];
                        outpos++;
                    }
                } else {
                    huff_free(&litlen);
                    huff_free(&dist);
                    return 0;
                }
            }

            huff_free(&litlen);
            huff_free(&dist);
        } else {
            return 0;
        }
    }

    *outlen = outpos;
    return 1;
}

int cupidimage_inflate_zlib_prefix(const uint8_t *data, size_t size, uint8_t *out, size_t outcap,
                                   size_t *outlen, size_t *consumed) {
    if (!outlen || !consumed)
        return 0;
    *outlen = 0;
    *consumed = 0;
    if (!data || (!out && outcap) || size < 6)
        return 0;
    uint8_t cmf = data[0], flg = data[1];
    if ((cmf & 15) != 8 || (cmf >> 4) > 7 || (((unsigned)cmf << 8) | flg) % 31 || (flg & 32))
        return 0;
    struct bitstream bs = {data + 2, size - 2, 0, 0, 0, 0, (size_t)1 << ((cmf >> 4) + 8)};
    size_t produced = 0;
    if (!deflate_decode(&bs, out, outcap, &produced) || bs.failed)
        return 0;
    bs_align(&bs);
    if (bs.pos > size - 6)
        return 0;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < produced; i++) {
        a = (a + out[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    const uint8_t *sum = data + 2 + bs.pos;
    uint32_t expected =
        ((uint32_t)sum[0] << 24) | ((uint32_t)sum[1] << 16) | ((uint32_t)sum[2] << 8) | sum[3];
    if (((b << 16) | a) != expected)
        return 0;
    *outlen = produced;
    *consumed = bs.pos + 6;
    return 1;
}

/* Container decoders retain strict whole-stream validation. Inline PDF images
   use the prefix form to locate binary data without looking for text tokens. */
int cupidimage_inflate_zlib(const uint8_t *data, size_t size, uint8_t *out, size_t outcap,
                            size_t *outlen) {
    size_t consumed = 0;
    if (!outlen)
        return 0;
    int ok = cupidimage_inflate_zlib_prefix(data, size, out, outcap, outlen, &consumed);
    if (!ok || consumed != size) {
        *outlen = 0;
        return 0;
    }
    return 1;
}
