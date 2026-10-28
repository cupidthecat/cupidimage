#include "tiff_internal.h"

static int tiff_apply_predictor_row(const tiff_context *ctx, uint8_t *row, size_t row_bytes,
                                    uint16_t samples, uint16_t bps);
static int tiff_decompress_block(const tiff_context *ctx, const tiff_image_info *info,
                                 const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size,
                                 uint32_t width, uint32_t rows, char *err, size_t errcap);
static int tiff_decode_strips_chunky(const tiff_context *ctx, const tiff_image_info *info,
                                     uint8_t *raw, size_t raw_size, size_t row_bytes, char *err,
                                     size_t errcap);
static int tiff_decode_strips_planar(const tiff_context *ctx, const tiff_image_info *info,
                                     uint8_t *raw, size_t raw_size, size_t row_bytes,
                                     size_t bytes_per_sample, char *err, size_t errcap);
static int tiff_decode_tiles_chunky(const tiff_context *ctx, const tiff_image_info *info,
                                    uint8_t *raw, size_t raw_size, size_t row_bytes,
                                    size_t bytes_per_pixel, char *err, size_t errcap);
static int tiff_decode_tiles_planar(const tiff_context *ctx, const tiff_image_info *info,
                                    uint8_t *raw, size_t raw_size, size_t row_bytes,
                                    size_t bytes_per_sample, char *err, size_t errcap);
static int tiff_build_jpeg_stream(const uint8_t *tables, size_t tables_size, const uint8_t *data,
                                  size_t data_size, uint8_t **out, size_t *out_size, char *err,
                                  size_t errcap);
static int tiff_decode_jpeg_stream(const unsigned char *data, size_t size, uint32_t expected_w,
                                   uint32_t expected_h, uint8_t *rgba, size_t rgba_size, char *err,
                                   size_t errcap);

uint8_t cupidimage_tiff_sample16_to_8(uint16_t v) {
    return (uint8_t)((v + 128u) / 257u);
}

uint32_t cupidimage_tiff_read_sample(const tiff_context *ctx, const uint8_t *row, uint32_t index,
                                     uint16_t bps, uint16_t fill_order) {
    if (bps == 0 || bps > 32) {
        return 0;
    }
    if ((bps & 7u) == 0) {
        uint32_t bytes = bps / 8u;
        const uint8_t *p = row + (size_t)index * bytes;
        uint32_t v = 0;
        if (ctx->big_endian) {
            for (uint32_t i = 0; i < bytes; i++) {
                v = (v << 8) | p[i];
            }
        } else {
            for (uint32_t i = 0; i < bytes; i++) {
                v |= (uint32_t)p[i] << (8u * i);
            }
        }
        return v;
    }

    uint32_t bitpos = index * bps;
    uint32_t v = 0;
    for (uint16_t i = 0; i < bps; i++) {
        uint32_t pos = bitpos + i;
        uint8_t byte = row[pos / 8u];
        uint32_t bit = pos & 7u;
        if (fill_order == TIFF_FILL_ORDER_LSB2MSB) {
            v = (v << 1) | (((uint32_t)byte >> bit) & 1u);
        } else {
            v = (v << 1) | (((uint32_t)byte >> (7u - bit)) & 1u);
        }
    }
    return v;
}

uint8_t cupidimage_tiff_scale_sample(uint32_t v, uint16_t bps) {
    if (bps == 0) {
        return 0;
    }
    if (bps == 8) {
        return (uint8_t)v;
    }
    if (bps == 16) {
        return cupidimage_tiff_sample16_to_8((uint16_t)v);
    }
    if (bps >= 32) {
        uint64_t maxv = 0xFFFFFFFFu;
        return (uint8_t)(((uint64_t)v * 255u + maxv / 2u) / maxv);
    }
    uint32_t maxv = (1u << bps) - 1u;
    return (uint8_t)(((uint64_t)v * 255u + maxv / 2u) / maxv);
}

uint8_t cupidimage_tiff_clamp8(int v) {
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (uint8_t)v;
}

uint8_t cupidimage_tiff_scale_float(double v) {
    if (v <= 0.0) {
        return 0;
    }
    if (v <= 1.0) {
        return (uint8_t)(v * 255.0 + 0.5);
    }
    if (v < 255.0) {
        return (uint8_t)(v + 0.5);
    }
    return 255;
}

double cupidimage_tiff_read_sample_float(const tiff_context *ctx, const uint8_t *row,
                                         uint32_t index, uint16_t bps) {
    if (bps == 32) {
        const uint8_t *p = row + (size_t)index * 4u;
        uint32_t u = cupidimage_tiff_read_u32(ctx, p);
        float f;
        memcpy(&f, &u, sizeof(float));
        return (double)f;
    }
    if (bps == 64) {
        const uint8_t *p = row + (size_t)index * 8u;
        uint64_t u = cupidimage_tiff_read_u64(ctx, p);
        double d;
        memcpy(&d, &u, sizeof(double));
        return d;
    }
    return 0.0;
}

static int tiff_apply_predictor_row(const tiff_context *ctx, uint8_t *row, size_t row_bytes,
                                    uint16_t samples, uint16_t bps) {
    if (bps == 8) {
        size_t stride = (size_t)samples;
        if (stride == 0) {
            return 0;
        }
        for (size_t i = stride; i < row_bytes; i++) {
            row[i] = (uint8_t)(row[i] + row[i - stride]);
        }
        return 1;
    }
    if (bps == 16) {
        size_t stride = (size_t)samples * 2u;
        if (stride == 0) {
            return 0;
        }
        for (size_t i = stride; i + 1 < row_bytes; i += 2) {
            uint16_t v = cupidimage_tiff_read_u16(ctx, row + i);
            uint16_t prev = cupidimage_tiff_read_u16(ctx, row + i - stride);
            v = (uint16_t)(v + prev);
            cupidimage_tiff_write_u16(ctx, row + i, v);
        }
        return 1;
    }
    return 0;
}

int cupidimage_tiff_apply_predictor(const tiff_context *ctx, const tiff_image_info *info,
                                    uint8_t *raw, size_t row_bytes, char *err, size_t errcap) {
    if (info->predictor != 2) {
        return 1;
    }
    if (info->bits_per_sample[0] != 8 && info->bits_per_sample[0] != 16) {
        cupidimage_set_err(err, errcap, "unsupported TIFF predictor bit depth");
        return 0;
    }
    for (uint32_t y = 0; y < info->height; y++) {
        uint8_t *row = raw + (size_t)y * row_bytes;
        if (!tiff_apply_predictor_row(ctx, row, row_bytes, info->samples_per_pixel,
                                      info->bits_per_sample[0])) {
            cupidimage_set_err(err, errcap, "TIFF predictor failed");
            return 0;
        }
    }
    return 1;
}

static int tiff_decompress_block(const tiff_context *ctx, const tiff_image_info *info,
                                 const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size,
                                 uint32_t width, uint32_t rows, char *err, size_t errcap) {
    (void)ctx;
    switch (info->compression) {
        case TIFF_COMPRESSION_NONE:
            if (src_size < dst_size) {
                cupidimage_set_err(err, errcap, "truncated TIFF data");
                return 0;
            }
            memcpy(dst, src, dst_size);
            return 1;
        case TIFF_COMPRESSION_LZW:
            return cupidimage_tiff_lzw_decode(src, src_size, dst, dst_size, err, errcap);
        case TIFF_COMPRESSION_PACKBITS:
            return cupidimage_tiff_packbits_decode(src, src_size, dst, dst_size, err, errcap);
        case TIFF_COMPRESSION_DEFLATE:
        case TIFF_COMPRESSION_ADOBE_DEFLATE: {
            size_t outlen = 0;
            if (!cupidimage_inflate_zlib(src, src_size, dst, dst_size, &outlen) ||
                outlen != dst_size) {
                cupidimage_set_err(err, errcap, "deflate decode failed");
                return 0;
            }
            return 1;
        }
        case TIFF_COMPRESSION_CCITT_RLE:
        case TIFF_COMPRESSION_CCITT_G3:
        case TIFF_COMPRESSION_CCITT_G4:
            return cupidimage_tiff_ccitt_decode(src, src_size, dst, dst_size, width, rows,
                                                info->compression, info->group3_options,
                                                info->fill_order, err, errcap);
        default:
            cupidimage_set_err(err, errcap, "unsupported TIFF compression");
            return 0;
    }
}

static int tiff_decode_strips_chunky(const tiff_context *ctx, const tiff_image_info *info,
                                     uint8_t *raw, size_t raw_size, size_t row_bytes, char *err,
                                     size_t errcap) {
    uint64_t strips_expected =
        ((uint64_t)info->height + info->rows_per_strip - 1u) / info->rows_per_strip;
    uint64_t strip_count = info->strip_offsets_count;
    if (strip_count < strips_expected) {
        cupidimage_set_err(err, errcap, "invalid number of strips");
        return 0;
    }
    if (strip_count > strips_expected) {
        strip_count = strips_expected;
    }

    for (uint64_t i = 0; i < strip_count; i++) {
        uint64_t row_start = i * (uint64_t)info->rows_per_strip;
        if (row_start >= info->height) {
            break;
        }
        uint32_t strip_rows = info->rows_per_strip;
        if (row_start + strip_rows > info->height) {
            strip_rows = info->height - (uint32_t)row_start;
        }
        size_t expected = 0;
        if (cupidimage_tiff_mul_overflow_size_t(row_bytes, (size_t)strip_rows, &expected)) {
            cupidimage_set_err(err, errcap, "invalid TIFF strip size");
            return 0;
        }
        size_t dst_off = 0;
        if (cupidimage_tiff_mul_overflow_size_t(row_bytes, (size_t)row_start, &dst_off)) {
            cupidimage_set_err(err, errcap, "invalid TIFF strip size");
            return 0;
        }
        if (dst_off + expected > raw_size) {
            cupidimage_set_err(err, errcap, "TIFF strip out of bounds");
            return 0;
        }

        uint64_t off = info->strip_offsets[i];
        if (off >= ctx->size) {
            cupidimage_set_err(err, errcap, "TIFF strip offset out of range");
            return 0;
        }
        size_t src_size = expected;
        if (info->compression != TIFF_COMPRESSION_NONE) {
            if (!info->strip_byte_counts || i >= info->strip_byte_counts_count) {
                cupidimage_set_err(err, errcap, "invalid StripByteCounts");
                return 0;
            }
            if (info->strip_byte_counts[i] == 0 ||
                !cupidimage_tiff_range_valid(ctx, off, info->strip_byte_counts[i])) {
                cupidimage_set_err(err, errcap, "invalid StripByteCounts");
                return 0;
            }
            src_size = (size_t)info->strip_byte_counts[i];
        } else if (info->strip_byte_counts && i < info->strip_byte_counts_count) {
            if (info->strip_byte_counts[i] < expected) {
                cupidimage_set_err(err, errcap, "truncated TIFF strip");
                return 0;
            }
        }
        if (!cupidimage_tiff_range_valid(ctx, off, src_size)) {
            cupidimage_set_err(err, errcap, "truncated TIFF strip");
            return 0;
        }

        if (!tiff_decompress_block(ctx, info, ctx->data + (size_t)off, src_size, raw + dst_off,
                                   expected, info->width, strip_rows, err, errcap)) {
            return 0;
        }
    }

    return 1;
}

static int tiff_decode_strips_planar(const tiff_context *ctx, const tiff_image_info *info,
                                     uint8_t *raw, size_t raw_size, size_t row_bytes,
                                     size_t bytes_per_sample, char *err, size_t errcap) {
    (void)raw_size;
    if ((info->bits_per_sample[0] & 7u) != 0) {
        cupidimage_set_err(err, errcap, "planar TIFF requires byte-aligned samples");
        return 0;
    }

    uint64_t strips_per_plane =
        ((uint64_t)info->height + info->rows_per_strip - 1u) / info->rows_per_strip;
    uint64_t required = strips_per_plane * info->samples_per_pixel;
    if (info->strip_offsets_count < required) {
        cupidimage_set_err(err, errcap, "invalid number of planar strips");
        return 0;
    }

    size_t row_bytes_plane = 0;
    if (cupidimage_tiff_mul_overflow_size_t((size_t)info->width, bytes_per_sample,
                                            &row_bytes_plane)) {
        cupidimage_set_err(err, errcap, "invalid planar row size");
        return 0;
    }

    size_t max_strip = 0;
    if (cupidimage_tiff_mul_overflow_size_t(row_bytes_plane, (size_t)info->rows_per_strip,
                                            &max_strip)) {
        cupidimage_set_err(err, errcap, "invalid planar strip size");
        return 0;
    }
    uint8_t *plane_buf = (uint8_t *)malloc(max_strip);
    if (!plane_buf) {
        cupidimage_set_err(err, errcap, "out of memory");
        return 0;
    }

    for (uint16_t s = 0; s < info->samples_per_pixel; s++) {
        for (uint64_t strip = 0; strip < strips_per_plane; strip++) {
            uint64_t row_start = strip * (uint64_t)info->rows_per_strip;
            if (row_start >= info->height) {
                break;
            }
            uint32_t strip_rows = info->rows_per_strip;
            if (row_start + strip_rows > info->height) {
                strip_rows = info->height - (uint32_t)row_start;
            }
            size_t expected = 0;
            if (cupidimage_tiff_mul_overflow_size_t(row_bytes_plane, (size_t)strip_rows,
                                                    &expected)) {
                free(plane_buf);
                cupidimage_set_err(err, errcap, "invalid planar strip size");
                return 0;
            }

            uint64_t idx = (uint64_t)s * strips_per_plane + strip;
            uint64_t off = info->strip_offsets[idx];
            if (off >= ctx->size) {
                free(plane_buf);
                cupidimage_set_err(err, errcap, "TIFF strip offset out of range");
                return 0;
            }
            size_t src_size = expected;
            if (info->compression != TIFF_COMPRESSION_NONE) {
                if (!info->strip_byte_counts || idx >= info->strip_byte_counts_count) {
                    free(plane_buf);
                    cupidimage_set_err(err, errcap, "invalid StripByteCounts");
                    return 0;
                }
                if (info->strip_byte_counts[idx] == 0 ||
                    !cupidimage_tiff_range_valid(ctx, off, info->strip_byte_counts[idx])) {
                    free(plane_buf);
                    cupidimage_set_err(err, errcap, "invalid StripByteCounts");
                    return 0;
                }
                src_size = (size_t)info->strip_byte_counts[idx];
            } else if (info->strip_byte_counts && idx < info->strip_byte_counts_count) {
                if (info->strip_byte_counts[idx] < expected) {
                    free(plane_buf);
                    cupidimage_set_err(err, errcap, "truncated TIFF strip");
                    return 0;
                }
            }
            if (!cupidimage_tiff_range_valid(ctx, off, src_size)) {
                free(plane_buf);
                cupidimage_set_err(err, errcap, "truncated TIFF strip");
                return 0;
            }

            if (!tiff_decompress_block(ctx, info, ctx->data + (size_t)off, src_size, plane_buf,
                                       expected, info->width, strip_rows, err, errcap)) {
                free(plane_buf);
                return 0;
            }

            if (info->predictor == 2) {
                for (uint32_t r = 0; r < strip_rows; r++) {
                    if (!tiff_apply_predictor_row(ctx, plane_buf + (size_t)r * row_bytes_plane,
                                                  row_bytes_plane, 1, info->bits_per_sample[0])) {
                        free(plane_buf);
                        cupidimage_set_err(err, errcap, "TIFF predictor failed");
                        return 0;
                    }
                }
            }

            for (uint32_t r = 0; r < strip_rows; r++) {
                uint32_t y = (uint32_t)row_start + r;
                if (y >= info->height) {
                    break;
                }
                uint8_t *dst_row = raw + (size_t)y * row_bytes;
                const uint8_t *src_row = plane_buf + (size_t)r * row_bytes_plane;
                for (uint32_t x = 0; x < info->width; x++) {
                    size_t dst = (size_t)x * (size_t)info->samples_per_pixel * bytes_per_sample +
                                 (size_t)s * bytes_per_sample;
                    memcpy(dst_row + dst, src_row + (size_t)x * bytes_per_sample, bytes_per_sample);
                }
            }
        }
    }

    free(plane_buf);
    if (raw_size == 0) {
        return 0;
    }
    return 1;
}

static int tiff_decode_tiles_chunky(const tiff_context *ctx, const tiff_image_info *info,
                                    uint8_t *raw, size_t raw_size, size_t row_bytes,
                                    size_t bytes_per_pixel, char *err, size_t errcap) {
    (void)raw_size;
    if (info->tile_width == 0 || info->tile_length == 0) {
        cupidimage_set_err(err, errcap, "invalid TIFF tile size");
        return 0;
    }
    if ((info->bits_per_sample[0] & 7u) != 0) {
        cupidimage_set_err(err, errcap, "tiled TIFF requires byte-aligned samples");
        return 0;
    }

    uint64_t tiles_across = ((uint64_t)info->width + info->tile_width - 1u) / info->tile_width;
    uint64_t tiles_down = ((uint64_t)info->height + info->tile_length - 1u) / info->tile_length;
    uint64_t tiles_total = tiles_across * tiles_down;
    if (info->tile_offsets_count < tiles_total) {
        cupidimage_set_err(err, errcap, "invalid number of tiles");
        return 0;
    }

    size_t tile_row_bytes = 0;
    if (cupidimage_tiff_mul_overflow_size_t((size_t)info->tile_width, bytes_per_pixel,
                                            &tile_row_bytes)) {
        cupidimage_set_err(err, errcap, "invalid tile row size");
        return 0;
    }
    size_t tile_buf_size = 0;
    if (cupidimage_tiff_mul_overflow_size_t(tile_row_bytes, (size_t)info->tile_length,
                                            &tile_buf_size)) {
        cupidimage_set_err(err, errcap, "invalid tile buffer size");
        return 0;
    }

    uint8_t *tile_buf = (uint8_t *)malloc(tile_buf_size);
    if (!tile_buf) {
        cupidimage_set_err(err, errcap, "out of memory");
        return 0;
    }

    for (uint64_t t = 0; t < tiles_total; t++) {
        uint64_t off = info->tile_offsets[t];
        if (off >= ctx->size) {
            free(tile_buf);
            cupidimage_set_err(err, errcap, "TIFF tile offset out of range");
            return 0;
        }
        size_t src_size = tile_buf_size;
        if (info->compression != TIFF_COMPRESSION_NONE) {
            if (!info->tile_byte_counts || t >= info->tile_byte_counts_count) {
                free(tile_buf);
                cupidimage_set_err(err, errcap, "invalid TileByteCounts");
                return 0;
            }
            if (info->tile_byte_counts[t] == 0 ||
                !cupidimage_tiff_range_valid(ctx, off, info->tile_byte_counts[t])) {
                free(tile_buf);
                cupidimage_set_err(err, errcap, "invalid TileByteCounts");
                return 0;
            }
            src_size = (size_t)info->tile_byte_counts[t];
        } else if (info->tile_byte_counts && t < info->tile_byte_counts_count) {
            if (info->tile_byte_counts[t] < tile_buf_size) {
                free(tile_buf);
                cupidimage_set_err(err, errcap, "truncated TIFF tile");
                return 0;
            }
        }
        if (!cupidimage_tiff_range_valid(ctx, off, src_size)) {
            free(tile_buf);
            cupidimage_set_err(err, errcap, "truncated TIFF tile");
            return 0;
        }

        if (!tiff_decompress_block(ctx, info, ctx->data + (size_t)off, src_size, tile_buf,
                                   tile_buf_size, info->tile_width, info->tile_length, err,
                                   errcap)) {
            free(tile_buf);
            return 0;
        }

        if (info->predictor == 2) {
            for (uint32_t r = 0; r < info->tile_length; r++) {
                if (!tiff_apply_predictor_row(ctx, tile_buf + (size_t)r * tile_row_bytes,
                                              tile_row_bytes, info->samples_per_pixel,
                                              info->bits_per_sample[0])) {
                    free(tile_buf);
                    cupidimage_set_err(err, errcap, "TIFF predictor failed");
                    return 0;
                }
            }
        }

        uint32_t tile_x = (uint32_t)(t % tiles_across) * info->tile_width;
        uint32_t tile_y = (uint32_t)(t / tiles_across) * info->tile_length;
        uint32_t copy_w = info->tile_width;
        uint32_t copy_h = info->tile_length;
        if (tile_x + copy_w > info->width) {
            copy_w = info->width - tile_x;
        }
        if (tile_y + copy_h > info->height) {
            copy_h = info->height - tile_y;
        }

        for (uint32_t r = 0; r < copy_h; r++) {
            const uint8_t *src_row = tile_buf + (size_t)r * tile_row_bytes;
            uint8_t *dst_row =
                raw + (size_t)(tile_y + r) * row_bytes + (size_t)tile_x * bytes_per_pixel;
            memcpy(dst_row, src_row, (size_t)copy_w * bytes_per_pixel);
        }
    }

    free(tile_buf);
    if (raw_size == 0) {
        return 0;
    }
    return 1;
}

static int tiff_decode_tiles_planar(const tiff_context *ctx, const tiff_image_info *info,
                                    uint8_t *raw, size_t raw_size, size_t row_bytes,
                                    size_t bytes_per_sample, char *err, size_t errcap) {
    (void)raw_size;
    if (info->tile_width == 0 || info->tile_length == 0) {
        cupidimage_set_err(err, errcap, "invalid TIFF tile size");
        return 0;
    }
    if ((info->bits_per_sample[0] & 7u) != 0) {
        cupidimage_set_err(err, errcap, "tiled TIFF requires byte-aligned samples");
        return 0;
    }

    uint64_t tiles_across = ((uint64_t)info->width + info->tile_width - 1u) / info->tile_width;
    uint64_t tiles_down = ((uint64_t)info->height + info->tile_length - 1u) / info->tile_length;
    uint64_t tiles_per_plane = tiles_across * tiles_down;
    uint64_t required = tiles_per_plane * info->samples_per_pixel;
    if (info->tile_offsets_count < required) {
        cupidimage_set_err(err, errcap, "invalid number of tiles");
        return 0;
    }

    size_t tile_row_bytes = 0;
    if (cupidimage_tiff_mul_overflow_size_t((size_t)info->tile_width, bytes_per_sample,
                                            &tile_row_bytes)) {
        cupidimage_set_err(err, errcap, "invalid tile row size");
        return 0;
    }
    size_t tile_buf_size = 0;
    if (cupidimage_tiff_mul_overflow_size_t(tile_row_bytes, (size_t)info->tile_length,
                                            &tile_buf_size)) {
        cupidimage_set_err(err, errcap, "invalid tile buffer size");
        return 0;
    }

    uint8_t *tile_buf = (uint8_t *)malloc(tile_buf_size);
    if (!tile_buf) {
        cupidimage_set_err(err, errcap, "out of memory");
        return 0;
    }

    for (uint16_t s = 0; s < info->samples_per_pixel; s++) {
        for (uint64_t t = 0; t < tiles_per_plane; t++) {
            uint64_t idx = (uint64_t)s * tiles_per_plane + t;
            uint64_t off = info->tile_offsets[idx];
            if (off >= ctx->size) {
                free(tile_buf);
                cupidimage_set_err(err, errcap, "TIFF tile offset out of range");
                return 0;
            }
            size_t src_size = tile_buf_size;
            if (info->compression != TIFF_COMPRESSION_NONE) {
                if (!info->tile_byte_counts || idx >= info->tile_byte_counts_count) {
                    free(tile_buf);
                    cupidimage_set_err(err, errcap, "invalid TileByteCounts");
                    return 0;
                }
                if (info->tile_byte_counts[idx] == 0 ||
                    !cupidimage_tiff_range_valid(ctx, off, info->tile_byte_counts[idx])) {
                    free(tile_buf);
                    cupidimage_set_err(err, errcap, "invalid TileByteCounts");
                    return 0;
                }
                src_size = (size_t)info->tile_byte_counts[idx];
            } else if (info->tile_byte_counts && idx < info->tile_byte_counts_count) {
                if (info->tile_byte_counts[idx] < tile_buf_size) {
                    free(tile_buf);
                    cupidimage_set_err(err, errcap, "truncated TIFF tile");
                    return 0;
                }
            }
            if (!cupidimage_tiff_range_valid(ctx, off, src_size)) {
                free(tile_buf);
                cupidimage_set_err(err, errcap, "truncated TIFF tile");
                return 0;
            }

            if (!tiff_decompress_block(ctx, info, ctx->data + (size_t)off, src_size, tile_buf,
                                       tile_buf_size, info->tile_width, info->tile_length, err,
                                       errcap)) {
                free(tile_buf);
                return 0;
            }

            if (info->predictor == 2) {
                for (uint32_t r = 0; r < info->tile_length; r++) {
                    if (!tiff_apply_predictor_row(ctx, tile_buf + (size_t)r * tile_row_bytes,
                                                  tile_row_bytes, 1, info->bits_per_sample[0])) {
                        free(tile_buf);
                        cupidimage_set_err(err, errcap, "TIFF predictor failed");
                        return 0;
                    }
                }
            }

            uint32_t tile_x = (uint32_t)(t % tiles_across) * info->tile_width;
            uint32_t tile_y = (uint32_t)(t / tiles_across) * info->tile_length;
            uint32_t copy_w = info->tile_width;
            uint32_t copy_h = info->tile_length;
            if (tile_x + copy_w > info->width) {
                copy_w = info->width - tile_x;
            }
            if (tile_y + copy_h > info->height) {
                copy_h = info->height - tile_y;
            }

            for (uint32_t r = 0; r < copy_h; r++) {
                const uint8_t *src_row = tile_buf + (size_t)r * tile_row_bytes;
                uint8_t *dst_row = raw + (size_t)(tile_y + r) * row_bytes;
                for (uint32_t x = 0; x < copy_w; x++) {
                    size_t dst =
                        (size_t)(tile_x + x) * (size_t)info->samples_per_pixel * bytes_per_sample +
                        (size_t)s * bytes_per_sample;
                    memcpy(dst_row + dst, src_row + (size_t)x * bytes_per_sample, bytes_per_sample);
                }
            }
        }
    }

    free(tile_buf);
    if (raw_size == 0) {
        return 0;
    }
    return 1;
}

int cupidimage_tiff_decode_image(const tiff_context *ctx, const tiff_image_info *info, uint8_t *raw,
                                 size_t raw_size, size_t row_bytes, size_t bytes_per_pixel,
                                 char *err, size_t errcap) {
    if (info->tile_offsets && info->tile_offsets_count > 0) {
        if (info->planar_config == TIFF_PLANAR_CHUNKY) {
            return tiff_decode_tiles_chunky(ctx, info, raw, raw_size, row_bytes, bytes_per_pixel,
                                            err, errcap);
        }
        return tiff_decode_tiles_planar(ctx, info, raw, raw_size, row_bytes,
                                        bytes_per_pixel / info->samples_per_pixel, err, errcap);
    }

    if (info->planar_config == TIFF_PLANAR_CHUNKY) {
        return tiff_decode_strips_chunky(ctx, info, raw, raw_size, row_bytes, err, errcap);
    }
    return tiff_decode_strips_planar(ctx, info, raw, raw_size, row_bytes,
                                     bytes_per_pixel / info->samples_per_pixel, err, errcap);
}

static int tiff_build_jpeg_stream(const uint8_t *tables, size_t tables_size, const uint8_t *data,
                                  size_t data_size, uint8_t **out, size_t *out_size, char *err,
                                  size_t errcap) {
    int tables_has_soi = tables_size >= 2 && tables[0] == 0xFF && tables[1] == 0xD8;
    int tables_has_eoi =
        tables_size >= 2 && tables[tables_size - 2] == 0xFF && tables[tables_size - 1] == 0xD9;
    size_t tables_start = tables_has_soi ? 2u : 0u;
    size_t tables_end = tables_has_eoi ? (tables_size - 2u) : tables_size;

    int data_has_soi = data_size >= 2 && data[0] == 0xFF && data[1] == 0xD8;
    int data_has_eoi = data_size >= 2 && data[data_size - 2] == 0xFF && data[data_size - 1] == 0xD9;
    size_t data_start = data_has_soi ? 2u : 0u;
    size_t data_end = data_size;

    size_t total =
        2u + (tables_end - tables_start) + (data_end - data_start) + (data_has_eoi ? 0u : 2u);
    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) {
        cupidimage_set_err(err, errcap, "out of memory");
        return 0;
    }

    size_t pos = 0;
    buf[pos++] = 0xFF;
    buf[pos++] = 0xD8;
    if (tables_end > tables_start) {
        memcpy(buf + pos, tables + tables_start, tables_end - tables_start);
        pos += tables_end - tables_start;
    }
    if (data_end > data_start) {
        memcpy(buf + pos, data + data_start, data_end - data_start);
        pos += data_end - data_start;
    }
    if (!data_has_eoi) {
        buf[pos++] = 0xFF;
        buf[pos++] = 0xD9;
    }

    *out = buf;
    *out_size = pos;
    return 1;
}

static int tiff_decode_jpeg_stream(const unsigned char *data, size_t size, uint32_t expected_w,
                                   uint32_t expected_h, uint8_t *rgba, size_t rgba_size, char *err,
                                   size_t errcap) {
    cupidimage_image img;
    if (!cupidimage_load_jpeg(data, size, &img, err, errcap)) {
        return 0;
    }
    if (img.width != expected_w || img.height != expected_h) {
        cupidimage_free(&img);
        cupidimage_set_err(err, errcap, "JPEG strip dimensions mismatch");
        return 0;
    }
    size_t need = (size_t)img.width * (size_t)img.height * 4u;
    if (need > rgba_size) {
        cupidimage_free(&img);
        cupidimage_set_err(err, errcap, "invalid RGBA buffer");
        return 0;
    }
    memcpy(rgba, img.rgba, need);
    cupidimage_free(&img);
    return 1;
}

int cupidimage_tiff_decode_jpeg_image(const tiff_context *ctx, const tiff_image_info *info,
                                      uint8_t **out_rgba, uint32_t *out_width, uint32_t *out_height,
                                      char *err, size_t errcap) {
    if (info->tile_offsets && info->tile_offsets_count > 0) {
        cupidimage_set_err(err, errcap, "JPEG-in-TIFF tiles not supported");
        return 0;
    }

    if (info->jpeg_if_offset && info->jpeg_if_bytecount) {
        if (!cupidimage_tiff_range_valid(ctx, info->jpeg_if_offset, info->jpeg_if_bytecount)) {
            cupidimage_set_err(err, errcap, "invalid JPEG interchange data");
            return 0;
        }
        const unsigned char *jpeg_data = ctx->data + (size_t)info->jpeg_if_offset;
        size_t jpeg_size = (size_t)info->jpeg_if_bytecount;
        cupidimage_image img;
        if (!cupidimage_load_jpeg(jpeg_data, jpeg_size, &img, err, errcap)) {
            return 0;
        }
        *out_rgba = img.rgba;
        *out_width = img.width;
        *out_height = img.height;
        return 1;
    }

    uint64_t strips_expected =
        ((uint64_t)info->height + info->rows_per_strip - 1u) / info->rows_per_strip;
    if (info->strip_offsets_count < strips_expected) {
        cupidimage_set_err(err, errcap, "invalid number of strips");
        return 0;
    }
    if (!info->strip_byte_counts || info->strip_byte_counts_count < strips_expected) {
        cupidimage_set_err(err, errcap, "missing StripByteCounts");
        return 0;
    }

    size_t rgba_size = (size_t)info->width * (size_t)info->height * 4u;
    uint8_t *rgba = (uint8_t *)malloc(rgba_size);
    if (!rgba) {
        cupidimage_set_err(err, errcap, "out of memory");
        return 0;
    }

    for (uint64_t i = 0; i < strips_expected; i++) {
        uint64_t row_start = i * (uint64_t)info->rows_per_strip;
        if (row_start >= info->height) {
            break;
        }
        uint32_t strip_rows = info->rows_per_strip;
        if (row_start + strip_rows > info->height) {
            strip_rows = info->height - (uint32_t)row_start;
        }

        uint64_t off = info->strip_offsets[i];
        uint64_t count = info->strip_byte_counts[i];
        if (count == 0 || !cupidimage_tiff_range_valid(ctx, off, count)) {
            free(rgba);
            cupidimage_set_err(err, errcap, "truncated JPEG strip");
            return 0;
        }
        const uint8_t *src = ctx->data + (size_t)off;
        size_t src_size = (size_t)count;

        uint8_t *stream = NULL;
        size_t stream_size = 0;
        if (!tiff_build_jpeg_stream(info->jpeg_tables, info->jpeg_tables_size, src, src_size,
                                    &stream, &stream_size, err, errcap)) {
            free(rgba);
            return 0;
        }

        size_t strip_rgba_size = (size_t)info->width * (size_t)strip_rows * 4u;
        uint8_t *strip_rgba = rgba + (size_t)row_start * (size_t)info->width * 4u;
        if (!tiff_decode_jpeg_stream(stream, stream_size, info->width, strip_rows, strip_rgba,
                                     strip_rgba_size, err, errcap)) {
            free(stream);
            free(rgba);
            return 0;
        }
        free(stream);
    }

    *out_rgba = rgba;
    *out_width = info->width;
    *out_height = info->height;
    return 1;
}

int cupidimage_tiff_decode_ycbcr_subsampled(const tiff_context *ctx, const tiff_image_info *info,
                                            uint8_t *rgba, size_t rgba_size, char *err,
                                            size_t errcap) {
    if (info->planar_config != TIFF_PLANAR_CHUNKY) {
        cupidimage_set_err(err, errcap, "YCbCr planar TIFF not supported");
        return 0;
    }
    if (info->samples_per_pixel < 3) {
        cupidimage_set_err(err, errcap, "invalid YCbCr SamplesPerPixel");
        return 0;
    }
    if (info->predictor == 2) {
        cupidimage_set_err(err, errcap, "YCbCr predictor not supported");
        return 0;
    }
    if (info->tile_offsets && info->tile_offsets_count > 0) {
        cupidimage_set_err(err, errcap, "YCbCr tiled TIFF not supported");
        return 0;
    }
    if (info->bits_per_sample[0] != 8) {
        cupidimage_set_err(err, errcap, "YCbCr subsampling requires 8-bit samples");
        return 0;
    }
    uint32_t hsub = info->ycbcr_subsampling[0];
    uint32_t vsub = info->ycbcr_subsampling[1];
    if (hsub == 0 || vsub == 0) {
        cupidimage_set_err(err, errcap, "invalid YCbCr subsampling");
        return 0;
    }

    size_t expected_rgba = (size_t)info->width * (size_t)info->height * 4u;
    if (rgba_size < expected_rgba) {
        cupidimage_set_err(err, errcap, "invalid RGBA buffer");
        return 0;
    }

    uint64_t strips_expected =
        ((uint64_t)info->height + info->rows_per_strip - 1u) / info->rows_per_strip;
    if (info->strip_offsets_count < strips_expected) {
        cupidimage_set_err(err, errcap, "invalid number of strips");
        return 0;
    }

    for (uint64_t i = 0; i < strips_expected; i++) {
        uint64_t row_start = i * (uint64_t)info->rows_per_strip;
        if (row_start >= info->height) {
            break;
        }
        uint32_t strip_rows = info->rows_per_strip;
        if (row_start + strip_rows > info->height) {
            strip_rows = info->height - (uint32_t)row_start;
        }

        uint64_t blocks_across = (info->width + hsub - 1u) / hsub;
        uint64_t blocks_down = (strip_rows + vsub - 1u) / vsub;
        size_t block_samples = (size_t)hsub * (size_t)vsub + 2u;
        size_t expected = 0;
        if (cupidimage_tiff_mul_overflow_size_t((size_t)blocks_across, (size_t)blocks_down,
                                                &expected) ||
            cupidimage_tiff_mul_overflow_size_t(expected, block_samples, &expected)) {
            cupidimage_set_err(err, errcap, "invalid YCbCr strip size");
            return 0;
        }

        uint64_t off = info->strip_offsets[i];
        if (off >= ctx->size) {
            cupidimage_set_err(err, errcap, "TIFF strip offset out of range");
            return 0;
        }
        size_t src_size = expected;
        if (info->compression != TIFF_COMPRESSION_NONE) {
            if (!info->strip_byte_counts || i >= info->strip_byte_counts_count) {
                cupidimage_set_err(err, errcap, "invalid StripByteCounts");
                return 0;
            }
            if (info->strip_byte_counts[i] == 0 ||
                !cupidimage_tiff_range_valid(ctx, off, info->strip_byte_counts[i])) {
                cupidimage_set_err(err, errcap, "invalid StripByteCounts");
                return 0;
            }
            src_size = (size_t)info->strip_byte_counts[i];
        } else if (info->strip_byte_counts && i < info->strip_byte_counts_count) {
            if (info->strip_byte_counts[i] < expected) {
                cupidimage_set_err(err, errcap, "truncated TIFF strip");
                return 0;
            }
        }
        if (!cupidimage_tiff_range_valid(ctx, off, src_size)) {
            cupidimage_set_err(err, errcap, "truncated TIFF strip");
            return 0;
        }

        uint8_t *buf = (uint8_t *)malloc(expected);
        if (!buf) {
            cupidimage_set_err(err, errcap, "out of memory");
            return 0;
        }
        if (!tiff_decompress_block(ctx, info, ctx->data + (size_t)off, src_size, buf, expected,
                                   info->width, strip_rows, err, errcap)) {
            free(buf);
            return 0;
        }

        size_t pos = 0;
        for (uint64_t by = 0; by < blocks_down; by++) {
            for (uint64_t bx = 0; bx < blocks_across; bx++) {
                uint8_t yblock[16];
                size_t ycount = (size_t)hsub * (size_t)vsub;
                if (ycount > sizeof(yblock)) {
                    free(buf);
                    cupidimage_set_err(err, errcap, "unsupported YCbCr subsampling");
                    return 0;
                }
                for (size_t k = 0; k < ycount; k++) {
                    if (pos >= expected) {
                        free(buf);
                        cupidimage_set_err(err, errcap, "truncated YCbCr data");
                        return 0;
                    }
                    yblock[k] = buf[pos++];
                }
                if (pos + 1 >= expected) {
                    free(buf);
                    cupidimage_set_err(err, errcap, "truncated YCbCr data");
                    return 0;
                }
                uint8_t cb = buf[pos++];
                uint8_t cr = buf[pos++];
                int cbi = (int)cb - 128;
                int cri = (int)cr - 128;
                double cbi_d = (double)cbi;
                double cri_d = (double)cri;
                for (uint32_t ry = 0; ry < vsub; ry++) {
                    uint32_t y = (uint32_t)row_start + (uint32_t)by * vsub + ry;
                    if (y >= info->height) {
                        continue;
                    }
                    for (uint32_t rx = 0; rx < hsub; rx++) {
                        uint32_t x = (uint32_t)bx * hsub + rx;
                        if (x >= info->width) {
                            continue;
                        }
                        uint8_t yv = yblock[(size_t)ry * hsub + rx];
                        int ri = (int)((double)yv + (1.402 * cri_d));
                        int gi = (int)((double)yv - (0.344136 * cbi_d) - (0.714136 * cri_d));
                        int bi = (int)((double)yv + (1.772 * cbi_d));
                        size_t dst = ((size_t)y * (size_t)info->width + x) * 4u;
                        rgba[dst + 0] = cupidimage_tiff_clamp8(ri);
                        rgba[dst + 1] = cupidimage_tiff_clamp8(gi);
                        rgba[dst + 2] = cupidimage_tiff_clamp8(bi);
                        rgba[dst + 3] = 255;
                    }
                }
            }
        }

        free(buf);
    }

    return 1;
}
