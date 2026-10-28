#include "core/internal.h"
#include "cupidimage.h"
#include "formats/svg/cupidimage_svg_base.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wshadow"
#endif

#include "svg_internal.h"

void cupidimage_svg_gaussian_blur(uint8_t *buf, uint32_t w, uint32_t h, float sigma_x,
                                  float sigma_y) {
    if (!buf || w == 0 || h == 0) {
        return;
    }
    if (sigma_x < 0.01f && sigma_y < 0.01f) {
        return;
    }
    uint8_t *tmp = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
    if (!tmp) {
        return;
    }
    const int max_radius = 96;
    if (sigma_x >= 0.01f) {
        int radius = (int)fminf((float)max_radius, ceilf(3.0f * sigma_x));
        if (radius < 1)
            radius = 1;
        if (radius > max_radius)
            radius = max_radius;
        float sigma_eff = sigma_x;
        float max_sigma = (float)radius / 3.0f;
        if (sigma_eff > max_sigma)
            sigma_eff = max_sigma;
        int klen = radius * 2 + 1;
        float *kernel = (float *)malloc((size_t)klen * sizeof(float));
        if (!kernel) {
            free(tmp);
            return;
        }
        float sum = 0.0f;
        for (int i = -radius; i <= radius; i++) {
            float v = expf(-0.5f * (float)(i * i) / (sigma_eff * sigma_eff));
            kernel[i + radius] = v;
            sum += v;
        }
        if (sum > 0.0f) {
            for (int i = 0; i < klen; i++) {
                kernel[i] /= sum;
            }
        }
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                float acc[4] = {0, 0, 0, 0};
                for (int k = -radius; k <= radius; k++) {
                    int sx = (int)x + k;
                    if (sx < 0)
                        sx = 0;
                    if (sx >= (int)w)
                        sx = (int)w - 1;
                    size_t idx = ((size_t)y * w + (size_t)sx) * 4u;
                    float wgt = kernel[k + radius];
                    acc[0] += buf[idx + 0] * wgt;
                    acc[1] += buf[idx + 1] * wgt;
                    acc[2] += buf[idx + 2] * wgt;
                    acc[3] += buf[idx + 3] * wgt;
                }
                size_t out = ((size_t)y * w + (size_t)x) * 4u;
                tmp[out + 0] = (uint8_t)lroundf(fminf(fmaxf(acc[0], 0.0f), 255.0f));
                tmp[out + 1] = (uint8_t)lroundf(fminf(fmaxf(acc[1], 0.0f), 255.0f));
                tmp[out + 2] = (uint8_t)lroundf(fminf(fmaxf(acc[2], 0.0f), 255.0f));
                tmp[out + 3] = (uint8_t)lroundf(fminf(fmaxf(acc[3], 0.0f), 255.0f));
            }
        }
        memcpy(buf, tmp, (size_t)w * (size_t)h * 4u);
        free(kernel);
    }
    if (sigma_y >= 0.01f) {
        int radius = (int)fminf((float)max_radius, ceilf(3.0f * sigma_y));
        if (radius < 1)
            radius = 1;
        if (radius > max_radius)
            radius = max_radius;
        float sigma_eff = sigma_y;
        float max_sigma = (float)radius / 3.0f;
        if (sigma_eff > max_sigma)
            sigma_eff = max_sigma;
        int klen = radius * 2 + 1;
        float *kernel = (float *)malloc((size_t)klen * sizeof(float));
        if (!kernel) {
            free(tmp);
            return;
        }
        float sum = 0.0f;
        for (int i = -radius; i <= radius; i++) {
            float v = expf(-0.5f * (float)(i * i) / (sigma_eff * sigma_eff));
            kernel[i + radius] = v;
            sum += v;
        }
        if (sum > 0.0f) {
            for (int i = 0; i < klen; i++) {
                kernel[i] /= sum;
            }
        }
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                float acc[4] = {0, 0, 0, 0};
                for (int k = -radius; k <= radius; k++) {
                    int sy = (int)y + k;
                    if (sy < 0)
                        sy = 0;
                    if (sy >= (int)h)
                        sy = (int)h - 1;
                    size_t idx = ((size_t)sy * w + (size_t)x) * 4u;
                    float wgt = kernel[k + radius];
                    acc[0] += buf[idx + 0] * wgt;
                    acc[1] += buf[idx + 1] * wgt;
                    acc[2] += buf[idx + 2] * wgt;
                    acc[3] += buf[idx + 3] * wgt;
                }
                size_t out = ((size_t)y * w + (size_t)x) * 4u;
                tmp[out + 0] = (uint8_t)lroundf(fminf(fmaxf(acc[0], 0.0f), 255.0f));
                tmp[out + 1] = (uint8_t)lroundf(fminf(fmaxf(acc[1], 0.0f), 255.0f));
                tmp[out + 2] = (uint8_t)lroundf(fminf(fmaxf(acc[2], 0.0f), 255.0f));
                tmp[out + 3] = (uint8_t)lroundf(fminf(fmaxf(acc[3], 0.0f), 255.0f));
            }
        }
        memcpy(buf, tmp, (size_t)w * (size_t)h * 4u);
        free(kernel);
    }
    free(tmp);
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
