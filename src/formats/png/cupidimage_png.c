#include "core/internal.h"
#include "cupidimage.h"

#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg);
    }
}

static uint32_t read_be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = abs(p - (int)a);
    int pb = abs(p - (int)b);
    int pc = abs(p - (int)c);
    if (pa <= pb && pa <= pc) {
        return a;
    }
    if (pb <= pc) {
        return b;
    }
    return c;
}

static int unfilter(const uint8_t *raw, size_t raw_size, uint8_t *recon, size_t rowbytes,
                    uint32_t height, int bpp) {
    size_t expected = (rowbytes + 1) * (size_t)height;
    if (raw_size < expected) {
        return 0;
    }

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *in = raw + y * (rowbytes + 1) + 1;
        uint8_t *out = recon + y * rowbytes;
        uint8_t filter = raw[y * (rowbytes + 1)];

        switch (filter) {
            case 0:
                memcpy(out, in, rowbytes);
                break;
            case 1:
                for (size_t x = 0; x < rowbytes; x++) {
                    uint8_t left = (x >= (size_t)bpp) ? out[x - (size_t)bpp] : 0;
                    out[x] = (uint8_t)(in[x] + left);
                }
                break;
            case 2:
                for (size_t x = 0; x < rowbytes; x++) {
                    uint8_t up = (y > 0) ? recon[(y - 1) * rowbytes + x] : 0;
                    out[x] = (uint8_t)(in[x] + up);
                }
                break;
            case 3:
                for (size_t x = 0; x < rowbytes; x++) {
                    uint8_t left = (x >= (size_t)bpp) ? out[x - (size_t)bpp] : 0;
                    uint8_t up = (y > 0) ? recon[(y - 1) * rowbytes + x] : 0;
                    out[x] = (uint8_t)(in[x] + (uint8_t)(((int)left + (int)up) / 2));
                }
                break;
            case 4:
                for (size_t x = 0; x < rowbytes; x++) {
                    uint8_t left = (x >= (size_t)bpp) ? out[x - (size_t)bpp] : 0;
                    uint8_t up = (y > 0) ? recon[(y - 1) * rowbytes + x] : 0;
                    uint8_t up_left = (y > 0 && x >= (size_t)bpp)
                                          ? recon[(y - 1) * rowbytes + x - (size_t)bpp]
                                          : 0;
                    out[x] = (uint8_t)(in[x] + paeth(left, up, up_left));
                }
                break;
            default:
                return 0;
        }
    }

    return 1;
}

static int calc_rowbytes(uint32_t width, uint8_t color_type, uint8_t bit_depth, int channels,
                         size_t *rowbytes_out, int *bpp_out) {
    if (color_type == 3 || (color_type == 0 && bit_depth < 8)) {
        size_t bits_per_row = (size_t)width * (size_t)bit_depth;
        if (width > 0 && bits_per_row / (size_t)bit_depth != (size_t)width) {
            return 0;
        }
        size_t rowbytes = (bits_per_row + 7u) / 8u;
        *rowbytes_out = rowbytes;
        *bpp_out = 1;
        return 1;
    }
    size_t bytes_per_pixel = (size_t)channels;
    if (bit_depth == 16) {
        bytes_per_pixel *= 2u;
    }
    size_t rowbytes = (size_t)width * bytes_per_pixel;
    if (width > 0 && rowbytes / bytes_per_pixel != (size_t)width) {
        return 0;
    }
    *rowbytes_out = rowbytes;
    *bpp_out = (int)bytes_per_pixel;
    return 1;
}

static uint8_t palette_index_at(const uint8_t *row, uint32_t x, uint8_t bit_depth) {
    if (bit_depth == 8) {
        return row[x];
    }
    if (bit_depth == 4) {
        uint8_t byte = row[x / 2u];
        return (x % 2u == 0) ? (byte >> 4) : (byte & 0x0Fu);
    }
    if (bit_depth == 2) {
        uint8_t byte = row[x / 4u];
        unsigned shift = 6u - 2u * (x % 4u);
        return (uint8_t)(((uint32_t)byte >> shift) & 0x03u);
    }
    uint8_t byte = row[x / 8u];
    unsigned shift = 7u - (x % 8u);
    return (uint8_t)(((uint32_t)byte >> shift) & 0x01u);
}

static uint8_t gray_sample_at(const uint8_t *row, uint32_t x, uint8_t bit_depth) {
    if (bit_depth == 8) {
        return row[x];
    }
    if (bit_depth == 4) {
        uint8_t byte = row[x / 2u];
        return (x % 2u == 0) ? (byte >> 4) : (byte & 0x0Fu);
    }
    if (bit_depth == 2) {
        uint8_t byte = row[x / 4u];
        unsigned shift = 6u - 2u * (x % 4u);
        return (uint8_t)(((uint32_t)byte >> shift) & 0x03u);
    }
    uint8_t byte = row[x / 8u];
    unsigned shift = 7u - (x % 8u);
    return (uint8_t)(((uint32_t)byte >> shift) & 0x01u);
}

static uint8_t sample16_to_8(uint16_t v) {
    return (uint8_t)((v + 128u) / 257u);
}

static uint16_t read_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint8_t transparency_alpha(const uint8_t *row, uint32_t x, uint8_t bit_depth,
                                  uint8_t color_type, const uint8_t *trns) {
    unsigned mask = (1u << bit_depth) - 1u;
    if (color_type == 0) {
        uint16_t sample =
            bit_depth == 16 ? read_be16(row + (size_t)x * 2u) : gray_sample_at(row, x, bit_depth);
        return sample == (read_be16(trns) & mask) ? 0 : 255;
    }
    for (unsigned c = 0; c < 3; c++) {
        size_t offset = (size_t)x * 3u + c;
        uint16_t sample = bit_depth == 16 ? read_be16(row + offset * 2u) : row[offset];
        if (sample != (read_be16(trns + c * 2u) & mask))
            return 255;
    }
    return 0;
}

int cupidimage_load_png(const unsigned char *data, size_t size, cupidimage_image *out, char *err,
                        size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }

    memset(out, 0, sizeof(*out));

    static const unsigned char sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (size < 8 || memcmp(data, sig, 8) != 0) {
        set_err(err, errcap, "not a PNG");
        return 0;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t bit_depth = 0;
    uint8_t color_type = 0;
    uint8_t compression = 0;
    uint8_t filter = 0;
    uint8_t interlace = 0;

    uint8_t palette[256 * 3];
    uint8_t palette_alpha[256];
    uint8_t trns[256];
    int palette_entries = 0;
    int trns_entries = 0;
    int have_trns = 0;
    for (int i = 0; i < 256; i++) {
        palette_alpha[i] = 255;
    }

    uint8_t *idat = NULL;
    size_t idat_size = 0;

    int have_header = 0, have_palette = 0, have_data = 0, data_ended = 0, have_end = 0;
    size_t chunk_off = 8;
    while (size - chunk_off >= 12) {
        uint32_t length = read_be32(data + chunk_off);
        const unsigned char *type = data + chunk_off + 4;
        if (length > 0x7fffffffu || (size_t)length > size - chunk_off - 12) {
            free(idat);
            set_err(err, errcap, "invalid PNG chunk length");
            return 0;
        }
        for (unsigned i = 0; i < 4; i++) {
            if (!((type[i] >= 'A' && type[i] <= 'Z') || (type[i] >= 'a' && type[i] <= 'z'))) {
                free(idat);
                set_err(err, errcap, "invalid PNG chunk type");
                return 0;
            }
        }
        uint32_t crc = UINT32_C(0xffffffff);
        for (size_t i = 0; i < (size_t)length + 4; i++) {
            crc ^= type[i];
            for (unsigned bit = 0; bit < 8; bit++)
                crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
        }
        const unsigned char *chunk = type + 4;
        if (~crc != read_be32(chunk + length)) {
            free(idat);
            set_err(err, errcap, "PNG chunk CRC mismatch");
            return 0;
        }
        int header = memcmp(type, "IHDR", 4) == 0;
        int palette_chunk = memcmp(type, "PLTE", 4) == 0;
        int transparency = memcmp(type, "tRNS", 4) == 0;
        int image_data = memcmp(type, "IDAT", 4) == 0;
        int end = memcmp(type, "IEND", 4) == 0;
        if ((!have_header && !header) || (header && have_header) ||
            (palette_chunk &&
             (have_palette || have_data || have_trns || color_type == 0 || color_type == 4)) ||
            (transparency && (have_trns || have_data || (color_type == 3 && !have_palette))) ||
            (image_data && (data_ended || (color_type == 3 && !have_palette))) ||
            (end && (!have_data || length != 0)) ||
            (!(type[0] & 32) && !header && !palette_chunk && !image_data && !end)) {
            free(idat);
            set_err(err, errcap, "invalid PNG chunk ordering or unsupported critical chunk");
            return 0;
        }
        if (have_data && !image_data)
            data_ended = 1;
        if (header) {
            if (length != 13) {
                free(idat);
                set_err(err, errcap, "invalid IHDR");
                return 0;
            }
            have_header = 1;
            width = read_be32(chunk);
            height = read_be32(chunk + 4);
            bit_depth = chunk[8];
            color_type = chunk[9];
            compression = chunk[10];
            filter = chunk[11];
            interlace = chunk[12];
            if (width > 0x7fffffffu || height > 0x7fffffffu) {
                free(idat);
                set_err(err, errcap, "invalid PNG dimensions");
                return 0;
            }
        } else if (palette_chunk) {
            if (!length || length % 3 || length / 3 > 256 ||
                (color_type == 3 && bit_depth < 8 && length / 3 > (1u << bit_depth))) {
                free(idat);
                set_err(err, errcap, "invalid PLTE");
                return 0;
            }
            have_palette = 1;
            palette_entries = (int)(length / 3);
            memcpy(palette, chunk, length);
        } else if (transparency) {
            if (length > 256) {
                free(idat);
                set_err(err, errcap, "invalid tRNS");
                return 0;
            }
            trns_entries = (int)length;
            have_trns = 1;
            memcpy(trns, chunk, length);
        } else if (image_data) {
            have_data = 1;
            if ((size_t)length > SIZE_MAX - idat_size) {
                free(idat);
                set_err(err, errcap, "PNG data too large");
                return 0;
            }
            if (length) {
                uint8_t *next = (uint8_t *)realloc(idat, idat_size + length);
                if (!next) {
                    free(idat);
                    set_err(err, errcap, "out of memory");
                    return 0;
                }
                idat = next;
                memcpy(idat + idat_size, chunk, length);
                idat_size += length;
            }
        } else if (end) {
            have_end = 1;
            break;
        }
        chunk_off += (size_t)length + 12;
    }
    if (!have_end) {
        free(idat);
        set_err(err, errcap, "missing IEND or truncated PNG");
        return 0;
    }

    if (width == 0 || height == 0) {
        free(idat);
        set_err(err, errcap, "missing IHDR");
        return 0;
    }
    if (idat_size == 0) {
        free(idat);
        set_err(err, errcap, "missing IDAT");
        return 0;
    }
    if (compression != 0 || filter != 0 || (interlace != 0 && interlace != 1)) {
        free(idat);
        set_err(err, errcap, "unsupported PNG method");
        return 0;
    }
    if (have_trns &&
        ((color_type == 0 && trns_entries != 2) || (color_type == 2 && trns_entries != 6) ||
         (color_type == 3 && trns_entries > palette_entries) || color_type == 4 ||
         color_type == 6)) {
        free(idat);
        set_err(err, errcap, "invalid tRNS");
        return 0;
    }
    if (color_type == 3) {
        if (!(bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8)) {
            free(idat);
            set_err(err, errcap, "unsupported palette bit depth");
            return 0;
        }
        if (palette_entries == 0) {
            free(idat);
            set_err(err, errcap, "missing PLTE");
            return 0;
        }
        if (have_trns)
            memcpy(palette_alpha, trns, (size_t)trns_entries);
    } else if (color_type == 0) {
        if (!(bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8 ||
              bit_depth == 16)) {
            free(idat);
            set_err(err, errcap, "unsupported grayscale bit depth");
            return 0;
        }
    } else {
        if (!(bit_depth == 8 || bit_depth == 16)) {
            free(idat);
            set_err(err, errcap, "only 8-bit or 16-bit PNG supported");
            return 0;
        }
    }

    int channels = 0;
    switch (color_type) {
        case 0:
            channels = 1;
            break;
        case 2:
            channels = 3;
            break;
        case 3:
            channels = 1;
            break;
        case 4:
            channels = 2;
            break;
        case 6:
            channels = 4;
            break;
        default:
            free(idat);
            set_err(err, errcap, "unsupported color type");
            return 0;
    }

    size_t rowbytes = 0;
    int bpp = 0;
    if (!calc_rowbytes(width, color_type, bit_depth, channels, &rowbytes, &bpp)) {
        free(idat);
        set_err(err, errcap, "image too large");
        return 0;
    }

    if ((size_t)width > SIZE_MAX / 4u / (size_t)height) {
        free(idat);
        set_err(err, errcap, "image too large");
        return 0;
    }
    size_t rgba_size = (size_t)width * (size_t)height * 4u;
    if (rowbytes == SIZE_MAX) {
        free(idat);
        set_err(err, errcap, "image too large");
        return 0;
    }

    if (interlace == 0) {
        size_t raw_size = (rowbytes + 1) * (size_t)height;
        if (raw_size / (rowbytes + 1) != (size_t)height) {
            free(idat);
            set_err(err, errcap, "image too large");
            return 0;
        }

        uint8_t *raw = (uint8_t *)malloc(raw_size);
        if (!raw) {
            free(idat);
            set_err(err, errcap, "out of memory");
            return 0;
        }

        size_t outlen = 0;
        if (!cupidimage_inflate_zlib(idat, idat_size, raw, raw_size, &outlen) ||
            outlen != raw_size) {
            free(idat);
            free(raw);
            set_err(err, errcap, "zlib decode failed");
            return 0;
        }
        free(idat);

        uint8_t *recon = (uint8_t *)malloc(rowbytes * (size_t)height);
        if (!recon) {
            free(raw);
            set_err(err, errcap, "out of memory");
            return 0;
        }

        if (!unfilter(raw, raw_size, recon, rowbytes, height, bpp)) {
            free(raw);
            free(recon);
            set_err(err, errcap, "PNG filter failed");
            return 0;
        }
        free(raw);

        uint8_t *rgba = (uint8_t *)malloc(rgba_size);
        if (!rgba) {
            free(recon);
            set_err(err, errcap, "out of memory");
            return 0;
        }

        for (uint32_t y = 0; y < height; y++) {
            const uint8_t *src = recon + (size_t)y * rowbytes;
            uint8_t *dst = rgba + (size_t)y * (size_t)width * 4u;
            for (uint32_t x = 0; x < width; x++) {
                if (color_type == 0) {
                    if (bit_depth == 16) {
                        size_t off = (size_t)x * 2u;
                        uint16_t g16 = (uint16_t)((src[off] << 8) | src[off + 1]);
                        uint8_t g = sample16_to_8(g16);
                        dst[x * 4 + 0] = g;
                        dst[x * 4 + 1] = g;
                        dst[x * 4 + 2] = g;
                        dst[x * 4 + 3] = 255;
                    } else if (bit_depth == 8) {
                        uint8_t g = src[x];
                        dst[x * 4 + 0] = g;
                        dst[x * 4 + 1] = g;
                        dst[x * 4 + 2] = g;
                        dst[x * 4 + 3] = 255;
                    } else {
                        uint8_t v = gray_sample_at(src, x, bit_depth);
                        unsigned maxv = (1u << bit_depth) - 1u;
                        uint8_t g = (uint8_t)((v * 255u + maxv / 2u) / maxv);
                        dst[x * 4 + 0] = g;
                        dst[x * 4 + 1] = g;
                        dst[x * 4 + 2] = g;
                        dst[x * 4 + 3] = 255;
                    }
                } else if (color_type == 2) {
                    if (bit_depth == 16) {
                        size_t off = (size_t)x * 6u;
                        uint16_t r16 = (uint16_t)((src[off] << 8) | src[off + 1]);
                        uint16_t g16 = (uint16_t)((src[off + 2] << 8) | src[off + 3]);
                        uint16_t b16 = (uint16_t)((src[off + 4] << 8) | src[off + 5]);
                        dst[x * 4 + 0] = sample16_to_8(r16);
                        dst[x * 4 + 1] = sample16_to_8(g16);
                        dst[x * 4 + 2] = sample16_to_8(b16);
                        dst[x * 4 + 3] = 255;
                    } else {
                        dst[x * 4 + 0] = src[x * 3 + 0];
                        dst[x * 4 + 1] = src[x * 3 + 1];
                        dst[x * 4 + 2] = src[x * 3 + 2];
                        dst[x * 4 + 3] = 255;
                    }
                } else if (color_type == 3) {
                    uint8_t idx = palette_index_at(src, x, bit_depth);
                    if ((int)idx >= palette_entries) {
                        free(recon);
                        free(rgba);
                        set_err(err, errcap, "palette index out of range");
                        return 0;
                    }
                    dst[x * 4 + 0] = palette[(int)idx * 3 + 0];
                    dst[x * 4 + 1] = palette[(int)idx * 3 + 1];
                    dst[x * 4 + 2] = palette[(int)idx * 3 + 2];
                    dst[x * 4 + 3] = palette_alpha[idx];
                } else if (color_type == 4) {
                    if (bit_depth == 16) {
                        size_t off = (size_t)x * 4u;
                        uint16_t g16 = (uint16_t)((src[off] << 8) | src[off + 1]);
                        uint16_t a16 = (uint16_t)((src[off + 2] << 8) | src[off + 3]);
                        dst[x * 4 + 0] = sample16_to_8(g16);
                        dst[x * 4 + 1] = sample16_to_8(g16);
                        dst[x * 4 + 2] = sample16_to_8(g16);
                        dst[x * 4 + 3] = sample16_to_8(a16);
                    } else {
                        uint8_t g = src[x * 2 + 0];
                        uint8_t a = src[x * 2 + 1];
                        dst[x * 4 + 0] = g;
                        dst[x * 4 + 1] = g;
                        dst[x * 4 + 2] = g;
                        dst[x * 4 + 3] = a;
                    }
                } else {
                    if (bit_depth == 16) {
                        size_t off = (size_t)x * 8u;
                        uint16_t r16 = (uint16_t)((src[off] << 8) | src[off + 1]);
                        uint16_t g16 = (uint16_t)((src[off + 2] << 8) | src[off + 3]);
                        uint16_t b16 = (uint16_t)((src[off + 4] << 8) | src[off + 5]);
                        uint16_t a16 = (uint16_t)((src[off + 6] << 8) | src[off + 7]);
                        dst[x * 4 + 0] = sample16_to_8(r16);
                        dst[x * 4 + 1] = sample16_to_8(g16);
                        dst[x * 4 + 2] = sample16_to_8(b16);
                        dst[x * 4 + 3] = sample16_to_8(a16);
                    } else {
                        dst[x * 4 + 0] = src[x * 4 + 0];
                        dst[x * 4 + 1] = src[x * 4 + 1];
                        dst[x * 4 + 2] = src[x * 4 + 2];
                        dst[x * 4 + 3] = src[x * 4 + 3];
                    }
                }
                if (have_trns && (color_type == 0 || color_type == 2)) {
                    dst[x * 4 + 3] = transparency_alpha(src, x, bit_depth, color_type, trns);
                }
            }
        }

        free(recon);

        out->width = width;
        out->height = height;
        out->rgba = rgba;
        return 1;
    }

    static const uint32_t pass_x[7] = {0u, 4u, 0u, 2u, 0u, 1u, 0u};
    static const uint32_t pass_y[7] = {0u, 0u, 4u, 0u, 2u, 0u, 1u};
    static const uint32_t pass_dx[7] = {8u, 8u, 4u, 4u, 2u, 2u, 1u};
    static const uint32_t pass_dy[7] = {8u, 8u, 8u, 4u, 4u, 2u, 2u};

    size_t total_raw = 0;
    for (int p = 0; p < 7; p++) {
        uint32_t pw =
            (width > pass_x[p]) ? ((width - pass_x[p] + pass_dx[p] - 1u) / pass_dx[p]) : 0u;
        uint32_t ph =
            (height > pass_y[p]) ? ((height - pass_y[p] + pass_dy[p] - 1u) / pass_dy[p]) : 0u;
        if (pw == 0 || ph == 0) {
            continue;
        }
        size_t pass_rowbytes = 0;
        int pass_bpp = 0;
        if (!calc_rowbytes(pw, color_type, bit_depth, channels, &pass_rowbytes, &pass_bpp)) {
            free(idat);
            set_err(err, errcap, "image too large");
            return 0;
        }
        size_t pass_size = (pass_rowbytes + 1) * (size_t)ph;
        if (pass_size / (pass_rowbytes + 1) != (size_t)ph) {
            free(idat);
            set_err(err, errcap, "image too large");
            return 0;
        }
        if (total_raw + pass_size < total_raw) {
            free(idat);
            set_err(err, errcap, "image too large");
            return 0;
        }
        total_raw += pass_size;
    }

    uint8_t *raw = (uint8_t *)malloc(total_raw);
    if (!raw) {
        free(idat);
        set_err(err, errcap, "out of memory");
        return 0;
    }
    size_t outlen = 0;
    if (!cupidimage_inflate_zlib(idat, idat_size, raw, total_raw, &outlen) || outlen != total_raw) {
        free(idat);
        free(raw);
        set_err(err, errcap, "zlib decode failed");
        return 0;
    }
    free(idat);

    uint8_t *rgba = (uint8_t *)malloc(rgba_size);
    if (!rgba) {
        free(raw);
        set_err(err, errcap, "out of memory");
        return 0;
    }

    size_t offset = 0;
    for (int p = 0; p < 7; p++) {
        uint32_t pw =
            (width > pass_x[p]) ? ((width - pass_x[p] + pass_dx[p] - 1u) / pass_dx[p]) : 0u;
        uint32_t ph =
            (height > pass_y[p]) ? ((height - pass_y[p] + pass_dy[p] - 1u) / pass_dy[p]) : 0u;
        if (pw == 0 || ph == 0) {
            continue;
        }
        size_t pass_rowbytes = 0;
        int pass_bpp = 0;
        if (!calc_rowbytes(pw, color_type, bit_depth, channels, &pass_rowbytes, &pass_bpp)) {
            free(raw);
            free(rgba);
            set_err(err, errcap, "image too large");
            return 0;
        }
        size_t pass_raw_size = (pass_rowbytes + 1) * (size_t)ph;
        if (offset + pass_raw_size > total_raw) {
            free(raw);
            free(rgba);
            set_err(err, errcap, "truncated PNG");
            return 0;
        }

        uint8_t *recon = (uint8_t *)malloc(pass_rowbytes * (size_t)ph);
        if (!recon) {
            free(raw);
            free(rgba);
            set_err(err, errcap, "out of memory");
            return 0;
        }
        if (!unfilter(raw + offset, pass_raw_size, recon, pass_rowbytes, ph, pass_bpp)) {
            free(raw);
            free(rgba);
            free(recon);
            set_err(err, errcap, "PNG filter failed");
            return 0;
        }
        offset += pass_raw_size;

        for (uint32_t y = 0; y < ph; y++) {
            const uint8_t *src = recon + (size_t)y * pass_rowbytes;
            uint32_t dest_y = pass_y[p] + y * pass_dy[p];
            for (uint32_t x = 0; x < pw; x++) {
                uint32_t dest_x = pass_x[p] + x * pass_dx[p];
                uint8_t *dst = rgba + ((size_t)dest_y * (size_t)width + (size_t)dest_x) * 4u;
                if (color_type == 0) {
                    if (bit_depth == 16) {
                        size_t off = (size_t)x * 2u;
                        uint16_t g16 = (uint16_t)((src[off] << 8) | src[off + 1]);
                        uint8_t g = sample16_to_8(g16);
                        dst[0] = g;
                        dst[1] = g;
                        dst[2] = g;
                        dst[3] = 255;
                    } else if (bit_depth == 8) {
                        uint8_t g = src[x];
                        dst[0] = g;
                        dst[1] = g;
                        dst[2] = g;
                        dst[3] = 255;
                    } else {
                        uint8_t v = gray_sample_at(src, x, bit_depth);
                        unsigned maxv = (1u << bit_depth) - 1u;
                        uint8_t g = (uint8_t)((v * 255u + maxv / 2u) / maxv);
                        dst[0] = g;
                        dst[1] = g;
                        dst[2] = g;
                        dst[3] = 255;
                    }
                } else if (color_type == 2) {
                    if (bit_depth == 16) {
                        size_t off = (size_t)x * 6u;
                        uint16_t r16 = (uint16_t)((src[off] << 8) | src[off + 1]);
                        uint16_t g16 = (uint16_t)((src[off + 2] << 8) | src[off + 3]);
                        uint16_t b16 = (uint16_t)((src[off + 4] << 8) | src[off + 5]);
                        dst[0] = sample16_to_8(r16);
                        dst[1] = sample16_to_8(g16);
                        dst[2] = sample16_to_8(b16);
                        dst[3] = 255;
                    } else {
                        dst[0] = src[x * 3 + 0];
                        dst[1] = src[x * 3 + 1];
                        dst[2] = src[x * 3 + 2];
                        dst[3] = 255;
                    }
                } else if (color_type == 3) {
                    uint8_t idx = palette_index_at(src, x, bit_depth);
                    if ((int)idx >= palette_entries) {
                        free(raw);
                        free(rgba);
                        free(recon);
                        set_err(err, errcap, "palette index out of range");
                        return 0;
                    }
                    dst[0] = palette[(int)idx * 3 + 0];
                    dst[1] = palette[(int)idx * 3 + 1];
                    dst[2] = palette[(int)idx * 3 + 2];
                    dst[3] = palette_alpha[idx];
                } else if (color_type == 4) {
                    if (bit_depth == 16) {
                        size_t off = (size_t)x * 4u;
                        uint16_t g16 = (uint16_t)((src[off] << 8) | src[off + 1]);
                        uint16_t a16 = (uint16_t)((src[off + 2] << 8) | src[off + 3]);
                        dst[0] = sample16_to_8(g16);
                        dst[1] = sample16_to_8(g16);
                        dst[2] = sample16_to_8(g16);
                        dst[3] = sample16_to_8(a16);
                    } else {
                        uint8_t g = src[x * 2 + 0];
                        uint8_t a = src[x * 2 + 1];
                        dst[0] = g;
                        dst[1] = g;
                        dst[2] = g;
                        dst[3] = a;
                    }
                } else {
                    if (bit_depth == 16) {
                        size_t off = (size_t)x * 8u;
                        uint16_t r16 = (uint16_t)((src[off] << 8) | src[off + 1]);
                        uint16_t g16 = (uint16_t)((src[off + 2] << 8) | src[off + 3]);
                        uint16_t b16 = (uint16_t)((src[off + 4] << 8) | src[off + 5]);
                        uint16_t a16 = (uint16_t)((src[off + 6] << 8) | src[off + 7]);
                        dst[0] = sample16_to_8(r16);
                        dst[1] = sample16_to_8(g16);
                        dst[2] = sample16_to_8(b16);
                        dst[3] = sample16_to_8(a16);
                    } else {
                        dst[0] = src[x * 4 + 0];
                        dst[1] = src[x * 4 + 1];
                        dst[2] = src[x * 4 + 2];
                        dst[3] = src[x * 4 + 3];
                    }
                }
                if (have_trns && (color_type == 0 || color_type == 2)) {
                    dst[3] = transparency_alpha(src, x, bit_depth, color_type, trns);
                }
            }
        }
        free(recon);
    }

    free(raw);
    out->width = width;
    out->height = height;
    out->rgba = rgba;
    return 1;
}

int cupidimage_load_png_file(const char *path, cupidimage_image *out, char *err, size_t errcap) {
    return cupidimage_load_image_file_via_memory(path, out, err, errcap, cupidimage_load_png);
}
