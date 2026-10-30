#include "core/internal.h"
#include "jpeg_internal.h"
#include <stdlib.h>
#include <string.h>

static uint16_t read_be16(const unsigned char *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static void jpeg_free_coeffs(jpeg_comp *comps, int num_comp) {
    for (int i = 0; i < num_comp; i++) {
        free(comps[i].coeffs);
        comps[i].coeffs = NULL;
    }
}

static int jpeg_allocate_coefficients(jpeg_comp *components, int count, int columns, int rows,
                                      char *error, size_t capacity) {
    for (int i = 0; i < count; i++) {
        jpeg_comp *component = components + i;
        component->blocks_w = columns * component->h;
        component->blocks_h = rows * component->v;
        size_t blocks = (size_t)component->blocks_w * component->blocks_h;
        if (!blocks || blocks > SIZE_MAX / (64u * sizeof(int32_t))) {
            cupidimage_jpeg_set_error(error, capacity, "image too large");
            return 0;
        }
        component->coeffs = calloc(blocks * 64u, sizeof(int32_t));
        if (!component->coeffs) {
            cupidimage_jpeg_set_error(error, capacity, "out of memory");
            return 0;
        }
    }
    return 1;
}

static int jpeg_load(const unsigned char *data, size_t size, cupidimage_image *out, char *err,
                     size_t errcap, const jpeg_sample_profile *profile) {
    jpeg_huff dc_tables[4] = {0};
    jpeg_huff ac_tables[4] = {0};
    int qtables[4][64];
    int qvalid[4] = {0, 0, 0, 0};
    jpeg_comp comps[4] = {0};
    int num_comp = 0;
    int width = 0;
    int height = 0;
    int precision = 0;
    int hmax = 0;
    int vmax = 0;
    int restart_interval = 0;
    int progressive = 0, arithmetic = 0;
    jpeg_arithmetic_conditioning conditioning = {{0, 0, 0, 0}, {1, 1, 1, 1}, {5, 5, 5, 5}};
    int adobe_transform = -1;
    int coeffs_allocated = 0;
    int mcu_cols = 0;
    int mcu_rows = 0;
    size_t pos = 2;
    int saw_sof = 0;
    int saw_sos = 0, saw_eoi = 0, sequential_buffered = 0;
    unsigned sequential_seen = 0;

    if (!data || !out) {
        cupidimage_jpeg_set_error(err, errcap, "invalid arguments");
        goto fail;
    }

    memset(out, 0, sizeof(*out));

    if (size < 2 || data[0] != 0xFF || data[1] != 0xD8) {
        cupidimage_jpeg_set_error(err, errcap, "not a JPEG");
        goto fail;
    }

    memset(comps, 0, sizeof(comps));

    while (pos + 1 < size) {
        if (data[pos] != 0xFF) {
            JPEG_DEBUG("jpeg: invalid marker prefix at %zu (0x%02X)\n", pos, data[pos]);
            cupidimage_jpeg_set_error(err, errcap, "invalid jpeg marker");
            goto fail;
        }
        while (pos < size && data[pos] == 0xFF) {
            pos++;
        }
        if (pos >= size) {
            break;
        }
        uint8_t marker = data[pos++];
        JPEG_DEBUG("jpeg: marker 0x%02X at %zu\n", marker, pos - 1);
        if (marker == 0xD9) {
            saw_eoi = 1;
            break;
        }

        if (marker == 0xDA) {
            if (pos + 2 > size) {
                cupidimage_jpeg_set_error(err, errcap, "truncated jpeg");
                goto fail;
            }
            uint16_t length = read_be16(data + pos);
            pos += 2;
            if (length < 6 || (size_t)length - 2 > size - pos) {
                cupidimage_jpeg_set_error(err, errcap, "invalid SOS");
                goto fail;
            }
            if (!saw_sof) {
                cupidimage_jpeg_set_error(err, errcap, "missing SOF");
                goto fail;
            }
            size_t hdr_end = pos + length - 2;
            int ns = data[pos++];
            if (ns < 1 || ns > num_comp || length != 6 + 2 * ns) {
                cupidimage_jpeg_set_error(err, errcap, "unsupported scan components");
                goto fail;
            }
            int scan_comp[4];
            unsigned seen_components = 0;
            for (int i = 0; i < ns; i++) {
                int cid = data[pos++];
                int tdta = data[pos++];
                int dc_sel = (tdta >> 4) & 0x0F;
                int ac_sel = tdta & 0x0F;
                if (dc_sel > 3 || ac_sel > 3) {
                    cupidimage_jpeg_set_error(err, errcap, "invalid huffman table selector");
                    goto fail;
                }
                int found = 0;
                for (int c = 0; c < num_comp; c++) {
                    if (comps[c].id == cid) {
                        if (seen_components & (1u << c)) {
                            cupidimage_jpeg_set_error(err, errcap, "duplicate scan component");
                            goto fail;
                        }
                        seen_components |= 1u << c;
                        comps[c].dc_table = dc_sel;
                        comps[c].ac_table = ac_sel;
                        scan_comp[i] = c;
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    cupidimage_jpeg_set_error(err, errcap, "scan component not found");
                    goto fail;
                }
            }
            unsigned blocks_per_mcu = 0;
            for (int i = 0; i < ns; i++)
                blocks_per_mcu += (unsigned)(comps[scan_comp[i]].h * comps[scan_comp[i]].v);
            if (ns > 1 && blocks_per_mcu > 10) {
                cupidimage_jpeg_set_error(err, errcap,
                                          "too many data units in interleaved JPEG MCU");
                goto fail;
            }
            int ss = data[pos++];
            int se = data[pos++];
            int ah_al = data[pos++];
            int ah = (ah_al >> 4) & 0x0F;
            int al = ah_al & 0x0F;
            JPEG_DEBUG("jpeg: SOS ns=%d ss=%d se=%d ah=%d al=%d progressive=%d\n", ns, ss, se, ah,
                       al, progressive);
            if (pos != hdr_end) {
                cupidimage_jpeg_set_error(err, errcap, "invalid SOS length");
                goto fail;
            }

            for (int i = 0; i < ns; i++) {
                jpeg_comp *component = comps + scan_comp[i];
                if (component->tq < 0 || component->tq > 3 || !qvalid[component->tq]) {
                    cupidimage_jpeg_set_error(err, errcap, "missing quant table");
                    goto fail;
                }
                if (!component->have_quantizers) {
                    memcpy(component->quantizers, qtables[component->tq],
                           sizeof(component->quantizers));
                    component->have_quantizers = 1;
                } else if (memcmp(component->quantizers, qtables[component->tq],
                                  sizeof(component->quantizers))) {
                    cupidimage_jpeg_set_error(err, errcap,
                                              "quantization table changed during component scans");
                    goto fail;
                }
            }

            cupidimage_jpeg_default_huffman(dc_tables, ac_tables);

            if (profile && (adobe_transform > 2 || (num_comp == 3 && adobe_transform == 2) ||
                            (num_comp == 4 && adobe_transform == 1))) {
                cupidimage_jpeg_set_error(err, errcap, "unsupported JPEG component transform");
                goto fail;
            }
            saw_sos = 1;
            size_t consumed = 0;
            if (!progressive) {
                if (ss != 0 || se != 63 || ah != 0 || al != 0 ||
                    (sequential_seen & seen_components)) {
                    cupidimage_jpeg_set_error(err, errcap, "invalid sequential JPEG scan");
                    goto fail;
                }
                sequential_seen |= seen_components;
                if (arithmetic || ns != num_comp || sequential_buffered) {
                    sequential_buffered = 1;
                    if (!coeffs_allocated) {
                        coeffs_allocated = 1;
                        if (!jpeg_allocate_coefficients(comps, num_comp, mcu_cols, mcu_rows, err,
                                                        errcap))
                            goto fail;
                    }
                    if (arithmetic) {
                        if (!cupidimage_jpeg_decode_arithmetic_scan(
                                data + pos, size - pos, comps, num_comp, scan_comp, ns, ss, se, ah,
                                al, width, height, mcu_cols, mcu_rows, hmax, vmax, &conditioning,
                                restart_interval, &consumed, err, errcap, 1))
                            goto fail;
                    } else if (!cupidimage_jpeg_decode_coefficient_scan(
                                   data + pos, size - pos, comps, num_comp, scan_comp, ns, ss, se,
                                   ah, al, width, height, mcu_cols, mcu_rows, hmax, vmax, dc_tables,
                                   ac_tables, restart_interval, &consumed, err, errcap, 1))
                        goto fail;
                } else {
                    if (!cupidimage_jpeg_decode_scan(
                            data + pos, size - pos, comps, num_comp, scan_comp, width, height, hmax,
                            vmax, qtables, dc_tables, ac_tables, restart_interval, adobe_transform,
                            &consumed, out, err, errcap, profile))
                        goto fail;
                    if (!profile)
                        return 1;
                }
                pos += consumed;
                continue;
            }

            if (ss > se || se > 63 || al > 13 || ah > 13 || (ah > 0 && al + 1 != ah)) {
                cupidimage_jpeg_set_error(err, errcap, "unsupported JPEG scan");
                goto fail;
            }
            if (ss != 0 || se != 0) {
                if (ns != 1 || ss == 0) {
                    cupidimage_jpeg_set_error(err, errcap, "invalid AC scan");
                    goto fail;
                }
            }

            if (arithmetic) {
                if (!cupidimage_jpeg_decode_arithmetic_scan(
                        data + pos, size - pos, comps, num_comp, scan_comp, ns, ss, se, ah, al,
                        width, height, mcu_cols, mcu_rows, hmax, vmax, &conditioning,
                        restart_interval, &consumed, err, errcap, 0))
                    goto fail;
            } else if (!cupidimage_jpeg_decode_coefficient_scan(
                           data + pos, size - pos, comps, num_comp, scan_comp, ns, ss, se, ah, al,
                           width, height, mcu_cols, mcu_rows, hmax, vmax, dc_tables, ac_tables,
                           restart_interval, &consumed, err, errcap, 0)) {
                goto fail;
            }
            JPEG_DEBUG("jpeg: progressive scan consumed %zu bytes\n", consumed);
            pos += consumed;
            while (pos + 1 < size) {
                if (data[pos] == 0xFF && data[pos + 1] != 0x00 && data[pos + 1] != 0xFF) {
                    break;
                }
                pos++;
            }
            JPEG_DEBUG("jpeg: resynced to %zu\n", pos);
            continue;
        }

        if (pos + 2 > size) {
            cupidimage_jpeg_set_error(err, errcap, "truncated jpeg");
            goto fail;
        }
        uint16_t length = read_be16(data + pos);
        pos += 2;
        if (length < 2 || pos + length - 2 > size) {
            cupidimage_jpeg_set_error(err, errcap, "invalid segment length");
            goto fail;
        }
        const unsigned char *seg = data + pos;

        if (marker == 0xDB) {
            JPEG_DEBUG("jpeg: DQT length=%u\n", length);
            size_t off = 0;
            size_t seg_len = (size_t)length - 2u;
            while (off + 1 < seg_len) {
                uint8_t pq_tq = seg[off++];
                int pq = (pq_tq >> 4) & 0x0F;
                int tq = pq_tq & 0x0F;
                if (tq > 3) {
                    cupidimage_jpeg_set_error(err, errcap, "invalid quant table");
                    goto fail;
                }
                if (pq == 0) {
                    if (off + 64 > seg_len) {
                        cupidimage_jpeg_set_error(err, errcap, "invalid quant table length");
                        goto fail;
                    }
                    for (int i = 0; i < 64; i++) {
                        qtables[tq][cupidimage_jpeg_zigzag[i]] = seg[off++];
                    }
                } else if (pq == 1) {
                    if (off + 128 > seg_len) {
                        cupidimage_jpeg_set_error(err, errcap, "invalid quant table length");
                        goto fail;
                    }
                    for (int i = 0; i < 64; i++) {
                        int v = (seg[off] << 8) | seg[off + 1];
                        qtables[tq][cupidimage_jpeg_zigzag[i]] = v;
                        off += 2;
                    }
                } else {
                    cupidimage_jpeg_set_error(err, errcap, "unsupported quant precision");
                    goto fail;
                }
                qvalid[tq] = 1;
            }
        } else if (marker == 0xC4) {
            JPEG_DEBUG("jpeg: DHT length=%u\n", length);
            size_t off = 0;
            size_t seg_len = (size_t)length - 2u;
            while (off + 1 < seg_len) {
                uint8_t tc_th = seg[off++];
                int tc = (tc_th >> 4) & 0x0F;
                int th = tc_th & 0x0F;
                if (th > 3 || tc > 1) {
                    cupidimage_jpeg_set_error(err, errcap, "invalid huffman table");
                    goto fail;
                }
                if (off + 16 > seg_len) {
                    cupidimage_jpeg_set_error(err, errcap, "invalid huffman table length");
                    goto fail;
                }
                uint8_t bits[16];
                int count = 0;
                for (int i = 0; i < 16; i++) {
                    bits[i] = seg[off++];
                    count += bits[i];
                }
                if (off + (size_t)count > seg_len) {
                    cupidimage_jpeg_set_error(err, errcap, "invalid huffman table length");
                    goto fail;
                }
                jpeg_huff *table = tc == 0 ? &dc_tables[th] : &ac_tables[th];
                if (!cupidimage_jpeg_huff_build(table, bits, seg + off, count)) {
                    cupidimage_jpeg_set_error(err, errcap, "invalid huffman codes");
                    goto fail;
                }
                off += (size_t)count;
            }
        } else if (marker == 0xEE) {
            if (length >= 14) {
                if (seg[0] == 'A' && seg[1] == 'd' && seg[2] == 'o' && seg[3] == 'b' &&
                    seg[4] == 'e') {
                    adobe_transform = seg[11];
                    if (profile)
                        *profile->marker = adobe_transform;
                    JPEG_DEBUG("jpeg: Adobe APP14 transform=%d\n", adobe_transform);
                }
            }
        } else if (marker == 0xCC) {
            if (length < 4 || (length & 1)) {
                cupidimage_jpeg_set_error(err, errcap, "invalid arithmetic conditioning length");
                goto fail;
            }
            for (unsigned offset = 0; offset < (unsigned)length - 2; offset += 2) {
                unsigned selector = seg[offset], value = seg[offset + 1];
                unsigned table = selector & 15;
                if (table > 3 || selector > 0x13 ||
                    (selector < 0x10 ? (value & 15) > (value >> 4) : (value < 1 || value > 63))) {
                    cupidimage_jpeg_set_error(err, errcap, "invalid arithmetic conditioning table");
                    goto fail;
                }
                if (selector < 0x10) {
                    conditioning.lower[table] = value & 15;
                    conditioning.upper[table] = value >> 4;
                } else {
                    conditioning.ac[table] = value;
                }
            }
        } else if (marker == 0xC2 || marker == 0xC0 || marker == 0xC1 || marker == 0xC9 ||
                   marker == 0xCA) {
            if (saw_sof) {
                cupidimage_jpeg_set_error(err, errcap, "multiple JPEG frames unsupported");
                goto fail;
            }
            JPEG_DEBUG("jpeg: %s length=%u\n", marker == 0xC2 ? "SOF2" : "SOF0", length);
            if (length < 8) {
                cupidimage_jpeg_set_error(err, errcap, "invalid SOF");
                goto fail;
            }
            progressive = (marker == 0xC2 || marker == 0xCA);
            arithmetic = (marker == 0xC9 || marker == 0xCA);
            precision = seg[0];
            height = (seg[1] << 8) | seg[2];
            width = (seg[3] << 8) | seg[4];
            num_comp = seg[5];
            if (width == 0 || height == 0) {
                cupidimage_jpeg_set_error(err, errcap, "invalid JPEG dimensions");
                goto fail;
            }
            if (precision != 8) {
                cupidimage_jpeg_set_error(err, errcap, "only 8-bit JPEG supported");
                goto fail;
            }
            if (num_comp != 1 && num_comp != 3 && num_comp != 4 && !(profile && num_comp == 2)) {
                cupidimage_jpeg_set_error(err, errcap, "unsupported component count");
                goto fail;
            }
            if (profile && (width != profile->width || height != profile->height ||
                            num_comp != profile->components)) {
                cupidimage_jpeg_set_error(err, errcap,
                                          "JPEG dimensions or components do not match the image");
                goto fail;
            }
            if (length != 8 + num_comp * 3) {
                cupidimage_jpeg_set_error(err, errcap, "invalid SOF length");
                goto fail;
            }
            hmax = 0;
            vmax = 0;
            for (int i = 0; i < num_comp; i++) {
                int idx = 6 + i * 3;
                comps[i].id = seg[idx];
                for (int j = 0; j < i; j++) {
                    if (comps[j].id == comps[i].id) {
                        cupidimage_jpeg_set_error(err, errcap, "duplicate frame component");
                        goto fail;
                    }
                }
                int hv = seg[idx + 1];
                comps[i].h = (hv >> 4) & 0x0F;
                comps[i].v = hv & 0x0F;
                comps[i].tq = seg[idx + 2] & 0x0F;
                if (comps[i].h < 1 || comps[i].v < 1) {
                    cupidimage_jpeg_set_error(err, errcap, "invalid sampling factors");
                    goto fail;
                }
                if (comps[i].h > hmax)
                    hmax = comps[i].h;
                if (comps[i].v > vmax)
                    vmax = comps[i].v;
            }
            if (hmax > 4 || vmax > 4) {
                cupidimage_jpeg_set_error(err, errcap, "unsupported sampling factors");
                goto fail;
            }
            /* A single-component scan has one block per MCU regardless of
             * the frame's relative sampling factors (T.81, A.2). */
            if (num_comp == 1)
                comps[0].h = comps[0].v = hmax = vmax = 1;
            mcu_cols = (width + hmax * 8 - 1) / (hmax * 8);
            mcu_rows = (height + vmax * 8 - 1) / (vmax * 8);
            if (progressive && !coeffs_allocated) {
                coeffs_allocated = 1;
                if (!jpeg_allocate_coefficients(comps, num_comp, mcu_cols, mcu_rows, err, errcap))
                    goto fail;
            }
            saw_sof = 1;
        } else if (marker == 0xDD) {
            JPEG_DEBUG("jpeg: DRI length=%u\n", length);
            if (length != 4) {
                cupidimage_jpeg_set_error(err, errcap, "invalid DRI");
                goto fail;
            }
            restart_interval = (seg[0] << 8) | seg[1];
        }

        pos += length - 2;
    }

    if ((profile || sequential_buffered) && !saw_eoi) {
        cupidimage_jpeg_set_error(err, errcap, "missing JPEG end marker");
        goto fail;
    }
    if (!progressive && saw_sos && sequential_seen != (1u << num_comp) - 1) {
        cupidimage_jpeg_set_error(err, errcap, "incomplete sequential JPEG components");
        goto fail;
    }
    if (profile && !progressive && saw_sos && !sequential_buffered)
        return 1;
    if ((progressive || sequential_buffered) && saw_sos) {
        if (!cupidimage_jpeg_finish_coefficients(comps, num_comp, width, height, mcu_cols, mcu_rows,
                                                 hmax, vmax, adobe_transform, out, err, errcap,
                                                 profile)) {
            goto fail;
        }
        if (coeffs_allocated) {
            jpeg_free_coeffs(comps, num_comp);
            coeffs_allocated = 0;
        }
        return 1;
    }

    if (!saw_sos) {
        cupidimage_jpeg_set_error(err, errcap, "missing SOS");
    }
fail:
    if (coeffs_allocated) {
        jpeg_free_coeffs(comps, num_comp);
    }
    return 0;
}

int cupidimage_load_jpeg(const unsigned char *data, size_t size, cupidimage_image *out, char *err,
                         size_t errcap) {
    return jpeg_load(data, size, out, err, errcap, NULL);
}

int cupidimage_jpeg_decode_samples(const unsigned char *data, size_t size, int width, int height,
                                   int components, int color_transform, unsigned char **samples) {
    *samples = NULL;
    if (width <= 0 || height <= 0 || components < 1 || components > 4 || color_transform < -1 ||
        color_transform > 1 ||
        (size_t)width > (64u * 1024u * 1024u) / (unsigned)components / (unsigned)height)
        return 0;
    int marker = -1;
    jpeg_sample_profile profile = {width, height, components, color_transform, &marker};
    cupidimage_image image = {0};
    if (!jpeg_load(data, size, &image, NULL, 0, &profile) || marker > 2 ||
        (components == 3 && marker == 2) || (components == 4 && marker == 1)) {
        free(image.rgba);
        return 0;
    }
    cupidimage_jpeg_convert_native(&image, &profile);
    *samples = image.rgba;
    return 1;
}

int cupidimage_load_jpeg_file(const char *path, cupidimage_image *out, char *err, size_t errcap) {
    return cupidimage_load_image_file_via_memory(path, out, err, errcap, cupidimage_load_jpeg);
}
