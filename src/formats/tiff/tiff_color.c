#include "tiff_internal.h"

int cupidimage_tiff_convert_to_rgba(const tiff_context *ctx, const tiff_image_info *info,
                                    const uint8_t *raw, size_t row_bytes, uint8_t *rgba,
                                    size_t rgba_size, char *err, size_t errcap) {
    uint16_t bps = info->bits_per_sample[0];
    int photometric = info->photometric;
    int samples = info->samples_per_pixel;

    size_t expected_rgba = (size_t)info->width * (size_t)info->height * 4u;
    if (rgba_size < expected_rgba) {
        cupidimage_set_err(err, errcap, "invalid RGBA buffer");
        return 0;
    }

    if (bps == 0) {
        cupidimage_set_err(err, errcap, "unsupported TIFF bit depth");
        return 0;
    }
    if (info->sample_format == 3) {
        if (bps != 32 && bps != 64) {
            cupidimage_set_err(err, errcap, "unsupported TIFF float bit depth");
            return 0;
        }
    } else if (bps > 32) {
        cupidimage_set_err(err, errcap, "unsupported TIFF bit depth");
        return 0;
    }

    int base_samples = 1;
    if (photometric == TIFF_PHOTOMETRIC_RGB) {
        base_samples = 3;
    } else if (photometric == TIFF_PHOTOMETRIC_CMYK) {
        base_samples = 4;
    } else if (photometric == TIFF_PHOTOMETRIC_YCBCR) {
        base_samples = 3;
    } else if (photometric == TIFF_PHOTOMETRIC_CIELAB) {
        base_samples = 3;
    }
    if (samples < base_samples) {
        cupidimage_set_err(err, errcap, "invalid TIFF samples per pixel");
        return 0;
    }

    int alpha_index = -1;
    int premultiplied = 0;
    for (uint16_t i = 0; i < info->extra_count; i++) {
        if (info->extra_samples[i] == TIFF_EXTRA_SAMPLE_ASSOC_ALPHA ||
            info->extra_samples[i] == TIFF_EXTRA_SAMPLE_UNASSOC_ALPHA) {
            alpha_index = base_samples + (int)i;
            premultiplied = (info->extra_samples[i] == TIFF_EXTRA_SAMPLE_ASSOC_ALPHA);
            break;
        }
    }
    if (alpha_index < 0 && samples == base_samples + 1) {
        alpha_index = base_samples;
    }
    if (alpha_index >= samples) {
        alpha_index = -1;
    }

    uint32_t palette_len = 0;
    if (photometric == TIFF_PHOTOMETRIC_PALETTE) {
        if (!info->color_map || info->color_map_count == 0) {
            cupidimage_set_err(err, errcap, "missing TIFF ColorMap");
            return 0;
        }
        if ((info->color_map_count % 3u) != 0) {
            cupidimage_set_err(err, errcap, "invalid TIFF ColorMap");
            return 0;
        }
        palette_len = (uint32_t)(info->color_map_count / 3u);
    }

    int float_samples = (info->sample_format == 3);
    for (uint32_t y = 0; y < info->height; y++) {
        const uint8_t *row = raw + (size_t)y * row_bytes;
        for (uint32_t x = 0; x < info->width; x++) {
            uint32_t sample_base = x * (uint32_t)samples;
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            uint8_t a = 255;

            if (photometric == TIFF_PHOTOMETRIC_RGB) {
                if (float_samples) {
                    r = cupidimage_tiff_scale_float(
                        cupidimage_tiff_read_sample_float(ctx, row, sample_base + 0u, bps));
                    g = cupidimage_tiff_scale_float(
                        cupidimage_tiff_read_sample_float(ctx, row, sample_base + 1u, bps));
                    b = cupidimage_tiff_scale_float(
                        cupidimage_tiff_read_sample_float(ctx, row, sample_base + 2u, bps));
                } else {
                    uint32_t r0 = cupidimage_tiff_read_sample(ctx, row, sample_base + 0u, bps,
                                                              info->fill_order);
                    uint32_t g0 = cupidimage_tiff_read_sample(ctx, row, sample_base + 1u, bps,
                                                              info->fill_order);
                    uint32_t b0 = cupidimage_tiff_read_sample(ctx, row, sample_base + 2u, bps,
                                                              info->fill_order);
                    r = cupidimage_tiff_scale_sample(r0, bps);
                    g = cupidimage_tiff_scale_sample(g0, bps);
                    b = cupidimage_tiff_scale_sample(b0, bps);
                }
            } else if (photometric == TIFF_PHOTOMETRIC_BLACK_IS_ZERO ||
                       photometric == TIFF_PHOTOMETRIC_WHITE_IS_ZERO) {
                uint8_t gray = 0;
                if (float_samples) {
                    gray = cupidimage_tiff_scale_float(
                        cupidimage_tiff_read_sample_float(ctx, row, sample_base, bps));
                } else {
                    uint32_t v =
                        cupidimage_tiff_read_sample(ctx, row, sample_base, bps, info->fill_order);
                    gray = cupidimage_tiff_scale_sample(v, bps);
                }
                if (photometric == TIFF_PHOTOMETRIC_WHITE_IS_ZERO) {
                    gray = (uint8_t)(255u - gray);
                }
                r = g = b = gray;
            } else if (photometric == TIFF_PHOTOMETRIC_PALETTE) {
                uint32_t idx =
                    cupidimage_tiff_read_sample(ctx, row, sample_base, bps, info->fill_order);
                if (idx >= palette_len) {
                    cupidimage_set_err(err, errcap, "invalid TIFF palette index");
                    return 0;
                }
                uint16_t pr = info->color_map[idx];
                uint16_t pg = info->color_map[idx + palette_len];
                uint16_t pb = info->color_map[idx + palette_len * 2u];
                r = cupidimage_tiff_sample16_to_8(pr);
                g = cupidimage_tiff_sample16_to_8(pg);
                b = cupidimage_tiff_sample16_to_8(pb);
            } else if (photometric == TIFF_PHOTOMETRIC_CMYK) {
                uint8_t c, m, yv, k;
                if (float_samples) {
                    c = cupidimage_tiff_scale_float(
                        cupidimage_tiff_read_sample_float(ctx, row, sample_base + 0u, bps));
                    m = cupidimage_tiff_scale_float(
                        cupidimage_tiff_read_sample_float(ctx, row, sample_base + 1u, bps));
                    yv = cupidimage_tiff_scale_float(
                        cupidimage_tiff_read_sample_float(ctx, row, sample_base + 2u, bps));
                    k = cupidimage_tiff_scale_float(
                        cupidimage_tiff_read_sample_float(ctx, row, sample_base + 3u, bps));
                } else {
                    c = cupidimage_tiff_scale_sample(
                        cupidimage_tiff_read_sample(ctx, row, sample_base + 0u, bps,
                                                    info->fill_order),
                        bps);
                    m = cupidimage_tiff_scale_sample(
                        cupidimage_tiff_read_sample(ctx, row, sample_base + 1u, bps,
                                                    info->fill_order),
                        bps);
                    yv = cupidimage_tiff_scale_sample(
                        cupidimage_tiff_read_sample(ctx, row, sample_base + 2u, bps,
                                                    info->fill_order),
                        bps);
                    k = cupidimage_tiff_scale_sample(
                        cupidimage_tiff_read_sample(ctx, row, sample_base + 3u, bps,
                                                    info->fill_order),
                        bps);
                }
                r = (uint8_t)(((255u - c) * (255u - k) + 127u) / 255u);
                g = (uint8_t)(((255u - m) * (255u - k) + 127u) / 255u);
                b = (uint8_t)(((255u - yv) * (255u - k) + 127u) / 255u);
            } else if (photometric == TIFF_PHOTOMETRIC_YCBCR) {
                uint8_t yv = cupidimage_tiff_scale_sample(
                    cupidimage_tiff_read_sample(ctx, row, sample_base + 0u, bps, info->fill_order),
                    bps);
                uint8_t cb = cupidimage_tiff_scale_sample(
                    cupidimage_tiff_read_sample(ctx, row, sample_base + 1u, bps, info->fill_order),
                    bps);
                uint8_t cr = cupidimage_tiff_scale_sample(
                    cupidimage_tiff_read_sample(ctx, row, sample_base + 2u, bps, info->fill_order),
                    bps);
                int cbi = (int)cb - 128;
                int cri = (int)cr - 128;
                double cbi_d = (double)cbi;
                double cri_d = (double)cri;
                int ri = (int)((double)yv + (1.402 * cri_d));
                int gi = (int)((double)yv - (0.344136 * cbi_d) - (0.714136 * cri_d));
                int bi = (int)((double)yv + (1.772 * cbi_d));
                r = cupidimage_tiff_clamp8(ri);
                g = cupidimage_tiff_clamp8(gi);
                b = cupidimage_tiff_clamp8(bi);
            } else if (photometric == TIFF_PHOTOMETRIC_CIELAB) {
                uint8_t l8 = cupidimage_tiff_scale_sample(
                    cupidimage_tiff_read_sample(ctx, row, sample_base + 0u, bps, info->fill_order),
                    bps);
                uint8_t a8 = cupidimage_tiff_scale_sample(
                    cupidimage_tiff_read_sample(ctx, row, sample_base + 1u, bps, info->fill_order),
                    bps);
                uint8_t b8 = cupidimage_tiff_scale_sample(
                    cupidimage_tiff_read_sample(ctx, row, sample_base + 2u, bps, info->fill_order),
                    bps);
                double L = ((double)l8 * 100.0) / 255.0;
                double aa = (double)a8 - 128.0;
                double bb = (double)b8 - 128.0;
                double fy = (L + 16.0) / 116.0;
                double fx = fy + (aa / 500.0);
                double fz = fy - (bb / 200.0);
                double xr = (fx * fx * fx > 0.008856) ? fx * fx * fx : (fx - 16.0 / 116.0) / 7.787;
                double yr = (fy * fy * fy > 0.008856) ? fy * fy * fy : (fy - 16.0 / 116.0) / 7.787;
                double zr = (fz * fz * fz > 0.008856) ? fz * fz * fz : (fz - 16.0 / 116.0) / 7.787;
                double X = xr * 0.95047;
                double Y = yr * 1.00000;
                double Z = zr * 1.08883;
                double rr = X * 3.2406 + Y * -1.5372 + Z * -0.4986;
                double gg = X * -0.9689 + Y * 1.8758 + Z * 0.0415;
                double bb2 = X * 0.0557 + Y * -0.2040 + Z * 1.0570;
                r = cupidimage_tiff_clamp8((int)(rr * 255.0 + 0.5));
                g = cupidimage_tiff_clamp8((int)(gg * 255.0 + 0.5));
                b = cupidimage_tiff_clamp8((int)(bb2 * 255.0 + 0.5));
            } else {
                cupidimage_set_err(err, errcap, "unsupported TIFF photometric");
                return 0;
            }

            if (alpha_index >= 0) {
                if (float_samples) {
                    a = cupidimage_tiff_scale_float(cupidimage_tiff_read_sample_float(
                        ctx, row, sample_base + (uint32_t)alpha_index, bps));
                } else {
                    uint32_t av = cupidimage_tiff_read_sample(
                        ctx, row, sample_base + (uint32_t)alpha_index, bps, info->fill_order);
                    a = cupidimage_tiff_scale_sample(av, bps);
                }
                if (premultiplied && a > 0 && a < 255) {
                    r = (uint8_t)((r * 255u + a / 2u) / a);
                    g = (uint8_t)((g * 255u + a / 2u) / a);
                    b = (uint8_t)((b * 255u + a / 2u) / a);
                }
            }

            size_t dst = ((size_t)y * (size_t)info->width + x) * 4u;
            rgba[dst + 0] = r;
            rgba[dst + 1] = g;
            rgba[dst + 2] = b;
            rgba[dst + 3] = a;
        }
    }

    return 1;
}

int cupidimage_tiff_apply_orientation_rgba(uint8_t **rgba, uint32_t *width, uint32_t *height,
                                           uint16_t orientation, char *err, size_t errcap) {
    if (orientation == 1) {
        return 1;
    }
    if (orientation < 1 || orientation > 8) {
        cupidimage_set_err(err, errcap, "invalid TIFF orientation");
        return 0;
    }

    uint32_t src_w = *width;
    uint32_t src_h = *height;
    uint32_t dst_w = (orientation >= 5) ? src_h : src_w;
    uint32_t dst_h = (orientation >= 5) ? src_w : src_h;
    size_t dst_size = (size_t)dst_w * (size_t)dst_h * 4u;
    uint8_t *dst = (uint8_t *)malloc(dst_size);
    if (!dst) {
        cupidimage_set_err(err, errcap, "out of memory");
        return 0;
    }

    for (uint32_t y = 0; y < dst_h; y++) {
        for (uint32_t x = 0; x < dst_w; x++) {
            uint32_t sx = 0;
            uint32_t sy = 0;
            switch (orientation) {
                case 1:
                    sx = x;
                    sy = y;
                    break;
                case 2:
                    sx = src_w - 1u - x;
                    sy = y;
                    break;
                case 3:
                    sx = src_w - 1u - x;
                    sy = src_h - 1u - y;
                    break;
                case 4:
                    sx = x;
                    sy = src_h - 1u - y;
                    break;
                case 5:
                    sx = y;
                    sy = x;
                    break;
                case 6:
                    sx = y;
                    sy = src_h - 1u - x;
                    break;
                case 7:
                    sx = src_w - 1u - y;
                    sy = src_h - 1u - x;
                    break;
                case 8:
                    sx = src_w - 1u - y;
                    sy = x;
                    break;
                default:
                    sx = x;
                    sy = y;
                    break;
            }
            size_t src_idx = ((size_t)sy * (size_t)src_w + sx) * 4u;
            size_t dst_idx = ((size_t)y * (size_t)dst_w + x) * 4u;
            dst[dst_idx + 0] = (*rgba)[src_idx + 0];
            dst[dst_idx + 1] = (*rgba)[src_idx + 1];
            dst[dst_idx + 2] = (*rgba)[src_idx + 2];
            dst[dst_idx + 3] = (*rgba)[src_idx + 3];
        }
    }

    free(*rgba);
    *rgba = dst;
    *width = dst_w;
    *height = dst_h;
    return 1;
}
