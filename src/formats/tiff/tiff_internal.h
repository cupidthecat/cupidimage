#ifndef CUPIDIMAGE_TIFF_INTERNAL_H
#define CUPIDIMAGE_TIFF_INTERNAL_H

#include "core/internal.h"
#include "cupidimage.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CUPIDIMAGE_TIFF_DEBUG
#define TIFF_DEBUG(...) fprintf(stderr, "TIFF: " __VA_ARGS__)
#else
#define TIFF_DEBUG(...) ((void)0)
#endif

#define TIFF_TAG_IMAGE_WIDTH 256
#define TIFF_TAG_IMAGE_LENGTH 257
#define TIFF_TAG_BITS_PER_SAMPLE 258
#define TIFF_TAG_COMPRESSION 259
#define TIFF_TAG_PHOTOMETRIC 262
#define TIFF_TAG_FILL_ORDER 266
#define TIFF_TAG_STRIP_OFFSETS 273
#define TIFF_TAG_ORIENTATION 274
#define TIFF_TAG_SAMPLES_PER_PIXEL 277
#define TIFF_TAG_ROWS_PER_STRIP 278
#define TIFF_TAG_STRIP_BYTE_COUNTS 279
#define TIFF_TAG_PLANAR_CONFIGURATION 284
#define TIFF_TAG_GROUP3_OPTIONS 292
#define TIFF_TAG_GROUP4_OPTIONS 293
#define TIFF_TAG_PREDICTOR 317
#define TIFF_TAG_COLOR_MAP 320
#define TIFF_TAG_SUB_IFD 330
#define TIFF_TAG_TILE_WIDTH 322
#define TIFF_TAG_TILE_LENGTH 323
#define TIFF_TAG_TILE_OFFSETS 324
#define TIFF_TAG_TILE_BYTE_COUNTS 325
#define TIFF_TAG_EXTRA_SAMPLES 338
#define TIFF_TAG_SAMPLE_FORMAT 339
#define TIFF_TAG_JPEG_TABLES 347
#define TIFF_TAG_JPEG_INTERCHANGE_FORMAT 513
#define TIFF_TAG_JPEG_INTERCHANGE_FORMAT_LENGTH 514
#define TIFF_TAG_YCBCR_COEFFICIENTS 529
#define TIFF_TAG_YCBCR_SUBSAMPLING 530
#define TIFF_TAG_YCBCR_POSITIONING 531
#define TIFF_TAG_REFERENCE_BLACK_WHITE 532

#define TIFF_COMPRESSION_NONE 1
#define TIFF_COMPRESSION_CCITT_RLE 2
#define TIFF_COMPRESSION_CCITT_G3 3
#define TIFF_COMPRESSION_CCITT_G4 4
#define TIFF_COMPRESSION_LZW 5
#define TIFF_COMPRESSION_JPEG 7
#define TIFF_COMPRESSION_DEFLATE 8
#define TIFF_COMPRESSION_ADOBE_DEFLATE 32946
#define TIFF_COMPRESSION_PACKBITS 32773

#define TIFF_PHOTOMETRIC_WHITE_IS_ZERO 0
#define TIFF_PHOTOMETRIC_BLACK_IS_ZERO 1
#define TIFF_PHOTOMETRIC_RGB 2
#define TIFF_PHOTOMETRIC_PALETTE 3
#define TIFF_PHOTOMETRIC_CMYK 5
#define TIFF_PHOTOMETRIC_YCBCR 6
#define TIFF_PHOTOMETRIC_CIELAB 8

#define TIFF_PLANAR_CHUNKY 1
#define TIFF_PLANAR_SEPARATE 2

#define TIFF_EXTRA_SAMPLE_ASSOC_ALPHA 1
#define TIFF_EXTRA_SAMPLE_UNASSOC_ALPHA 2

#define TIFF_SAMPLE_FORMAT_UINT 1

#define TIFF_FILL_ORDER_MSB2LSB 1
#define TIFF_FILL_ORDER_LSB2MSB 2

typedef struct tiff_context {
    const uint8_t *data;
    size_t size;
    int big_endian;
    int big_tiff;
    uint64_t first_ifd;
    size_t entry_size;
    size_t value_size;
} tiff_context;

typedef struct tiff_entry {
    uint16_t tag;
    uint16_t type;
    uint64_t count;
    const uint8_t *value_ptr;
    size_t value_size;
} tiff_entry;

typedef struct tiff_image_info {
    uint32_t width;
    uint32_t height;
    uint16_t bits_per_sample[8];
    uint16_t bits_count;
    uint16_t samples_per_pixel;
    uint16_t compression;
    uint16_t photometric;
    uint16_t planar_config;
    uint32_t rows_per_strip;
    uint16_t fill_order;
    uint16_t predictor;
    uint16_t sample_format;
    uint16_t orientation;
    uint16_t ycbcr_subsampling[2];
    uint16_t ycbcr_positioning;
    uint32_t group3_options;
    uint32_t group4_options;
    uint16_t extra_samples[8];
    uint16_t extra_count;
    uint64_t *strip_offsets;
    uint64_t *strip_byte_counts;
    uint64_t strip_offsets_count;
    uint64_t strip_byte_counts_count;
    uint64_t *tile_offsets;
    uint64_t *tile_byte_counts;
    uint64_t tile_offsets_count;
    uint64_t tile_byte_counts_count;
    uint32_t tile_width;
    uint32_t tile_length;
    uint16_t *color_map;
    uint64_t color_map_count;
    const uint8_t *jpeg_tables;
    size_t jpeg_tables_size;
    uint64_t jpeg_if_offset;
    uint64_t jpeg_if_bytecount;
} tiff_image_info;

int cupidimage_tiff_mul_overflow_size_t(size_t a, size_t b, size_t *out);
uint16_t cupidimage_tiff_read_u16(const tiff_context *ctx, const uint8_t *p);
uint32_t cupidimage_tiff_read_u32(const tiff_context *ctx, const uint8_t *p);
uint64_t cupidimage_tiff_read_u64(const tiff_context *ctx, const uint8_t *p);
uint8_t cupidimage_tiff_sample16_to_8(uint16_t v);
int cupidimage_tiff_apply_orientation_rgba(uint8_t **rgba, uint32_t *width, uint32_t *height,
                                           uint16_t orientation, char *err, size_t errcap);
int cupidimage_tiff_apply_predictor(const tiff_context *ctx, const tiff_image_info *info,
                                    uint8_t *raw, size_t row_bytes, char *err, size_t errcap);
int cupidimage_tiff_ccitt_decode(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size,
                                 uint32_t width, uint32_t height, uint16_t compression,
                                 uint32_t group3_options, int fill_order, char *err, size_t errcap);
uint8_t cupidimage_tiff_clamp8(int v);
int cupidimage_tiff_collect_subifds(const tiff_context *ctx, uint64_t ifd_offset, uint64_t **list,
                                    size_t *count, size_t *cap, char *err, size_t errcap);
int cupidimage_tiff_convert_to_rgba(const tiff_context *ctx, const tiff_image_info *info,
                                    const uint8_t *raw, size_t row_bytes, uint8_t *rgba,
                                    size_t rgba_size, char *err, size_t errcap);
int cupidimage_tiff_decode_image(const tiff_context *ctx, const tiff_image_info *info, uint8_t *raw,
                                 size_t raw_size, size_t row_bytes, size_t bytes_per_pixel,
                                 char *err, size_t errcap);
int cupidimage_tiff_decode_jpeg_image(const tiff_context *ctx, const tiff_image_info *info,
                                      uint8_t **out_rgba, uint32_t *out_width, uint32_t *out_height,
                                      char *err, size_t errcap);
int cupidimage_tiff_decode_ycbcr_subsampled(const tiff_context *ctx, const tiff_image_info *info,
                                            uint8_t *rgba, size_t rgba_size, char *err,
                                            size_t errcap);
int cupidimage_tiff_ifd_list_append(uint64_t **list, size_t *count, size_t *cap, uint64_t off);
void cupidimage_tiff_info_free(tiff_image_info *info);
void cupidimage_tiff_info_init(tiff_image_info *info);
int cupidimage_tiff_lzw_decode(const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_size,
                               char *err, size_t errcap);
int cupidimage_tiff_packbits_decode(const uint8_t *src, size_t src_size, uint8_t *dst,
                                    size_t dst_size, char *err, size_t errcap);
int cupidimage_tiff_parse_header(const unsigned char *data, size_t size, tiff_context *ctx,
                                 char *err, size_t errcap);
int cupidimage_tiff_parse_image_info(const tiff_context *ctx, uint64_t ifd_offset,
                                     tiff_image_info *info, char *err, size_t errcap);
int cupidimage_tiff_range_valid(const tiff_context *ctx, uint64_t offset, uint64_t length);
int cupidimage_tiff_read_ifd_header(const tiff_context *ctx, uint64_t offset, uint64_t *entry_count,
                                    uint64_t *next_ifd, char *err, size_t errcap);
uint32_t cupidimage_tiff_read_sample(const tiff_context *ctx, const uint8_t *row, uint32_t index,
                                     uint16_t bps, uint16_t fill_order);
double cupidimage_tiff_read_sample_float(const tiff_context *ctx, const uint8_t *row,
                                         uint32_t index, uint16_t bps);
uint8_t cupidimage_tiff_scale_float(double v);
uint8_t cupidimage_tiff_scale_sample(uint32_t v, uint16_t bps);
void cupidimage_tiff_write_u16(const tiff_context *ctx, uint8_t *p, uint16_t v);

#endif
