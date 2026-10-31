#include "boxes.h"
#include <string.h>

static void jpx_channel_type(const pdf_jpx_file *file, unsigned channel, unsigned *precision,
                             int *is_signed) {
    const pdf_jpx_channel *mapping = file->header.mapping + channel;
    if (mapping->type) {
        unsigned bits = file->header.palette.bits[mapping->column];
        *precision = (bits & 127) + 1;
        *is_signed = (bits & 128) != 0;
    } else {
        const pdf_jpx_plane *plane = file->image.planes + mapping->component;
        *precision = plane->precision;
        *is_signed = plane->is_signed != 0;
    }
}

int cupidimage_pdf_jpx_channel_sample(const pdf_jpx_file *file, unsigned channel, unsigned x,
                                      unsigned y, uint64_t *value, unsigned *precision,
                                      int *is_signed) {
    if (channel >= file->channels || x >= file->image.width || y >= file->image.height)
        return 0;
    const pdf_jpx_channel *mapping = file->header.mapping + channel;
    const pdf_jpx_plane *plane = file->image.planes + mapping->component;
    uint64_t gx = (uint64_t)file->image.x0 + x, gy = (uint64_t)file->image.y0 + y;
    uint64_t cx = gx / plane->dx, cy = gy / plane->dy;
    cx = cx < plane->x0 ? 0 : cx - plane->x0;
    cy = cy < plane->y0 ? 0 : cy - plane->y0;
    if (cx >= plane->width)
        cx = plane->width - 1;
    if (cy >= plane->height)
        cy = plane->height - 1;
    uint32_t sample = plane->samples[(size_t)cy * plane->width + (size_t)cx];
    jpx_channel_type(file, channel, precision, is_signed);
    if (!mapping->type) {
        *value = sample;
        return 1;
    }
    const pdf_jpx_palette *palette = &file->header.palette;
    int64_t index = sample;
    if (plane->is_signed)
        index -= INT64_C(1) << (plane->precision - 1);
    /* Bound malformed palette indices to the nearest defined entry. */
    if (index < 0)
        index = 0;
    if ((uint64_t)index >= palette->entries)
        index = palette->entries - 1;
    const unsigned char *p =
        palette->data.data + (size_t)index * palette->row_bytes + palette->offsets[mapping->column];
    uint64_t result = 0;
    for (unsigned i = 0; i < (*precision + 7) / 8; i++)
        result = result * 256 + p[i];
    if (*is_signed)
        result ^= UINT64_C(1) << (*precision - 1);
    *value = result;
    return 1;
}

static int jpx_alpha_type(const pdf_jpx_file *file, unsigned channel) {
    unsigned precision;
    int is_signed;
    jpx_channel_type(file, channel, &precision, &is_signed);
    return !is_signed;
}

static int jpx_opacity(pdf_jpx_file *file) {
    pdf_jpx_slice source = file->header.opacity;
    const unsigned char *p = source.data;
    if (p[0] < 2) {
        if (file->channels < 2 || !jpx_alpha_type(file, file->channels - 1))
            return 0;
        file->ordinary = file->channels - 1;
        file->opacity_channels = 1;
        for (unsigned c = 0; c < file->ordinary; c++) {
            file->color_order[c] = (uint16_t)c;
            file->alpha[c] = (uint16_t)(file->channels - 1);
            file->alpha_type[c] = p[0] == 0 ? 1 : 2;
        }
        return 1;
    }
    if (source.length < 2 || p[1] != file->channels)
        return 0;
    file->ordinary = file->channels;
    file->chroma_key = 1;
    size_t at = 2;
    for (unsigned c = 0; c < file->channels; c++) {
        unsigned precision;
        int is_signed;
        jpx_channel_type(file, c, &precision, &is_signed);
        unsigned bytes = (precision + 7) / 8;
        if (bytes > source.length - at)
            return 0;
        uint64_t value = 0, mask = (UINT64_C(1) << precision) - 1;
        for (unsigned i = 0; i < bytes; i++)
            value = value * 256 + p[at++];
        unsigned padding = bytes * 8 - precision;
        uint64_t expected = is_signed && (value & (UINT64_C(1) << (precision - 1)))
                                ? (UINT64_C(1) << padding) - 1
                                : 0;
        if ((value >> precision) != expected)
            return 0;
        value &= mask;
        if (is_signed)
            value ^= UINT64_C(1) << (precision - 1);
        file->keys[c] = value;
        file->color_order[c] = (uint16_t)c;
    }
    return at == source.length;
}

int cupidimage_jpx_channels(pdf_jpx_file *file) {
    pdf_jpx_header *header = &file->header;
    if ((header->palette.entries != 0) != (header->mapping_count != 0))
        return 0;
    if (!header->mapping_count) {
        header->mapping_count = file->image.components;
        for (unsigned c = 0; c < header->mapping_count; c++)
            header->mapping[c] = (pdf_jpx_channel){(uint16_t)c, 0, 0};
    }
    file->channels = header->mapping_count;
    for (unsigned c = 0; c < file->channels; c++) {
        const pdf_jpx_channel *mapping = header->mapping + c;
        if (mapping->component >= file->image.components ||
            (mapping->type && mapping->column >= header->palette.columns))
            return 0;
    }
    memset(file->color_order, 255, sizeof(file->color_order));
    memset(file->alpha, 255, sizeof(file->alpha));
    if (header->opacity.data)
        return jpx_opacity(file);
    if (!header->has_definitions) {
        file->ordinary = file->channels;
        for (unsigned c = 0; c < file->ordinary; c++)
            file->color_order[c] = (uint16_t)c;
        return 1;
    }
    unsigned char seen[PDF_JPX_COMPONENTS] = {0}, alpha_seen[PDF_JPX_COMPONENTS] = {0};
    for (unsigned i = 0; i < header->definition_count; i++) {
        const pdf_jpx_definition *d = header->definitions + i;
        if (d->channel >= file->channels || (d->type > 2 && d->type != 65535))
            return 0;
        seen[d->channel] = 1;
        if (d->type != 0)
            continue;
        if (!d->association || d->association == 65535)
            continue;
        if (d->association > PDF_JPX_COMPONENTS || file->color_order[d->association - 1] != 65535)
            return 0;
        file->color_order[d->association - 1] = d->channel;
        if (d->association > file->ordinary)
            file->ordinary = d->association;
    }
    if (!file->ordinary)
        return 0;
    for (unsigned c = 0; c < file->ordinary; c++)
        if (file->color_order[c] == 65535)
            return 0;
    for (unsigned c = 0; c < file->channels; c++)
        if (!seen[c])
            return 0;
    for (unsigned i = 0; i < header->definition_count; i++) {
        const pdf_jpx_definition *d = header->definitions + i;
        if (d->type != 1 && d->type != 2)
            continue;
        if (!jpx_alpha_type(file, d->channel))
            return 0;
        if (!alpha_seen[d->channel]) {
            alpha_seen[d->channel] = 1;
            file->opacity_channels++;
        }
        if (d->association == 65535)
            continue;
        if (d->association > file->ordinary)
            return 0;
        unsigned first = d->association ? d->association - 1 : 0;
        unsigned last = d->association ? d->association : file->ordinary;
        for (unsigned c = first; c < last; c++) {
            if (file->alpha[c] != 65535)
                return 0;
            file->alpha[c] = d->channel;
            file->alpha_type[c] = (unsigned char)d->type;
        }
    }
    return 1;
}
