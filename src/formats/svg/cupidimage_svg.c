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

int cupidimage_load_svg_with_options(const unsigned char *data, size_t size, cupidimage_image *out,
                                     const cupidimage_svg_options *opts, char *err, size_t errcap) {
    if (!data || !out) {
        cupidimage_set_err(err, errcap, "invalid arguments");
        return 0;
    }
    memset(out, 0, sizeof(*out));
    if (opts &&
        (!isfinite(opts->scale) || !isfinite(opts->dpi) || !isfinite(opts->animation_time))) {
        cupidimage_set_err(err, errcap, "invalid SVG options");
        return 0;
    }
    svg_render_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    int ok = cupidimage_svg_render(&ctx, data, size, err, errcap, opts);
    if (!ok) {
        free(ctx.hi_rgba);
        return 0;
    }

    size_t out_size = (size_t)ctx.width * (size_t)ctx.height * 4u;
    if (out_size / 4u != (size_t)ctx.width * (size_t)ctx.height) {
        free(ctx.hi_rgba);
        cupidimage_set_err(err, errcap, "image too large");
        return 0;
    }
    uint8_t *rgba = (uint8_t *)malloc(out_size);
    if (!rgba) {
        free(ctx.hi_rgba);
        cupidimage_set_err(err, errcap, "out of memory");
        return 0;
    }

    uint32_t ss = ctx.ss;
    size_t hi_w = (size_t)ctx.width * ss;
    size_t block = (size_t)ss * (size_t)ss;
    for (uint32_t y = 0; y < ctx.height; y++) {
        for (uint32_t x = 0; x < ctx.width; x++) {
            uint64_t sum_r = 0, sum_g = 0, sum_b = 0, sum_a = 0;
            size_t base = ((size_t)y * ss * hi_w) + ((size_t)x * ss);
            for (uint32_t sy = 0; sy < ss; sy++) {
                size_t row = base + (size_t)sy * hi_w;
                for (uint32_t sx = 0; sx < ss; sx++) {
                    size_t idx = (row + sx) * 4u;
                    sum_r += ctx.hi_rgba[idx + 0];
                    sum_g += ctx.hi_rgba[idx + 1];
                    sum_b += ctx.hi_rgba[idx + 2];
                    sum_a += ctx.hi_rgba[idx + 3];
                }
            }
            uint8_t a = (uint8_t)(sum_a / block);
            uint8_t r = 0, g = 0, b = 0;
            if (a > 0) {
                r = (uint8_t)((sum_r / block) * 255u / a);
                g = (uint8_t)((sum_g / block) * 255u / a);
                b = (uint8_t)((sum_b / block) * 255u / a);
            }
            size_t out_idx = ((size_t)y * ctx.width + x) * 4u;
            rgba[out_idx + 0] = r;
            rgba[out_idx + 1] = g;
            rgba[out_idx + 2] = b;
            rgba[out_idx + 3] = a;
        }
    }

    free(ctx.hi_rgba);
    out->width = ctx.width;
    out->height = ctx.height;
    out->rgba = rgba;
    out->hotspot_x = 0;
    out->hotspot_y = 0;
    return 1;
}

int cupidimage_load_svg(const unsigned char *data, size_t size, cupidimage_image *out, char *err,
                        size_t errcap) {
    cupidimage_svg_options opts;
    opts.width = 0;
    opts.height = 0;
    opts.scale = 1.0f;
    opts.dpi = 96.0f;
    opts.animation_time = 0.0f;
    opts.supersampling = 2;
    opts.background_alpha = 0;
    return cupidimage_load_svg_with_options(data, size, out, &opts, err, errcap);
}

int cupidimage_load_svg_file(const char *path, cupidimage_image *out, char *err, size_t errcap) {
    return cupidimage_load_image_file_via_memory(path, out, err, errcap, cupidimage_load_svg);
}

int cupidimage_load_svg_file_with_options(const char *path, cupidimage_image *out,
                                          const cupidimage_svg_options *opts, char *err,
                                          size_t errcap) {
    if (!path || !out) {
        cupidimage_set_err(err, errcap, "invalid arguments");
        return 0;
    }
    unsigned char *data = NULL;
    size_t size = 0;
    if (!cupidimage_read_file_bytes(path, &data, &size, err, errcap)) {
        return 0;
    }
    int ok = cupidimage_load_svg_with_options(data, size, out, opts, err, errcap);
    free(data);
    return ok;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
