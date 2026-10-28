#include "jpeg_internal.h"
#include <stdlib.h>
#include <string.h>

#define IDCT_BITS 14
#define IDCT_SHIFT (IDCT_BITS * 2 + 2)

static const int16_t idct_tab[8][8] = {{11585, 11585, 11585, 11585, 11585, 11585, 11585, 11585},
                                       {16069, 13623, 9102, 3196, -3196, -9102, -13623, -16069},
                                       {15137, 6270, -6270, -15137, -15137, -6270, 6270, 15137},
                                       {13623, -3196, -16069, -9102, 9102, 16069, 3196, -13623},
                                       {11585, -11585, -11585, 11585, 11585, -11585, -11585, 11585},
                                       {9102, -16069, 3196, 13623, -13623, -3196, 16069, -9102},
                                       {6270, -15137, 15137, -6270, -6270, 15137, -15137, 6270},
                                       {3196, -9102, 13623, -16069, 16069, -13623, 9102, -3196}};

/* Bound every term so all 64 fixed-point IDCT products and rounding fit int64_t. */
#define JPEG_DEQUANT_LIMIT ((INT64_C(1) << 29) - 1)

int cupidimage_jpeg_dequantize(int32_t coefficient, int quantizer, int32_t *result) {
    int64_t value = (int64_t)coefficient * quantizer;
    if (value < -JPEG_DEQUANT_LIMIT || value > JPEG_DEQUANT_LIMIT)
        return 0;
    *result = (int32_t)value;
    return 1;
}

static void idct_8x8(const int32_t *block, uint8_t *out, int stride) {
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int64_t sum = 0;
            for (int v = 0; v < 8; v++) {
                const int16_t tv = idct_tab[v][y];
                for (int u = 0; u < 8; u++) {
                    sum += (int64_t)block[v * 8 + u] * (int64_t)idct_tab[u][x] * (int64_t)tv;
                }
            }
            int64_t val = (sum + ((int64_t)1 << (IDCT_SHIFT - 1))) >> IDCT_SHIFT;
            val += 128;
            if (val < 0)
                val = 0;
            if (val > 255)
                val = 255;
            out[y * stride + x] = (uint8_t)val;
        }
    }
}

static uint8_t jpeg_sample_clamp(int value) {
    return (uint8_t)(value < 0 ? 0 : value > 255 ? 255 : value);
}

/* DCT stream samples remain in their native component space for the caller's
 * Decode array and color-space conversion. Component order follows the frame. */
static void jpeg_native_pixel(uint8_t *destination, uint8_t buffers[4][32 * 32],
                              const int widths[4], const int heights[4], int components, int x,
                              int y, int mcu_width, int mcu_height) {
    for (int i = 0; i < components; i++) {
        int sx = x * widths[i] / mcu_width, sy = y * heights[i] / mcu_height;
        destination[i] = buffers[i][sy * widths[i] + sx];
    }
}

static void jpeg_native_color(uint8_t *destination, int components) {
    int yy = destination[0], cb = (int)destination[1] - 128, cr = (int)destination[2] - 128;
    uint8_t rgb[3] = {jpeg_sample_clamp(yy + ((91881 * cr) >> 16)),
                      jpeg_sample_clamp(yy - ((22554 * cb + 46802 * cr) >> 16)),
                      jpeg_sample_clamp(yy + ((116130 * cb) >> 16))};
    for (int i = 0; i < 3; i++)
        destination[i] = components == 4 ? (uint8_t)(255 - rgb[i]) : rgb[i];
}

static int jpeg_sample_transform(const jpeg_sample_profile *profile, int marker) {
    return marker >= 0               ? marker != 0
           : profile->transform >= 0 ? profile->transform
                                     : profile->components == 3;
}

int cupidimage_jpeg_finish_coefficients(jpeg_comp *comps, int num_comp, int width, int height,
                                        int mcu_cols, int mcu_rows, int hmax, int vmax,
                                        int adobe_transform, cupidimage_image *out, char *err,
                                        size_t errcap, const jpeg_sample_profile *profile) {
    unsigned channels = profile ? (unsigned)num_comp : 4u;
    size_t rgba_size = (size_t)width * (size_t)height * channels;
    if (rgba_size / channels != (size_t)width * (size_t)height) {
        cupidimage_jpeg_set_error(err, errcap, "image too large");
        return 0;
    }

    uint8_t *rgba = (uint8_t *)malloc(rgba_size);
    if (!rgba) {
        cupidimage_jpeg_set_error(err, errcap, "out of memory");
        return 0;
    }

    int mcu_w = hmax * 8;
    int mcu_h = vmax * 8;

    uint8_t comp_buf[4][32 * 32];
    int comp_w[4] = {0, 0, 0, 0};
    int comp_h[4] = {0, 0, 0, 0};
    for (int i = 0; i < num_comp; i++) {
        comp_w[i] = comps[i].h * 8;
        comp_h[i] = comps[i].v * 8;
    }

    int idx_y = 0;
    int idx_cb = (num_comp > 1) ? 1 : 0;
    int idx_cr = (num_comp > 2) ? 2 : 0;
    int idx_k = (num_comp > 3) ? 3 : 0;
    for (int i = 0; i < num_comp; i++) {
        if (comps[i].id == 1)
            idx_y = i;
        if (comps[i].id == 2)
            idx_cb = i;
        if (comps[i].id == 3)
            idx_cr = i;
        if (comps[i].id == 4)
            idx_k = i;
    }

    int32_t deq[64];
    uint8_t block_out[64];

    for (int my = 0; my < mcu_rows; my++) {
        for (int mx = 0; mx < mcu_cols; mx++) {
            for (int ci = 0; ci < num_comp; ci++) {
                jpeg_comp *c = &comps[ci];
                int cw = comp_w[ci];
                for (int by = 0; by < c->v; by++) {
                    for (int bx = 0; bx < c->h; bx++) {
                        int block_x = mx * c->h + bx;
                        int block_y = my * c->v + by;
                        int32_t *block = c->coeffs + ((block_y * c->blocks_w + block_x) * 64);
                        for (int i = 0; i < 64; i++) {
                            if (!cupidimage_jpeg_dequantize(block[i], c->quantizers[i], &deq[i])) {
                                free(rgba);
                                cupidimage_jpeg_set_error(
                                    err, errcap, "JPEG coefficient exceeds transform bounds");
                                return 0;
                            }
                        }
                        idct_8x8(deq, block_out, 8);
                        for (int y = 0; y < 8; y++) {
                            memcpy(&comp_buf[ci][(by * 8 + y) * cw + bx * 8], &block_out[y * 8], 8);
                        }
                    }
                }
            }

            for (int y = 0; y < mcu_h; y++) {
                int out_y = my * mcu_h + y;
                if (out_y >= height) {
                    continue;
                }
                for (int x = 0; x < mcu_w; x++) {
                    int out_x = mx * mcu_w + x;
                    if (out_x >= width) {
                        continue;
                    }

                    if (profile) {
                        size_t dst = ((size_t)out_y * width + out_x) * channels;
                        jpeg_native_pixel(rgba + dst, comp_buf, comp_w, comp_h, num_comp, x, y,
                                          mcu_w, mcu_h);
                        continue;
                    }
                    uint8_t r, g, b;
                    if (num_comp == 1) {
                        int yw = comp_w[idx_y];
                        int yh = comp_h[idx_y];
                        int yy = y * yh / mcu_h;
                        int xx = x * yw / mcu_w;
                        uint8_t yv = comp_buf[idx_y][yy * yw + xx];
                        r = yv;
                        g = yv;
                        b = yv;
                    } else if (num_comp == 3) {
                        int yw = comp_w[idx_y];
                        int yh = comp_h[idx_y];
                        int cbw = comp_w[idx_cb];
                        int cbh = comp_h[idx_cb];
                        int crw = comp_w[idx_cr];
                        int crh = comp_h[idx_cr];

                        int yy = y * yh / mcu_h;
                        int yx = x * yw / mcu_w;
                        int cby = y * cbh / mcu_h;
                        int cbx = x * cbw / mcu_w;
                        int cry = y * crh / mcu_h;
                        int crx = x * crw / mcu_w;

                        int Y = comp_buf[idx_y][yy * yw + yx];
                        int Cb = comp_buf[idx_cb][cby * cbw + cbx] - 128;
                        int Cr = comp_buf[idx_cr][cry * crw + crx] - 128;

                        int rtmp = Y + ((91881 * Cr) >> 16);
                        int gtmp = Y - ((22554 * Cb + 46802 * Cr) >> 16);
                        int btmp = Y + ((116130 * Cb) >> 16);

                        if (rtmp < 0)
                            rtmp = 0;
                        if (rtmp > 255)
                            rtmp = 255;
                        if (gtmp < 0)
                            gtmp = 0;
                        if (gtmp > 255)
                            gtmp = 255;
                        if (btmp < 0)
                            btmp = 0;
                        if (btmp > 255)
                            btmp = 255;

                        r = (uint8_t)rtmp;
                        g = (uint8_t)gtmp;
                        b = (uint8_t)btmp;
                    } else {
                        int yw = comp_w[idx_y];
                        int yh = comp_h[idx_y];
                        int cbw = comp_w[idx_cb];
                        int cbh = comp_h[idx_cb];
                        int crw = comp_w[idx_cr];
                        int crh = comp_h[idx_cr];
                        int kw = comp_w[idx_k];
                        int kh = comp_h[idx_k];

                        int yy = y * yh / mcu_h;
                        int yx = x * yw / mcu_w;
                        int cby = y * cbh / mcu_h;
                        int cbx = x * cbw / mcu_w;
                        int cry = y * crh / mcu_h;
                        int crx = x * crw / mcu_w;
                        int ky = y * kh / mcu_h;
                        int kx = x * kw / mcu_w;

                        uint8_t k = comp_buf[idx_k][ky * kw + kx];
                        if (adobe_transform == 2) {
                            int Y = comp_buf[idx_y][yy * yw + yx];
                            int Cb = comp_buf[idx_cb][cby * cbw + cbx] - 128;
                            int Cr = comp_buf[idx_cr][cry * crw + crx] - 128;

                            int rtmp = Y + ((91881 * Cr) >> 16);
                            int gtmp = Y - ((22554 * Cb + 46802 * Cr) >> 16);
                            int btmp = Y + ((116130 * Cb) >> 16);

                            if (rtmp < 0)
                                rtmp = 0;
                            if (rtmp > 255)
                                rtmp = 255;
                            if (gtmp < 0)
                                gtmp = 0;
                            if (gtmp > 255)
                                gtmp = 255;
                            if (btmp < 0)
                                btmp = 0;
                            if (btmp > 255)
                                btmp = 255;

                            int r2 = rtmp - k;
                            int g2 = gtmp - k;
                            int b2 = btmp - k;
                            if (r2 < 0)
                                r2 = 0;
                            if (g2 < 0)
                                g2 = 0;
                            if (b2 < 0)
                                b2 = 0;
                            r = (uint8_t)r2;
                            g = (uint8_t)g2;
                            b = (uint8_t)b2;
                        } else {
                            int c = comp_buf[idx_y][yy * yw + yx];
                            int m = comp_buf[idx_cb][cby * cbw + cbx];
                            int yv = comp_buf[idx_cr][cry * crw + crx];

                            int r2 = 255 - (c + k);
                            int g2 = 255 - (m + k);
                            int b2 = 255 - (yv + k);
                            if (r2 < 0)
                                r2 = 0;
                            if (g2 < 0)
                                g2 = 0;
                            if (b2 < 0)
                                b2 = 0;
                            if (r2 > 255)
                                r2 = 255;
                            if (g2 > 255)
                                g2 = 255;
                            if (b2 > 255)
                                b2 = 255;
                            r = (uint8_t)r2;
                            g = (uint8_t)g2;
                            b = (uint8_t)b2;
                        }
                    }

                    size_t dst = ((size_t)out_y * (size_t)width + (size_t)out_x) * 4u;
                    rgba[dst + 0] = r;
                    rgba[dst + 1] = g;
                    rgba[dst + 2] = b;
                    rgba[dst + 3] = 255;
                }
            }
        }
    }

    out->width = (uint32_t)width;
    out->height = (uint32_t)height;
    out->rgba = rgba;
    return 1;
}

int cupidimage_jpeg_decode_scan(const uint8_t *data, size_t size, jpeg_comp *comps, int num_comp,
                                const int *scan_comp, int width, int height, int hmax, int vmax,
                                int qtables[4][64], const jpeg_huff dc_tables[4],
                                const jpeg_huff ac_tables[4], int restart_interval,
                                int adobe_transform, size_t *consumed, cupidimage_image *out,
                                char *err, size_t errcap, const jpeg_sample_profile *profile) {
    jpeg_bitstream bs;
    bs.data = data;
    bs.size = size;
    bs.pos = 0;
    bs.bitbuf = 0;
    bs.bitcount = 0;
    bs.marker = 0;

    for (int i = 0; i < num_comp; i++) {
        comps[i].dc_pred = 0;
        if (!dc_tables[comps[i].dc_table].valid || !ac_tables[comps[i].ac_table].valid) {
            cupidimage_jpeg_set_error(err, errcap, "missing huffman table");
            return 0;
        }
    }

    unsigned channels = profile ? (unsigned)num_comp : 4u;
    size_t rgba_size = (size_t)width * (size_t)height * channels;
    if (rgba_size / channels != (size_t)width * (size_t)height) {
        cupidimage_jpeg_set_error(err, errcap, "image too large");
        return 0;
    }

    uint8_t *rgba = (uint8_t *)malloc(rgba_size);
    if (!rgba) {
        cupidimage_jpeg_set_error(err, errcap, "out of memory");
        return 0;
    }

    int mcu_w = hmax * 8;
    int mcu_h = vmax * 8;
    int mcu_cols = (width + mcu_w - 1) / mcu_w;
    int mcu_rows = (height + mcu_h - 1) / mcu_h;

    uint8_t comp_buf[4][32 * 32];
    int comp_w[4] = {0, 0, 0, 0};
    int comp_h[4] = {0, 0, 0, 0};
    for (int i = 0; i < num_comp; i++) {
        comp_w[i] = comps[i].h * 8;
        comp_h[i] = comps[i].v * 8;
    }

    int idx_y = 0;
    int idx_cb = (num_comp > 1) ? 1 : 0;
    int idx_cr = (num_comp > 2) ? 2 : 0;
    int idx_k = (num_comp > 3) ? 3 : 0;
    for (int i = 0; i < num_comp; i++) {
        if (comps[i].id == 1)
            idx_y = i;
        if (comps[i].id == 2)
            idx_cb = i;
        if (comps[i].id == 3)
            idx_cr = i;
        if (comps[i].id == 4)
            idx_k = i;
    }

    int32_t block[64];
    uint8_t block_out[64];

    int mcu_count = 0;
    int rst_index = 0;

    for (int my = 0; my < mcu_rows; my++) {
        for (int mx = 0; mx < mcu_cols; mx++) {
            for (int si = 0; si < num_comp; si++) {
                int ci = scan_comp[si];
                jpeg_comp *c = &comps[ci];
                int cw = comp_w[ci];

                for (int by = 0; by < c->v; by++) {
                    for (int bx = 0; bx < c->h; bx++) {
                        if (!cupidimage_jpeg_decode_block(&bs, &dc_tables[c->dc_table],
                                                          &ac_tables[c->ac_table], qtables[c->tq],
                                                          &c->dc_pred, block)) {
                            free(rgba);
                            cupidimage_jpeg_set_error(err, errcap, "jpeg decode failed");
                            return 0;
                        }
                        idct_8x8(block, block_out, 8);
                        for (int y = 0; y < 8; y++) {
                            memcpy(&comp_buf[ci][(by * 8 + y) * cw + bx * 8], &block_out[y * 8], 8);
                        }
                    }
                }
            }

            for (int y = 0; y < mcu_h; y++) {
                int out_y = my * mcu_h + y;
                if (out_y >= height) {
                    continue;
                }
                for (int x = 0; x < mcu_w; x++) {
                    int out_x = mx * mcu_w + x;
                    if (out_x >= width) {
                        continue;
                    }

                    if (profile) {
                        size_t dst = ((size_t)out_y * width + out_x) * channels;
                        jpeg_native_pixel(rgba + dst, comp_buf, comp_w, comp_h, num_comp, x, y,
                                          mcu_w, mcu_h);
                        continue;
                    }
                    uint8_t r, g, b;
                    if (num_comp == 1) {
                        int yw = comp_w[idx_y];
                        int yh = comp_h[idx_y];
                        int yy = y * yh / mcu_h;
                        int xx = x * yw / mcu_w;
                        uint8_t yv = comp_buf[idx_y][yy * yw + xx];
                        r = yv;
                        g = yv;
                        b = yv;
                    } else if (num_comp == 3) {
                        int yw = comp_w[idx_y];
                        int yh = comp_h[idx_y];
                        int cbw = comp_w[idx_cb];
                        int cbh = comp_h[idx_cb];
                        int crw = comp_w[idx_cr];
                        int crh = comp_h[idx_cr];

                        int yy = y * yh / mcu_h;
                        int yx = x * yw / mcu_w;
                        int cby = y * cbh / mcu_h;
                        int cbx = x * cbw / mcu_w;
                        int cry = y * crh / mcu_h;
                        int crx = x * crw / mcu_w;

                        int Y = comp_buf[idx_y][yy * yw + yx];
                        int Cb = comp_buf[idx_cb][cby * cbw + cbx] - 128;
                        int Cr = comp_buf[idx_cr][cry * crw + crx] - 128;

                        int rtmp = Y + ((91881 * Cr) >> 16);
                        int gtmp = Y - ((22554 * Cb + 46802 * Cr) >> 16);
                        int btmp = Y + ((116130 * Cb) >> 16);

                        if (rtmp < 0)
                            rtmp = 0;
                        if (rtmp > 255)
                            rtmp = 255;
                        if (gtmp < 0)
                            gtmp = 0;
                        if (gtmp > 255)
                            gtmp = 255;
                        if (btmp < 0)
                            btmp = 0;
                        if (btmp > 255)
                            btmp = 255;

                        r = (uint8_t)rtmp;
                        g = (uint8_t)gtmp;
                        b = (uint8_t)btmp;
                    } else {
                        int yw = comp_w[idx_y];
                        int yh = comp_h[idx_y];
                        int cbw = comp_w[idx_cb];
                        int cbh = comp_h[idx_cb];
                        int crw = comp_w[idx_cr];
                        int crh = comp_h[idx_cr];
                        int kw = comp_w[idx_k];
                        int kh = comp_h[idx_k];

                        int yy = y * yh / mcu_h;
                        int yx = x * yw / mcu_w;
                        int cby = y * cbh / mcu_h;
                        int cbx = x * cbw / mcu_w;
                        int cry = y * crh / mcu_h;
                        int crx = x * crw / mcu_w;
                        int ky = y * kh / mcu_h;
                        int kx = x * kw / mcu_w;

                        uint8_t k = comp_buf[idx_k][ky * kw + kx];
                        if (adobe_transform == 2) {
                            int Y = comp_buf[idx_y][yy * yw + yx];
                            int Cb = comp_buf[idx_cb][cby * cbw + cbx] - 128;
                            int Cr = comp_buf[idx_cr][cry * crw + crx] - 128;

                            int rtmp = Y + ((91881 * Cr) >> 16);
                            int gtmp = Y - ((22554 * Cb + 46802 * Cr) >> 16);
                            int btmp = Y + ((116130 * Cb) >> 16);

                            if (rtmp < 0)
                                rtmp = 0;
                            if (rtmp > 255)
                                rtmp = 255;
                            if (gtmp < 0)
                                gtmp = 0;
                            if (gtmp > 255)
                                gtmp = 255;
                            if (btmp < 0)
                                btmp = 0;
                            if (btmp > 255)
                                btmp = 255;

                            int r2 = rtmp - k;
                            int g2 = gtmp - k;
                            int b2 = btmp - k;
                            if (r2 < 0)
                                r2 = 0;
                            if (g2 < 0)
                                g2 = 0;
                            if (b2 < 0)
                                b2 = 0;
                            r = (uint8_t)r2;
                            g = (uint8_t)g2;
                            b = (uint8_t)b2;
                        } else {
                            int c = comp_buf[idx_y][yy * yw + yx];
                            int m = comp_buf[idx_cb][cby * cbw + cbx];
                            int yv = comp_buf[idx_cr][cry * crw + crx];

                            int r2 = 255 - (c + k);
                            int g2 = 255 - (m + k);
                            int b2 = 255 - (yv + k);
                            if (r2 < 0)
                                r2 = 0;
                            if (g2 < 0)
                                g2 = 0;
                            if (b2 < 0)
                                b2 = 0;
                            if (r2 > 255)
                                r2 = 255;
                            if (g2 > 255)
                                g2 = 255;
                            if (b2 > 255)
                                b2 = 255;
                            r = (uint8_t)r2;
                            g = (uint8_t)g2;
                            b = (uint8_t)b2;
                        }
                    }

                    size_t dst = ((size_t)out_y * (size_t)width + (size_t)out_x) * 4u;
                    rgba[dst + 0] = r;
                    rgba[dst + 1] = g;
                    rgba[dst + 2] = b;
                    rgba[dst + 3] = 255;
                }
            }

            mcu_count++;
            if (restart_interval > 0 && (mcu_count % restart_interval) == 0 &&
                (my + 1 < mcu_rows || mx + 1 < mcu_cols)) {
                int expected = 0xD0 + (rst_index & 7);
                if (!cupidimage_jpeg_consume_restart(&bs, expected)) {
                    free(rgba);
                    cupidimage_jpeg_set_error(err, errcap, "jpeg restart marker error");
                    return 0;
                }
                rst_index++;
                for (int i = 0; i < num_comp; i++) {
                    comps[i].dc_pred = 0;
                }
            }
        }
    }

    out->width = (uint32_t)width;
    out->height = (uint32_t)height;
    out->rgba = rgba;
    if (bs.marker != 0 && bs.pos >= 2) {
        *consumed = bs.pos - 2;
    } else {
        *consumed = bs.pos;
    }
    return 1;
}

void cupidimage_jpeg_convert_native(cupidimage_image *image, const jpeg_sample_profile *profile) {
    if (profile->components >= 3 && jpeg_sample_transform(profile, *profile->marker))
        for (size_t i = 0, count = (size_t)image->width * image->height; i < count; i++)
            jpeg_native_color(image->rgba + i * profile->components, profile->components);
}
