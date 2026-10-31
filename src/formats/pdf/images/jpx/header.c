#include "boxes.h"
#include <stdlib.h>
#include <string.h>

enum {
    JPX_IHDR = 1,
    JPX_BPCC = 2,
    JPX_PCLR = 4,
    JPX_CMAP = 8,
    JPX_CDEF = 16,
    JPX_CGRP = 32,
    JPX_OPCT = 64,
    JPX_CREG = 128
};

static int jpx_palette_read(pdf_jpx_reader *reader, pdf_jpx_slice source) {
    pdf_jpx_palette palette = {0};
    if (source.length < 3)
        return 0;
    palette.entries = cupidimage_jpx_u16(source.data);
    palette.columns = source.data[2];
    if (!palette.entries || palette.entries > 1024 || !palette.columns ||
        source.length < 3 + palette.columns)
        return 0;
    for (unsigned c = 0; c < palette.columns; c++) {
        palette.bits[c] = source.data[3 + c];
        unsigned precision = (palette.bits[c] & 127) + 1;
        if (precision > 38)
            return 0;
        palette.offsets[c] = palette.row_bytes;
        palette.row_bytes += (precision + 7) / 8;
    }
    size_t header = 3 + palette.columns;
    if (source.length - header != (size_t)palette.entries * palette.row_bytes ||
        !cupidimage_jpx_charge(reader, source.length))
        return 0;
    palette.data = (pdf_jpx_slice){source.data + header, source.length - header};
    for (unsigned row = 0; row < palette.entries; row++)
        for (unsigned c = 0; c < palette.columns; c++) {
            unsigned precision = (palette.bits[c] & 127) + 1, used = precision & 7;
            if (used &&
                (palette.data.data[(size_t)row * palette.row_bytes + palette.offsets[c]] >> used))
                return 0;
        }
    reader->file->header.palette = palette;
    return 1;
}

static int jpx_header_box(pdf_jpx_reader *reader, const pdf_jpx_box *box, int scope, unsigned *seen,
                          unsigned depth) {
    pdf_jpx_header *header = &reader->file->header;
    pdf_jpx_slice source = box->payload;
    const unsigned char *p = source.data;
    unsigned flag = 0;
    switch (box->type) {
        case JPX_BOX('i', 'h', 'd', 'r'):
            flag = JPX_IHDR;
            break;
        case JPX_BOX('b', 'p', 'c', 'c'):
            flag = JPX_BPCC;
            break;
        case JPX_BOX('p', 'c', 'l', 'r'):
            flag = JPX_PCLR;
            break;
        case JPX_BOX('c', 'm', 'a', 'p'):
            flag = JPX_CMAP;
            break;
        case JPX_BOX('c', 'd', 'e', 'f'):
            flag = JPX_CDEF;
            break;
        case JPX_BOX('c', 'g', 'r', 'p'):
            flag = JPX_CGRP;
            break;
        case JPX_BOX('o', 'p', 'c', 't'):
            flag = JPX_OPCT;
            break;
        case JPX_BOX('c', 'r', 'e', 'g'):
            flag = JPX_CREG;
            break;
        default:
            break;
    }
    int structural = flag & (JPX_IHDR | JPX_BPCC | JPX_PCLR | JPX_CMAP);
    int layer = flag & (JPX_CGRP | JPX_OPCT | JPX_CREG);
    if ((structural && scope >= 2) || (layer && scope != 2) ||
        (flag == JPX_CDEF && (scope == 1 || scope == 3)))
        return 1;
    if (flag && (*seen & flag))
        return 0;
    if (flag)
        *seen |= flag;
    if (flag == JPX_IHDR) {
        if (source.length != 14 || !cupidimage_jpx_u32(p) || !cupidimage_jpx_u32(p + 4) ||
            !cupidimage_jpx_u16(p + 8) || cupidimage_jpx_u16(p + 8) > PDF_JPX_COMPONENTS ||
            p[11] != 7 || p[12] > 1 || p[13] > 1 || (p[10] != 255 && (p[10] & 127) >= 38))
            return 0;
        header->height = cupidimage_jpx_u32(p);
        header->width = cupidimage_jpx_u32(p + 4);
        header->components = cupidimage_jpx_u16(p + 8);
        header->common_bits = p[10];
        header->has_image_header = 1;
        if (p[10] != 255)
            header->bits_count = 0;
    } else if (flag == JPX_BPCC) {
        if (!header->has_image_header || header->common_bits != 255 ||
            source.length != header->components)
            return 0;
        for (unsigned c = 0; c < header->components; c++) {
            if ((p[c] & 127) >= 38)
                return 0;
            header->bits[c] = p[c];
        }
        header->bits_count = header->components;
    } else if (flag == JPX_PCLR) {
        return jpx_palette_read(reader, source);
    } else if (flag == JPX_CMAP) {
        if (!source.length || source.length % 4 || source.length / 4 > PDF_JPX_COMPONENTS)
            return 0;
        header->mapping_count = (unsigned)(source.length / 4);
        for (unsigned c = 0; c < header->mapping_count; c++) {
            const unsigned char *entry = p + 4 * c;
            if (entry[2] > 1 || (!entry[2] && entry[3]))
                return 0;
            header->mapping[c] = (pdf_jpx_channel){cupidimage_jpx_u16(entry), entry[2], entry[3]};
        }
    } else if (flag == JPX_CDEF) {
        if ((*seen & JPX_OPCT) || source.length < 2)
            return 0;
        unsigned count = cupidimage_jpx_u16(p);
        if (!count || count > PDF_JPX_DEFINITIONS || source.length != 2u + (size_t)count * 6)
            return 0;
        header->has_definitions = 1;
        header->definition_count = count;
        header->opacity = (pdf_jpx_slice){0};
        for (unsigned i = 0; i < count; i++) {
            const unsigned char *entry = p + 2 + i * 6;
            header->definitions[i] =
                (pdf_jpx_definition){cupidimage_jpx_u16(entry), cupidimage_jpx_u16(entry + 2),
                                     cupidimage_jpx_u16(entry + 4)};
        }
    } else if (flag == JPX_CGRP) {
        header->color_count = 0;
        return cupidimage_jpx_header_read(reader, source, 3, box->to_end);
    } else if (flag == JPX_OPCT) {
        if ((*seen & JPX_CDEF) || !source.length || p[0] > 2 || (p[0] < 2 && source.length != 1) ||
            (p[0] == 2 && source.length < 2))
            return 0;
        header->opacity = source;
        header->has_definitions = header->definition_count = 0;
    } else if (flag == JPX_CREG) {
        /* One identity-registered codestream is the currently supported layer grid. */
        if (source.length != 10 || cupidimage_jpx_u16(p + 4) || !p[6] || !p[7] || p[8] || p[9] ||
            cupidimage_jpx_u16(p) != p[6] || cupidimage_jpx_u16(p + 2) != p[7])
            return 0;
    } else if (box->type == JPX_BOX('c', 'o', 'l', 'r') && (scope == 0 || scope == 3)) {
        if (source.length < 3)
            return 0;
        unsigned method = p[0];
        if (method < 1 || method > 4)
            return 1;
        if (header->color_count == PDF_JPX_COLORS || (method == 1 && source.length < 7))
            return 0;
        pdf_jpx_color *color = header->colors + header->color_count++;
        color->method = method;
        color->precedence = p[1] < 128 ? p[1] : (int)p[1] - 256;
        color->approximation = p[2];
        color->enumeration = method == 1 ? cupidimage_jpx_u32(p + 3) : 0;
        size_t prefix = method == 1 ? 7 : 3;
        color->parameters = (pdf_jpx_slice){p + prefix, source.length - prefix};
    } else if (box->type == JPX_BOX('c', 'r', 'e', 'f') && reader->file->extended &&
               (scope == 1 || scope == 2)) {
        if (source.length < 4)
            return 0;
        uint32_t type = cupidimage_jpx_u32(p);
        if (type == JPX_BOX('c', 'r', 'e', 'f'))
            return 0;
        int relevant =
            scope == 1
                ? type == JPX_BOX('i', 'h', 'd', 'r') || type == JPX_BOX('b', 'p', 'c', 'c') ||
                      type == JPX_BOX('p', 'c', 'l', 'r') || type == JPX_BOX('c', 'm', 'a', 'p')
                : type == JPX_BOX('c', 'g', 'r', 'p') || type == JPX_BOX('c', 'd', 'e', 'f') ||
                      type == JPX_BOX('o', 'p', 'c', 't') || type == JPX_BOX('c', 'r', 'e', 'g');
        if (!relevant)
            return 1;
        if (source.length < 12 || depth == 8 || reader->file->shared_count == PDF_JPX_SHARED)
            return 0;
        unsigned char *joined;
        size_t length, first, last;
        if (!cupidimage_jpx_fragments(reader, (pdf_jpx_slice){p + 4, source.length - 4},
                                      16u * 1024u * 1024u - reader->file->shared_bytes, &joined,
                                      &length, &first, &last))
            return 0;
        if (last > reader->metadata_end)
            reader->metadata_end = last;
        reader->file->shared[reader->file->shared_count++] = joined;
        reader->file->shared_bytes += length;
        pdf_jpx_box logical = {type, {joined, length}, 0};
        return jpx_header_box(reader, &logical, scope, seen, depth + 1);
    }
    return 1;
}

int cupidimage_jpx_header_read(pdf_jpx_reader *reader, pdf_jpx_slice source, int scope,
                               int allow_to_end) {
    size_t at = 0;
    unsigned seen = 0;
    while (at < source.length) {
        pdf_jpx_box box;
        if (!cupidimage_jpx_box_next(reader, source, &at, allow_to_end, &box) ||
            (!scope && !seen && box.type != JPX_BOX('i', 'h', 'd', 'r')) ||
            !jpx_header_box(reader, &box, scope, &seen, 0))
            return 0;
    }
    return scope != 0 || (seen & JPX_IHDR);
}
