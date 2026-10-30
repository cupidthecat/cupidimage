#include "tiff_internal.h"

static int tiff_load_page(const unsigned char *data, size_t size, int page_index,
                          cupidimage_image *out, char *err, size_t errcap);

static int tiff_load_page(const unsigned char *data, size_t size, int page_index,
                          cupidimage_image *out, char *err, size_t errcap) {
    tiff_context ctx;
    if (!cupidimage_tiff_parse_header(data, size, &ctx, err, errcap)) {
        return 0;
    }

    if (page_index < 0) {
        cupidimage_set_err(err, errcap, "invalid TIFF page index");
        return 0;
    }

    uint64_t *ifd_list = NULL;
    size_t ifd_count = 0;
    size_t ifd_cap = 0;
    uint64_t *sub_list = NULL;
    size_t sub_count = 0;
    size_t sub_cap = 0;

    uint64_t ifd_offset = ctx.first_ifd;
    for (int guard = 0; guard < 10000 && ifd_offset != 0; guard++) {
        if (!cupidimage_tiff_ifd_list_append(&ifd_list, &ifd_count, &ifd_cap, ifd_offset)) {
            free(ifd_list);
            free(sub_list);
            cupidimage_set_err(err, errcap, "out of memory");
            return 0;
        }
        if (!cupidimage_tiff_collect_subifds(&ctx, ifd_offset, &sub_list, &sub_count, &sub_cap, err,
                                             errcap)) {
            free(ifd_list);
            free(sub_list);
            return 0;
        }
        uint64_t entry_count = 0;
        uint64_t next_ifd = 0;
        if (!cupidimage_tiff_read_ifd_header(&ctx, ifd_offset, &entry_count, &next_ifd, err,
                                             errcap)) {
            free(ifd_list);
            free(sub_list);
            return 0;
        }
        ifd_offset = next_ifd;
    }

    for (size_t i = 0; i < sub_count; i++) {
        if (!cupidimage_tiff_ifd_list_append(&ifd_list, &ifd_count, &ifd_cap, sub_list[i])) {
            free(ifd_list);
            free(sub_list);
            cupidimage_set_err(err, errcap, "out of memory");
            return 0;
        }
    }
    free(sub_list);

    if ((size_t)page_index >= ifd_count) {
        free(ifd_list);
        cupidimage_set_err(err, errcap, "TIFF page index out of range");
        return 0;
    }
    ifd_offset = ifd_list[page_index];
    free(ifd_list);

    tiff_image_info info;
    cupidimage_tiff_info_init(&info);
    if (!cupidimage_tiff_parse_image_info(&ctx, ifd_offset, &info, err, errcap)) {
        cupidimage_tiff_info_free(&info);
        return 0;
    }

    if (info.bits_count == 0) {
        if ((info.photometric == TIFF_PHOTOMETRIC_WHITE_IS_ZERO ||
             info.photometric == TIFF_PHOTOMETRIC_BLACK_IS_ZERO) &&
            info.samples_per_pixel == 1) {
            info.bits_count = 1;
            info.bits_per_sample[0] = 1;
        } else {
            cupidimage_set_err(err, errcap, "missing BitsPerSample");
            cupidimage_tiff_info_free(&info);
            return 0;
        }
    }

    uint16_t bps = info.bits_per_sample[0];
    if (bps == 0) {
        cupidimage_set_err(err, errcap, "unsupported BitsPerSample");
        cupidimage_tiff_info_free(&info);
        return 0;
    }
    if (info.sample_format == 3) {
        if (bps != 32 && bps != 64) {
            cupidimage_set_err(err, errcap, "unsupported float BitsPerSample");
            cupidimage_tiff_info_free(&info);
            return 0;
        }
    } else if (bps > 32) {
        cupidimage_set_err(err, errcap, "unsupported BitsPerSample");
        cupidimage_tiff_info_free(&info);
        return 0;
    }
    for (uint16_t i = 1; i < info.bits_count; i++) {
        if (info.bits_per_sample[i] != bps) {
            cupidimage_set_err(err, errcap, "unsupported TIFF bit depths");
            cupidimage_tiff_info_free(&info);
            return 0;
        }
    }

    if (info.bits_count > 1 && info.bits_count != info.samples_per_pixel) {
        cupidimage_set_err(err, errcap, "invalid BitsPerSample count");
        cupidimage_tiff_info_free(&info);
        return 0;
    }

    if (info.samples_per_pixel == 0 || info.samples_per_pixel > 8) {
        cupidimage_set_err(err, errcap, "invalid SamplesPerPixel");
        cupidimage_tiff_info_free(&info);
        return 0;
    }

    if (info.photometric == TIFF_PHOTOMETRIC_RGB) {
        if (info.samples_per_pixel < 3) {
            cupidimage_set_err(err, errcap, "invalid RGB SamplesPerPixel");
            cupidimage_tiff_info_free(&info);
            return 0;
        }
        if (info.samples_per_pixel > 4) {
            cupidimage_set_err(err, errcap, "unsupported RGB samples");
            cupidimage_tiff_info_free(&info);
            return 0;
        }
    } else if (info.photometric == TIFF_PHOTOMETRIC_CMYK) {
        if (info.samples_per_pixel < 4) {
            cupidimage_set_err(err, errcap, "invalid CMYK SamplesPerPixel");
            cupidimage_tiff_info_free(&info);
            return 0;
        }
    } else if (info.photometric == TIFF_PHOTOMETRIC_YCBCR) {
        if (info.samples_per_pixel < 3) {
            cupidimage_set_err(err, errcap, "invalid YCbCr SamplesPerPixel");
            cupidimage_tiff_info_free(&info);
            return 0;
        }
    } else if (info.photometric == TIFF_PHOTOMETRIC_CIELAB) {
        if (info.samples_per_pixel < 3) {
            cupidimage_set_err(err, errcap, "invalid CIELAB SamplesPerPixel");
            cupidimage_tiff_info_free(&info);
            return 0;
        }
    } else if (info.photometric == TIFF_PHOTOMETRIC_PALETTE) {
        if (bps != 1 && bps != 2 && bps != 4 && bps != 8) {
            cupidimage_set_err(err, errcap, "unsupported palette bit depth");
            cupidimage_tiff_info_free(&info);
            return 0;
        }
        if (info.samples_per_pixel != 1) {
            cupidimage_set_err(err, errcap, "palette TIFF with multiple samples not supported");
            cupidimage_tiff_info_free(&info);
            return 0;
        }
    } else if (info.photometric == TIFF_PHOTOMETRIC_WHITE_IS_ZERO ||
               info.photometric == TIFF_PHOTOMETRIC_BLACK_IS_ZERO) {
        if (info.samples_per_pixel > 2) {
            cupidimage_set_err(err, errcap, "unsupported grayscale samples");
            cupidimage_tiff_info_free(&info);
            return 0;
        }
        if (info.sample_format == 3) {
            if (bps != 32 && bps != 64) {
                cupidimage_set_err(err, errcap, "unsupported grayscale bit depth");
                cupidimage_tiff_info_free(&info);
                return 0;
            }
        } else if (bps != 1 && bps != 2 && bps != 4 && bps != 8 && bps != 16 && bps != 32) {
            cupidimage_set_err(err, errcap, "unsupported grayscale bit depth");
            cupidimage_tiff_info_free(&info);
            return 0;
        }
    } else {
        cupidimage_set_err(err, errcap, "unsupported TIFF photometric");
        cupidimage_tiff_info_free(&info);
        return 0;
    }

    if (info.fill_order != TIFF_FILL_ORDER_MSB2LSB && info.fill_order != TIFF_FILL_ORDER_LSB2MSB) {
        cupidimage_set_err(err, errcap, "invalid TIFF FillOrder");
        cupidimage_tiff_info_free(&info);
        return 0;
    }

    if (info.compression != TIFF_COMPRESSION_NONE && info.compression != TIFF_COMPRESSION_LZW &&
        info.compression != TIFF_COMPRESSION_PACKBITS &&
        info.compression != TIFF_COMPRESSION_DEFLATE &&
        info.compression != TIFF_COMPRESSION_ADOBE_DEFLATE &&
        info.compression != TIFF_COMPRESSION_JPEG &&
        info.compression != TIFF_COMPRESSION_CCITT_RLE &&
        info.compression != TIFF_COMPRESSION_CCITT_G3 &&
        info.compression != TIFF_COMPRESSION_CCITT_G4) {
        cupidimage_set_err(err, errcap, "unsupported TIFF compression");
        cupidimage_tiff_info_free(&info);
        return 0;
    }

    if (info.strip_byte_counts && info.strip_byte_counts_count < info.strip_offsets_count) {
        cupidimage_set_err(err, errcap, "invalid StripByteCounts count");
        cupidimage_tiff_info_free(&info);
        return 0;
    }
    if (info.tile_byte_counts && info.tile_byte_counts_count < info.tile_offsets_count) {
        cupidimage_set_err(err, errcap, "invalid TileByteCounts count");
        cupidimage_tiff_info_free(&info);
        return 0;
    }

    if ((info.compression == TIFF_COMPRESSION_CCITT_RLE ||
         info.compression == TIFF_COMPRESSION_CCITT_G3 ||
         info.compression == TIFF_COMPRESSION_CCITT_G4) &&
        (bps != 1 || info.samples_per_pixel != 1)) {
        cupidimage_set_err(err, errcap, "CCITT compression requires 1-bit bilevel");
        cupidimage_tiff_info_free(&info);
        return 0;
    }

    if (info.compression == TIFF_COMPRESSION_JPEG) {
        uint8_t *jpeg_rgba = NULL;
        uint32_t jpeg_w = 0;
        uint32_t jpeg_h = 0;
        if (!cupidimage_tiff_decode_jpeg_image(&ctx, &info, &jpeg_rgba, &jpeg_w, &jpeg_h, err,
                                               errcap)) {
            cupidimage_tiff_info_free(&info);
            return 0;
        }
        if (!cupidimage_tiff_apply_orientation_rgba(&jpeg_rgba, &jpeg_w, &jpeg_h, info.orientation,
                                                    err, errcap)) {
            free(jpeg_rgba);
            cupidimage_tiff_info_free(&info);
            return 0;
        }
        out->width = jpeg_w;
        out->height = jpeg_h;
        out->rgba = jpeg_rgba;
        cupidimage_tiff_info_free(&info);
        return 1;
    }

    if (info.photometric == TIFF_PHOTOMETRIC_YCBCR &&
        (info.ycbcr_subsampling[0] != 1 || info.ycbcr_subsampling[1] != 1)) {
        size_t rgba_size = 0;
        if (cupidimage_tiff_mul_overflow_size_t((size_t)info.width, (size_t)info.height,
                                                &rgba_size) ||
            cupidimage_tiff_mul_overflow_size_t(rgba_size, 4u, &rgba_size)) {
            cupidimage_set_err(err, errcap, "invalid TIFF image size");
            cupidimage_tiff_info_free(&info);
            return 0;
        }
        uint8_t *rgba = (uint8_t *)malloc(rgba_size);
        if (!rgba) {
            cupidimage_set_err(err, errcap, "out of memory");
            cupidimage_tiff_info_free(&info);
            return 0;
        }
        if (!cupidimage_tiff_decode_ycbcr_subsampled(&ctx, &info, rgba, rgba_size, err, errcap)) {
            free(rgba);
            cupidimage_tiff_info_free(&info);
            return 0;
        }
        uint32_t out_w = info.width;
        uint32_t out_h = info.height;
        if (!cupidimage_tiff_apply_orientation_rgba(&rgba, &out_w, &out_h, info.orientation, err,
                                                    errcap)) {
            free(rgba);
            cupidimage_tiff_info_free(&info);
            return 0;
        }
        out->width = out_w;
        out->height = out_h;
        out->rgba = rgba;
        cupidimage_tiff_info_free(&info);
        return 1;
    }

    size_t row_bytes = 0;
    size_t bits_per_row = 0;
    if (cupidimage_tiff_mul_overflow_size_t((size_t)info.width, (size_t)info.samples_per_pixel,
                                            &bits_per_row) ||
        cupidimage_tiff_mul_overflow_size_t(bits_per_row, (size_t)bps, &bits_per_row)) {
        cupidimage_set_err(err, errcap, "invalid TIFF row size");
        cupidimage_tiff_info_free(&info);
        return 0;
    }
    row_bytes = (bits_per_row + 7u) / 8u;

    size_t raw_size = 0;
    if (cupidimage_tiff_mul_overflow_size_t(row_bytes, (size_t)info.height, &raw_size)) {
        cupidimage_set_err(err, errcap, "invalid TIFF image size");
        cupidimage_tiff_info_free(&info);
        return 0;
    }

    size_t bytes_per_pixel = 0;
    if ((bps & 7u) == 0) {
        if (cupidimage_tiff_mul_overflow_size_t((size_t)info.samples_per_pixel, (size_t)(bps / 8u),
                                                &bytes_per_pixel)) {
            cupidimage_set_err(err, errcap, "invalid TIFF pixel size");
            cupidimage_tiff_info_free(&info);
            return 0;
        }
    }

    if (info.planar_config == TIFF_PLANAR_SEPARATE && bytes_per_pixel == 0) {
        cupidimage_set_err(err, errcap, "planar TIFF requires byte-aligned samples");
        cupidimage_tiff_info_free(&info);
        return 0;
    }

    size_t rgba_size = 0;
    if (cupidimage_tiff_mul_overflow_size_t((size_t)info.width, (size_t)info.height, &rgba_size) ||
        cupidimage_tiff_mul_overflow_size_t(rgba_size, 4u, &rgba_size)) {
        cupidimage_set_err(err, errcap, "invalid TIFF image size");
        cupidimage_tiff_info_free(&info);
        return 0;
    }

    uint8_t *raw = (uint8_t *)malloc(raw_size);
    if (!raw) {
        cupidimage_set_err(err, errcap, "out of memory");
        cupidimage_tiff_info_free(&info);
        return 0;
    }

    if (!cupidimage_tiff_decode_image(&ctx, &info, raw, raw_size, row_bytes, bytes_per_pixel, err,
                                      errcap)) {
        free(raw);
        cupidimage_tiff_info_free(&info);
        return 0;
    }

    if (info.predictor == 2 && info.planar_config == TIFF_PLANAR_CHUNKY &&
        (info.tile_offsets == NULL || info.tile_offsets_count == 0)) {
        if (!cupidimage_tiff_apply_predictor(&ctx, &info, raw, row_bytes, err, errcap)) {
            free(raw);
            cupidimage_tiff_info_free(&info);
            return 0;
        }
    }

    uint8_t *rgba = (uint8_t *)malloc(rgba_size);
    if (!rgba) {
        free(raw);
        cupidimage_tiff_info_free(&info);
        cupidimage_set_err(err, errcap, "out of memory");
        return 0;
    }

    if (!cupidimage_tiff_convert_to_rgba(&ctx, &info, raw, row_bytes, rgba, rgba_size, err,
                                         errcap)) {
        free(raw);
        free(rgba);
        cupidimage_tiff_info_free(&info);
        return 0;
    }

    free(raw);
    uint32_t out_w = info.width;
    uint32_t out_h = info.height;
    if (!cupidimage_tiff_apply_orientation_rgba(&rgba, &out_w, &out_h, info.orientation, err,
                                                errcap)) {
        free(rgba);
        cupidimage_tiff_info_free(&info);
        return 0;
    }
    cupidimage_tiff_info_free(&info);

    out->width = out_w;
    out->height = out_h;
    out->rgba = rgba;
    return 1;
}

int cupidimage_load_tiff_page(const unsigned char *data, size_t size, cupidimage_image *out,
                              int page_index, char *err, size_t errcap) {
    if (!data || !out) {
        cupidimage_set_err(err, errcap, "invalid arguments");
        return 0;
    }

    memset(out, 0, sizeof(*out));
    return tiff_load_page(data, size, page_index, out, err, errcap);
}

int cupidimage_load_tiff(const unsigned char *data, size_t size, cupidimage_image *out, char *err,
                         size_t errcap) {
    return cupidimage_load_tiff_page(data, size, out, 0, err, errcap);
}

int cupidimage_get_tiff_page_count(const unsigned char *data, size_t size, int *count, char *err,
                                   size_t errcap) {
    if (!data || !count) {
        cupidimage_set_err(err, errcap, "invalid arguments");
        return 0;
    }

    tiff_context ctx;
    if (!cupidimage_tiff_parse_header(data, size, &ctx, err, errcap)) {
        return 0;
    }

    uint64_t *ifd_list = NULL;
    size_t ifd_count = 0;
    size_t ifd_cap = 0;
    uint64_t *sub_list = NULL;
    size_t sub_count = 0;
    size_t sub_cap = 0;

    uint64_t ifd_offset = ctx.first_ifd;
    for (int guard = 0; guard < 10000 && ifd_offset != 0; guard++) {
        if (!cupidimage_tiff_ifd_list_append(&ifd_list, &ifd_count, &ifd_cap, ifd_offset)) {
            free(ifd_list);
            free(sub_list);
            cupidimage_set_err(err, errcap, "out of memory");
            return 0;
        }
        if (!cupidimage_tiff_collect_subifds(&ctx, ifd_offset, &sub_list, &sub_count, &sub_cap, err,
                                             errcap)) {
            free(ifd_list);
            free(sub_list);
            return 0;
        }
        uint64_t entry_count = 0;
        uint64_t next_ifd = 0;
        if (!cupidimage_tiff_read_ifd_header(&ctx, ifd_offset, &entry_count, &next_ifd, err,
                                             errcap)) {
            free(ifd_list);
            free(sub_list);
            return 0;
        }
        ifd_offset = next_ifd;
    }

    for (size_t i = 0; i < sub_count; i++) {
        if (!cupidimage_tiff_ifd_list_append(&ifd_list, &ifd_count, &ifd_cap, sub_list[i])) {
            free(ifd_list);
            free(sub_list);
            cupidimage_set_err(err, errcap, "out of memory");
            return 0;
        }
    }
    free(sub_list);

    *count = (int)ifd_count;
    free(ifd_list);
    return 1;
}

int cupidimage_load_tiff_file(const char *path, cupidimage_image *out, char *err, size_t errcap) {
    return cupidimage_load_image_file_via_memory(path, out, err, errcap, cupidimage_load_tiff);
}
