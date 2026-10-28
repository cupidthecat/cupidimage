#ifndef CUPIDIMAGE_JPEG_INTERNAL_H
#define CUPIDIMAGE_JPEG_INTERNAL_H

#include "cupidimage.h"
#include <stdio.h>

#ifdef CUPIDIMAGE_JPEG_DEBUG
#define JPEG_DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define JPEG_DEBUG(...)                                                                            \
    do {                                                                                           \
    } while (0)
#endif

static inline void cupidimage_jpeg_set_error(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg);
    }
}

typedef struct jpeg_huff {
    uint8_t bits[16];
    uint8_t huffval[256];
    int mincode[17];
    int maxcode[18];
    int valptr[17];
    int num;
    int valid;
} jpeg_huff;

typedef struct jpeg_comp {
    int id;
    int h;
    int v;
    int tq;
    int dc_table;
    int ac_table;
    int dc_pred;
    int blocks_w;
    int blocks_h;
    int32_t *coeffs;
    int quantizers[64];
    int have_quantizers;
} jpeg_comp;

typedef struct jpeg_bitstream {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t bitbuf;
    int bitcount;
    int marker;
} jpeg_bitstream;

typedef struct {
    int width, height, components, transform;
    int *marker;
} jpeg_sample_profile;

typedef struct {
    uint8_t lower[4], upper[4], ac[4];
} jpeg_arithmetic_conditioning;

int cupidimage_jpeg_decode_arithmetic_scan(const uint8_t *data, size_t size, jpeg_comp *components,
                                           int count, const int *scan, int scan_count, int ss,
                                           int se, int ah, int al, int width, int height,
                                           int columns, int rows, int hmax, int vmax,
                                           const jpeg_arithmetic_conditioning *conditioning,
                                           int restart_interval, size_t *consumed, char *error,
                                           size_t capacity, int sequential);

extern const uint8_t cupidimage_jpeg_zigzag[64];

int cupidimage_jpeg_huff_build(jpeg_huff *h, const uint8_t *bits, const uint8_t *vals, int num);
void cupidimage_jpeg_default_huffman(jpeg_huff dc_tables[4], jpeg_huff ac_tables[4]);
int cupidimage_jpeg_decode_block(jpeg_bitstream *bs, const jpeg_huff *dc, const jpeg_huff *ac,
                                 const int *qt, int *dc_pred, int32_t *block);
int cupidimage_jpeg_consume_restart(jpeg_bitstream *bs, int expected);
int cupidimage_jpeg_decode_coefficient_scan(
    const uint8_t *data, size_t size, jpeg_comp *comps, int num_comp, const int *scan_comp,
    int scan_count, int ss, int se, int ah, int al, int width, int height, int mcu_cols,
    int mcu_rows, int hmax, int vmax, const jpeg_huff dc_tables[4], const jpeg_huff ac_tables[4],
    int restart_interval, size_t *consumed, char *err, size_t errcap, int sequential);
int cupidimage_jpeg_dequantize(int32_t coefficient, int quantizer, int32_t *result);
int cupidimage_jpeg_finish_coefficients(jpeg_comp *comps, int num_comp, int width, int height,
                                        int mcu_cols, int mcu_rows, int hmax, int vmax,
                                        int adobe_transform, cupidimage_image *out, char *err,
                                        size_t errcap, const jpeg_sample_profile *profile);
int cupidimage_jpeg_decode_scan(const uint8_t *data, size_t size, jpeg_comp *comps, int num_comp,
                                const int *scan_comp, int width, int height, int hmax, int vmax,
                                int qtables[4][64], const jpeg_huff dc_tables[4],
                                const jpeg_huff ac_tables[4], int restart_interval,
                                int adobe_transform, size_t *consumed, cupidimage_image *out,
                                char *err, size_t errcap, const jpeg_sample_profile *profile);
void cupidimage_jpeg_convert_native(cupidimage_image *image, const jpeg_sample_profile *profile);

#endif
