#include "tiff_internal.h"

static size_t tiff_type_size(uint16_t type);
static int tiff_read_entry(const tiff_context *ctx, const uint8_t *entry_ptr, tiff_entry *entry,
                           char *err, size_t errcap);
static int tiff_entry_get_u64(const tiff_context *ctx, const tiff_entry *entry, uint64_t index,
                              uint64_t *out);

int cupidimage_tiff_mul_overflow_size_t(size_t a, size_t b, size_t *out) {
    if (a == 0 || b == 0) {
        *out = 0;
        return 0;
    }
    if (a > SIZE_MAX / b) {
        return 1;
    }
    *out = a * b;
    return 0;
}

int cupidimage_tiff_range_valid(const tiff_context *ctx, uint64_t offset, uint64_t length) {
    return offset <= ctx->size && length <= ctx->size - offset;
}

uint16_t cupidimage_tiff_read_u16(const tiff_context *ctx, const uint8_t *p) {
    if (ctx->big_endian) {
        return (uint16_t)((p[0] << 8) | p[1]);
    }
    return (uint16_t)(p[0] | (p[1] << 8));
}

void cupidimage_tiff_write_u16(const tiff_context *ctx, uint8_t *p, uint16_t v) {
    if (ctx->big_endian) {
        p[0] = (uint8_t)(v >> 8);
        p[1] = (uint8_t)(v & 0xFFu);
    } else {
        p[0] = (uint8_t)(v & 0xFFu);
        p[1] = (uint8_t)(v >> 8);
    }
}

uint32_t cupidimage_tiff_read_u32(const tiff_context *ctx, const uint8_t *p) {
    if (ctx->big_endian) {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
               (uint32_t)p[3];
    }
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint64_t cupidimage_tiff_read_u64(const tiff_context *ctx, const uint8_t *p) {
    if (ctx->big_endian) {
        return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) |
               ((uint64_t)p[3] << 32) | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
               ((uint64_t)p[6] << 8) | (uint64_t)p[7];
    }
    return (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

static size_t tiff_type_size(uint16_t type) {
    switch (type) {
        case 1: /* BYTE */
        case 2: /* ASCII */
        case 6: /* SBYTE */
        case 7: /* UNDEFINED */
            return 1;
        case 3: /* SHORT */
        case 8: /* SSHORT */
            return 2;
        case 4:  /* LONG */
        case 9:  /* SLONG */
        case 11: /* FLOAT */
            return 4;
        case 5:  /* RATIONAL */
        case 10: /* SRATIONAL */
        case 12: /* DOUBLE */
        case 16: /* LONG8 */
        case 17: /* SLONG8 */
        case 18: /* IFD8 */
            return 8;
        default:
            return 0;
    }
}

static int tiff_read_entry(const tiff_context *ctx, const uint8_t *entry_ptr, tiff_entry *entry,
                           char *err, size_t errcap) {
    entry->tag = cupidimage_tiff_read_u16(ctx, entry_ptr);
    entry->type = cupidimage_tiff_read_u16(ctx, entry_ptr + 2);
    if (ctx->big_tiff) {
        entry->count = cupidimage_tiff_read_u64(ctx, entry_ptr + 4);
    } else {
        entry->count = cupidimage_tiff_read_u32(ctx, entry_ptr + 4);
    }

    size_t type_size = tiff_type_size(entry->type);
    if (type_size == 0) {
        entry->value_ptr = NULL;
        entry->value_size = 0;
        return 1;
    }

    if (entry->count > 0 && entry->count > (uint64_t)(SIZE_MAX / type_size)) {
        cupidimage_set_err(err, errcap, "TIFF tag value too large");
        return 0;
    }
    entry->value_size = (size_t)entry->count * type_size;

    uint64_t value_offset = 0;
    if (ctx->big_tiff) {
        value_offset = cupidimage_tiff_read_u64(ctx, entry_ptr + 12);
    } else {
        value_offset = cupidimage_tiff_read_u32(ctx, entry_ptr + 8);
    }

    if (entry->value_size <= ctx->value_size) {
        entry->value_ptr = entry_ptr + (ctx->big_tiff ? 12 : 8);
        return 1;
    }

    if (!cupidimage_tiff_range_valid(ctx, value_offset, entry->value_size)) {
        cupidimage_set_err(err, errcap, "TIFF tag value offset out of range");
        return 0;
    }
    entry->value_ptr = ctx->data + (size_t)value_offset;
    return 1;
}

static int tiff_entry_get_u64(const tiff_context *ctx, const tiff_entry *entry, uint64_t index,
                              uint64_t *out) {
    if (!entry->value_ptr || index >= entry->count) {
        return 0;
    }

    size_t type_size = tiff_type_size(entry->type);
    size_t offset = (size_t)index * type_size;
    if (offset + type_size > entry->value_size) {
        return 0;
    }
    const uint8_t *p = entry->value_ptr + offset;

    switch (entry->type) {
        case 1: /* BYTE */
        case 2: /* ASCII */
        case 6: /* SBYTE */
        case 7: /* UNDEFINED */
            *out = p[0];
            return 1;
        case 3: /* SHORT */
        case 8: /* SSHORT */
        {
            uint16_t v = cupidimage_tiff_read_u16(ctx, p);
            if (ctx->big_endian && entry->count == 1 && entry->value_size <= ctx->value_size) {
                uint16_t tail = (uint16_t)((p[2] << 8) | p[3]);
                if (v == 0 && tail != 0) {
                    v = tail;
                }
            }
            *out = v;
            return 1;
        }
        case 4:  /* LONG */
        case 9:  /* SLONG */
        case 11: /* FLOAT */
            *out = cupidimage_tiff_read_u32(ctx, p);
            return 1;
        case 5:  /* RATIONAL */
        case 10: /* SRATIONAL */
        case 12: /* DOUBLE */
        case 16: /* LONG8 */
        case 17: /* SLONG8 */
        case 18: /* IFD8 */
            *out = cupidimage_tiff_read_u64(ctx, p);
            return 1;
        default:
            return 0;
    }
}

void cupidimage_tiff_info_init(tiff_image_info *info) {
    memset(info, 0, sizeof(*info));
    info->samples_per_pixel = 1;
    info->compression = TIFF_COMPRESSION_NONE;
    info->photometric = 0xFFFFu;
    info->planar_config = TIFF_PLANAR_CHUNKY;
    info->fill_order = TIFF_FILL_ORDER_MSB2LSB;
    info->predictor = 1;
    info->sample_format = TIFF_SAMPLE_FORMAT_UINT;
    info->orientation = 1;
    info->ycbcr_subsampling[0] = 2;
    info->ycbcr_subsampling[1] = 2;
    info->ycbcr_positioning = 1;
}

void cupidimage_tiff_info_free(tiff_image_info *info) {
    free(info->strip_offsets);
    free(info->strip_byte_counts);
    free(info->tile_offsets);
    free(info->tile_byte_counts);
    free(info->color_map);
    info->strip_offsets = NULL;
    info->strip_byte_counts = NULL;
    info->tile_offsets = NULL;
    info->tile_byte_counts = NULL;
    info->color_map = NULL;
}

int cupidimage_tiff_parse_header(const unsigned char *data, size_t size, tiff_context *ctx,
                                 char *err, size_t errcap) {
    if (size < 8) {
        cupidimage_set_err(err, errcap, "truncated TIFF header");
        return 0;
    }

    if (data[0] == 'I' && data[1] == 'I') {
        ctx->big_endian = 0;
    } else if (data[0] == 'M' && data[1] == 'M') {
        ctx->big_endian = 1;
    } else {
        cupidimage_set_err(err, errcap, "invalid TIFF byte order");
        return 0;
    }

    ctx->data = data;
    ctx->size = size;

    uint16_t version = cupidimage_tiff_read_u16(ctx, data + 2);
    if (version == 42) {
        ctx->big_tiff = 0;
        ctx->entry_size = 12;
        ctx->value_size = 4;
        uint32_t off = cupidimage_tiff_read_u32(ctx, data + 4);
        if (off == 0 || off >= size) {
            cupidimage_set_err(err, errcap, "invalid TIFF IFD offset");
            return 0;
        }
        ctx->first_ifd = off;
        return 1;
    }

    if (version == 43) {
        if (size < 16) {
            cupidimage_set_err(err, errcap, "truncated BigTIFF header");
            return 0;
        }
        uint16_t bytesize = cupidimage_tiff_read_u16(ctx, data + 4);
        uint16_t zero = cupidimage_tiff_read_u16(ctx, data + 6);
        if (bytesize != 8 || zero != 0) {
            cupidimage_set_err(err, errcap, "invalid BigTIFF header");
            return 0;
        }
        uint64_t off = cupidimage_tiff_read_u64(ctx, data + 8);
        if (off == 0 || off >= size) {
            cupidimage_set_err(err, errcap, "invalid BigTIFF IFD offset");
            return 0;
        }
        ctx->big_tiff = 1;
        ctx->entry_size = 20;
        ctx->value_size = 8;
        ctx->first_ifd = off;
        return 1;
    }

    cupidimage_set_err(err, errcap, "unsupported TIFF version");
    return 0;
}

int cupidimage_tiff_read_ifd_header(const tiff_context *ctx, uint64_t offset, uint64_t *entry_count,
                                    uint64_t *next_ifd, char *err, size_t errcap) {
    if (offset >= ctx->size) {
        cupidimage_set_err(err, errcap, "invalid TIFF IFD offset");
        return 0;
    }

    uint64_t count = 0;
    size_t count_size = ctx->big_tiff ? 8 : 2;
    if (!cupidimage_tiff_range_valid(ctx, offset, count_size)) {
        cupidimage_set_err(err, errcap, "truncated TIFF IFD");
        return 0;
    }

    if (ctx->big_tiff) {
        count = cupidimage_tiff_read_u64(ctx, ctx->data + offset);
    } else {
        count = cupidimage_tiff_read_u16(ctx, ctx->data + offset);
    }

    uint64_t entries_size = count * (uint64_t)ctx->entry_size;
    uint64_t entries_start = offset + count_size;
    uint64_t next_offset_size = ctx->big_tiff ? 8 : 4;

    if (count > 0 && entries_size / (uint64_t)ctx->entry_size != count) {
        cupidimage_set_err(err, errcap, "TIFF IFD entry overflow");
        return 0;
    }

    if (!cupidimage_tiff_range_valid(ctx, entries_start, entries_size) ||
        next_offset_size > ctx->size - entries_start - entries_size) {
        cupidimage_set_err(err, errcap, "truncated TIFF IFD entries");
        return 0;
    }
    uint64_t after_entries = entries_start + entries_size;

    uint64_t next = 0;
    if (ctx->big_tiff) {
        next = cupidimage_tiff_read_u64(ctx, ctx->data + after_entries);
    } else {
        next = cupidimage_tiff_read_u32(ctx, ctx->data + after_entries);
    }
    if (next != 0 && next >= ctx->size) {
        cupidimage_set_err(err, errcap, "invalid TIFF next IFD offset");
        return 0;
    }

    *entry_count = count;
    *next_ifd = next;
    return 1;
}

int cupidimage_tiff_ifd_list_append(uint64_t **list, size_t *count, size_t *cap, uint64_t off) {
    if (*count >= *cap) {
        size_t next_cap = (*cap == 0) ? 8 : (*cap * 2u);
        uint64_t *next = (uint64_t *)realloc(*list, next_cap * sizeof(uint64_t));
        if (!next) {
            return 0;
        }
        *list = next;
        *cap = next_cap;
    }
    (*list)[(*count)++] = off;
    return 1;
}

int cupidimage_tiff_collect_subifds(const tiff_context *ctx, uint64_t ifd_offset, uint64_t **list,
                                    size_t *count, size_t *cap, char *err, size_t errcap) {
    uint64_t entry_count = 0;
    uint64_t next_ifd = 0;
    if (!cupidimage_tiff_read_ifd_header(ctx, ifd_offset, &entry_count, &next_ifd, err, errcap)) {
        return 0;
    }
    (void)next_ifd;

    uint64_t entries_start = ifd_offset + (ctx->big_tiff ? 8 : 2);
    for (uint64_t i = 0; i < entry_count; i++) {
        uint64_t entry_off = entries_start + i * (uint64_t)ctx->entry_size;
        if (entry_off + ctx->entry_size > ctx->size) {
            cupidimage_set_err(err, errcap, "truncated TIFF IFD entry");
            return 0;
        }
        tiff_entry entry;
        if (!tiff_read_entry(ctx, ctx->data + entry_off, &entry, err, errcap)) {
            return 0;
        }
        if (entry.tag != TIFF_TAG_SUB_IFD) {
            continue;
        }
        for (uint64_t j = 0; j < entry.count; j++) {
            uint64_t off = 0;
            if (!tiff_entry_get_u64(ctx, &entry, j, &off)) {
                cupidimage_set_err(err, errcap, "invalid SubIFD");
                return 0;
            }
            if (off == 0 || off >= ctx->size) {
                cupidimage_set_err(err, errcap, "invalid SubIFD offset");
                return 0;
            }
            if (!cupidimage_tiff_ifd_list_append(list, count, cap, off)) {
                cupidimage_set_err(err, errcap, "out of memory");
                return 0;
            }
        }
    }
    return 1;
}

int cupidimage_tiff_parse_image_info(const tiff_context *ctx, uint64_t ifd_offset,
                                     tiff_image_info *info, char *err, size_t errcap) {
    uint64_t entry_count = 0;
    uint64_t next_ifd = 0;
    if (!cupidimage_tiff_read_ifd_header(ctx, ifd_offset, &entry_count, &next_ifd, err, errcap)) {
        return 0;
    }
    (void)next_ifd;

    uint64_t entries_start = ifd_offset + (ctx->big_tiff ? 8 : 2);
    for (uint64_t i = 0; i < entry_count; i++) {
        uint64_t entry_off = entries_start + i * (uint64_t)ctx->entry_size;
        if (entry_off + ctx->entry_size > ctx->size) {
            cupidimage_set_err(err, errcap, "truncated TIFF IFD entry");
            return 0;
        }
        tiff_entry entry;
        if (!tiff_read_entry(ctx, ctx->data + entry_off, &entry, err, errcap)) {
            return 0;
        }

        switch (entry.tag) {
            case TIFF_TAG_IMAGE_WIDTH: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->width = (v > UINT32_MAX) ? 0 : (uint32_t)v;
                }
                break;
            }
            case TIFF_TAG_IMAGE_LENGTH: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->height = (v > UINT32_MAX) ? 0 : (uint32_t)v;
                }
                break;
            }
            case TIFF_TAG_BITS_PER_SAMPLE: {
                if (entry.count > 8) {
                    cupidimage_set_err(err, errcap, "too many BitsPerSample values");
                    return 0;
                }
                info->bits_count = (uint16_t)entry.count;
                for (uint64_t j = 0; j < entry.count; j++) {
                    uint64_t v = 0;
                    if (!tiff_entry_get_u64(ctx, &entry, j, &v)) {
                        cupidimage_set_err(err, errcap, "invalid BitsPerSample");
                        return 0;
                    }
                    info->bits_per_sample[j] = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_COMPRESSION: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->compression = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_PHOTOMETRIC: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->photometric = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_FILL_ORDER: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->fill_order = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_STRIP_OFFSETS: {
                free(info->strip_offsets);
                info->strip_offsets = NULL;
                info->strip_offsets_count = entry.count;
                if (entry.count == 0 || entry.count > SIZE_MAX / sizeof(uint64_t)) {
                    cupidimage_set_err(err, errcap, "invalid StripOffsets");
                    return 0;
                }
                info->strip_offsets = (uint64_t *)malloc((size_t)entry.count * sizeof(uint64_t));
                if (!info->strip_offsets) {
                    cupidimage_set_err(err, errcap, "out of memory");
                    return 0;
                }
                for (uint64_t j = 0; j < entry.count; j++) {
                    uint64_t v = 0;
                    if (!tiff_entry_get_u64(ctx, &entry, j, &v)) {
                        cupidimage_set_err(err, errcap, "invalid StripOffsets");
                        return 0;
                    }
                    info->strip_offsets[j] = v;
                }
                break;
            }
            case TIFF_TAG_STRIP_BYTE_COUNTS: {
                free(info->strip_byte_counts);
                info->strip_byte_counts = NULL;
                if (entry.count == 0 || entry.count > SIZE_MAX / sizeof(uint64_t)) {
                    cupidimage_set_err(err, errcap, "invalid StripByteCounts");
                    return 0;
                }
                info->strip_byte_counts_count = entry.count;
                info->strip_byte_counts =
                    (uint64_t *)malloc((size_t)entry.count * sizeof(uint64_t));
                if (!info->strip_byte_counts) {
                    cupidimage_set_err(err, errcap, "out of memory");
                    return 0;
                }
                for (uint64_t j = 0; j < entry.count; j++) {
                    uint64_t v = 0;
                    if (!tiff_entry_get_u64(ctx, &entry, j, &v)) {
                        cupidimage_set_err(err, errcap, "invalid StripByteCounts");
                        return 0;
                    }
                    info->strip_byte_counts[j] = v;
                }
                break;
            }
            case TIFF_TAG_SAMPLES_PER_PIXEL: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->samples_per_pixel = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_ROWS_PER_STRIP: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->rows_per_strip = (v > UINT32_MAX) ? 0 : (uint32_t)v;
                }
                break;
            }
            case TIFF_TAG_PLANAR_CONFIGURATION: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->planar_config = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_GROUP3_OPTIONS: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->group3_options = (uint32_t)v;
                }
                break;
            }
            case TIFF_TAG_GROUP4_OPTIONS: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->group4_options = (uint32_t)v;
                }
                break;
            }
            case TIFF_TAG_PREDICTOR: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->predictor = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_COLOR_MAP: {
                free(info->color_map);
                info->color_map = NULL;
                if (entry.count == 0 || entry.count > SIZE_MAX / sizeof(uint16_t)) {
                    cupidimage_set_err(err, errcap, "invalid ColorMap");
                    return 0;
                }
                info->color_map = (uint16_t *)malloc((size_t)entry.count * sizeof(uint16_t));
                if (!info->color_map) {
                    cupidimage_set_err(err, errcap, "out of memory");
                    return 0;
                }
                info->color_map_count = entry.count;
                for (uint64_t j = 0; j < entry.count; j++) {
                    uint64_t v = 0;
                    if (!tiff_entry_get_u64(ctx, &entry, j, &v)) {
                        cupidimage_set_err(err, errcap, "invalid ColorMap");
                        return 0;
                    }
                    info->color_map[j] = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_TILE_WIDTH: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->tile_width = (v > UINT32_MAX) ? 0 : (uint32_t)v;
                }
                break;
            }
            case TIFF_TAG_TILE_LENGTH: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->tile_length = (v > UINT32_MAX) ? 0 : (uint32_t)v;
                }
                break;
            }
            case TIFF_TAG_TILE_OFFSETS: {
                free(info->tile_offsets);
                info->tile_offsets = NULL;
                info->tile_offsets_count = entry.count;
                if (entry.count == 0 || entry.count > SIZE_MAX / sizeof(uint64_t)) {
                    cupidimage_set_err(err, errcap, "invalid TileOffsets");
                    return 0;
                }
                info->tile_offsets = (uint64_t *)malloc((size_t)entry.count * sizeof(uint64_t));
                if (!info->tile_offsets) {
                    cupidimage_set_err(err, errcap, "out of memory");
                    return 0;
                }
                for (uint64_t j = 0; j < entry.count; j++) {
                    uint64_t v = 0;
                    if (!tiff_entry_get_u64(ctx, &entry, j, &v)) {
                        cupidimage_set_err(err, errcap, "invalid TileOffsets");
                        return 0;
                    }
                    info->tile_offsets[j] = v;
                }
                break;
            }
            case TIFF_TAG_TILE_BYTE_COUNTS: {
                free(info->tile_byte_counts);
                info->tile_byte_counts = NULL;
                info->tile_byte_counts_count = entry.count;
                if (entry.count == 0 || entry.count > SIZE_MAX / sizeof(uint64_t)) {
                    cupidimage_set_err(err, errcap, "invalid TileByteCounts");
                    return 0;
                }
                info->tile_byte_counts = (uint64_t *)malloc((size_t)entry.count * sizeof(uint64_t));
                if (!info->tile_byte_counts) {
                    cupidimage_set_err(err, errcap, "out of memory");
                    return 0;
                }
                for (uint64_t j = 0; j < entry.count; j++) {
                    uint64_t v = 0;
                    if (!tiff_entry_get_u64(ctx, &entry, j, &v)) {
                        cupidimage_set_err(err, errcap, "invalid TileByteCounts");
                        return 0;
                    }
                    info->tile_byte_counts[j] = v;
                }
                break;
            }
            case TIFF_TAG_EXTRA_SAMPLES: {
                if (entry.count > 8) {
                    cupidimage_set_err(err, errcap, "too many ExtraSamples values");
                    return 0;
                }
                info->extra_count = (uint16_t)entry.count;
                for (uint64_t j = 0; j < entry.count; j++) {
                    uint64_t v = 0;
                    if (!tiff_entry_get_u64(ctx, &entry, j, &v)) {
                        cupidimage_set_err(err, errcap, "invalid ExtraSamples");
                        return 0;
                    }
                    info->extra_samples[j] = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_SAMPLE_FORMAT: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->sample_format = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_ORIENTATION: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->orientation = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_YCBCR_SUBSAMPLING: {
                uint64_t v0 = 0;
                uint64_t v1 = 0;
                if (entry.count >= 2 && tiff_entry_get_u64(ctx, &entry, 0, &v0) &&
                    tiff_entry_get_u64(ctx, &entry, 1, &v1)) {
                    info->ycbcr_subsampling[0] = (uint16_t)v0;
                    info->ycbcr_subsampling[1] = (uint16_t)v1;
                }
                break;
            }
            case TIFF_TAG_YCBCR_POSITIONING: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->ycbcr_positioning = (uint16_t)v;
                }
                break;
            }
            case TIFF_TAG_JPEG_TABLES: {
                info->jpeg_tables = entry.value_ptr;
                info->jpeg_tables_size = entry.value_size;
                break;
            }
            case TIFF_TAG_JPEG_INTERCHANGE_FORMAT: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->jpeg_if_offset = v;
                }
                break;
            }
            case TIFF_TAG_JPEG_INTERCHANGE_FORMAT_LENGTH: {
                uint64_t v = 0;
                if (tiff_entry_get_u64(ctx, &entry, 0, &v)) {
                    info->jpeg_if_bytecount = v;
                }
                break;
            }
            default:
                break;
        }
    }

    if (info->width == 0 || info->height == 0) {
        cupidimage_set_err(err, errcap, "invalid TIFF dimensions");
        return 0;
    }
    if (info->photometric == 0xFFFFu) {
        cupidimage_set_err(err, errcap, "missing PhotometricInterpretation");
        return 0;
    }
    if (info->planar_config != TIFF_PLANAR_CHUNKY && info->planar_config != TIFF_PLANAR_SEPARATE) {
        cupidimage_set_err(err, errcap, "invalid TIFF planar configuration");
        return 0;
    }
    if (info->predictor != 1 && info->predictor != 2) {
        cupidimage_set_err(err, errcap, "TIFF predictor not supported");
        return 0;
    }
    if (info->sample_format == 0) {
        info->sample_format = TIFF_SAMPLE_FORMAT_UINT;
    }
    if (info->sample_format != TIFF_SAMPLE_FORMAT_UINT && info->sample_format != 2 &&
        info->sample_format != 3) {
        cupidimage_set_err(err, errcap, "TIFF sample format not supported");
        return 0;
    }
    if ((info->tile_offsets == NULL || info->tile_offsets_count == 0) &&
        (info->strip_offsets == NULL || info->strip_offsets_count == 0)) {
        cupidimage_set_err(err, errcap, "missing StripOffsets/TileOffsets");
        return 0;
    }

    if (info->rows_per_strip == 0) {
        info->rows_per_strip = info->height;
    }
    if (info->rows_per_strip > info->height) {
        info->rows_per_strip = info->height;
    }

    if (info->compression != TIFF_COMPRESSION_NONE) {
        if (info->tile_offsets && info->tile_offsets_count > 0) {
            if (info->tile_byte_counts == NULL) {
                cupidimage_set_err(err, errcap, "missing TileByteCounts");
                return 0;
            }
        } else if (info->strip_byte_counts == NULL) {
            cupidimage_set_err(err, errcap, "missing StripByteCounts");
            return 0;
        }
    }

    return 1;
}
