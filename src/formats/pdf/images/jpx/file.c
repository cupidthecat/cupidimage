#include "boxes.h"
#include "core/internal.h"
#include <stdlib.h>
#include <string.h>

void cupidimage_pdf_jpx_file_free(pdf_jpx_file *file) {
    cupidimage_pdf_jpx_samples_free(&file->image);
    free(file->codestream_owned);
    for (unsigned i = 0; i < file->shared_count; i++)
        free(file->shared[i]);
    memset(file, 0, sizeof(*file));
}

static int jpx_file_type(pdf_jpx_reader *reader, pdf_jpx_slice source) {
    if (source.length < 12 || (source.length - 8) % 4 ||
        !cupidimage_jpx_charge(reader, source.length))
        return 0;
    uint32_t brand = cupidimage_jpx_u32(source.data);
    int jp2 = 0, baseline = 0;
    for (size_t at = 8; at < source.length; at += 4) {
        uint32_t type = cupidimage_jpx_u32(source.data + at);
        jp2 |= type == JPX_BOX('j', 'p', '2', ' ');
        baseline |= type == JPX_BOX('j', 'p', 'x', 'b');
    }
    reader->file->extended =
        brand == JPX_BOX('j', 'p', 'x', ' ') || (brand != JPX_BOX('j', 'p', '2', ' ') && baseline);
    return jp2 || baseline;
}

static int jpx_fragment_boundary(pdf_jpx_reader *reader, size_t first, size_t *boundary) {
    pdf_jpx_slice whole = {reader->data, reader->length};
    size_t at = 0;
    while (at < whole.length) {
        size_t start = at;
        pdf_jpx_box box;
        if (!cupidimage_jpx_box_next(reader, whole, &at, 1, &box))
            return 0;
        if (first >= start && first < at) {
            *boundary = start;
            return 1;
        }
    }
    return 0;
}

static int jpx_parse_file(pdf_jpx_reader *reader, pdf_jpx_slice *codestream) {
    pdf_jpx_slice whole = {reader->data, reader->length};
    pdf_jpx_file *file = reader->file;
    size_t at = 0, boundary = 0;
    pdf_jpx_box box;
    if (!cupidimage_jpx_box_next(reader, whole, &at, 1, &box) || at != 12 ||
        box.type != JPX_BOX('j', 'P', ' ', ' ') || box.payload.length != 4 ||
        cupidimage_jpx_u32(box.payload.data) != UINT32_C(0x0d0a870a) ||
        !cupidimage_jpx_box_next(reader, whole, &at, 1, &box) ||
        box.type != JPX_BOX('f', 't', 'y', 'p') || !jpx_file_type(reader, box.payload))
        return 0;
    int header = 0, stream_header = 0, layer_header = 0;
    while (at < whole.length) {
        size_t start = at;
        if (!cupidimage_jpx_box_next(reader, whole, &at, 1, &box))
            return 0;
        if (box.type == JPX_BOX('j', 'p', '2', 'h')) {
            if (header || codestream->data ||
                !cupidimage_jpx_header_read(reader, box.payload, 0, box.to_end))
                return 0;
            header = 1;
        } else if (box.type == JPX_BOX('j', 'p', '2', 'c')) {
            if (!header)
                return 0;
            if (!codestream->data) {
                *codestream = box.payload;
                boundary = start;
            }
        } else if (file->extended && box.type == JPX_BOX('f', 't', 'b', 'l')) {
            if (!header)
                return 0;
            if (!codestream->data) {
                size_t length, first, last;
                if (!cupidimage_jpx_fragments(reader, box.payload, 64u * 1024u * 1024u,
                                              &file->codestream_owned, &length, &first, &last) ||
                    !jpx_fragment_boundary(reader, first, &boundary))
                    return 0;
                *codestream = (pdf_jpx_slice){file->codestream_owned, length};
            }
        } else if (file->extended && box.type == JPX_BOX('j', 'p', 'c', 'h')) {
            if (!stream_header++) {
                if (!header || codestream->data ||
                    !cupidimage_jpx_header_read(reader, box.payload, 1, box.to_end))
                    return 0;
            }
        } else if (file->extended && box.type == JPX_BOX('j', 'p', 'l', 'h')) {
            if (!layer_header++) {
                if (!header || codestream->data ||
                    !cupidimage_jpx_header_read(reader, box.payload, 2, box.to_end))
                    return 0;
            }
        } else if (file->extended && box.type == JPX_BOX('m', 'd', 'a', 't') && !header) {
            return 0;
        } else if (box.type == JPX_BOX('f', 't', 'y', 'p') ||
                   box.type == JPX_BOX('j', 'P', ' ', ' ')) {
            return 0;
        }
    }
    return header && codestream->data && codestream->length && reader->metadata_end <= boundary;
}

static int jpx_match_header(const pdf_jpx_file *file) {
    const pdf_jpx_header *header = &file->header;
    const pdf_jpx_samples *image = &file->image;
    if (file->boxed &&
        (!header->has_image_header || image->width != header->width ||
         image->height != header->height || image->components != header->components ||
         (header->common_bits == 255 && header->bits_count != header->components)))
        return 0;
    for (unsigned c = 0; c < image->components; c++) {
        const pdf_jpx_plane *plane = image->planes + c;
        if (file->boxed) {
            unsigned bits = header->common_bits == 255 ? header->bits[c] : header->common_bits;
            if ((bits & 127) + 1 != plane->precision || (bits >> 7) != plane->is_signed)
                return 0;
        }
        uint64_t x0 = ((uint64_t)image->x0 + plane->dx - 1) / plane->dx;
        uint64_t y0 = ((uint64_t)image->y0 + plane->dy - 1) / plane->dy;
        uint64_t x1 = ((uint64_t)image->x0 + image->width + plane->dx - 1) / plane->dx;
        uint64_t y1 = ((uint64_t)image->y0 + image->height + plane->dy - 1) / plane->dy;
        if (plane->x0 != x0 || plane->y0 != y0 || plane->width != x1 - x0 ||
            plane->height != y1 - y0)
            return 0;
    }
    return 1;
}

int cupidimage_pdf_jpx_file_decode(const unsigned char *data, size_t length, pdf_jpx_file *file,
                                   size_t *work, char *error, size_t error_capacity) {
    memset(file, 0, sizeof(*file));
    if (!data || length < 2 || length > 64u * 1024u * 1024u || !work)
        return 0;
    pdf_jpx_reader reader = {data, length, 0, 0, work, file};
    pdf_jpx_slice codestream = {data, length};
    int ok = 1;
    if (data[0] != 0xff || data[1] != 0x4f) {
        file->boxed = 1;
        codestream = (pdf_jpx_slice){0};
        ok = jpx_parse_file(&reader, &codestream);
    }
    if (ok)
        ok = cupidimage_pdf_jpx_codestream(codestream.data, codestream.length, &file->image, work,
                                           error, error_capacity) &&
             jpx_match_header(file) && cupidimage_jpx_channels(file);
    if (!ok) {
        cupidimage_pdf_jpx_file_free(file);
        cupidimage_set_err(error, error_capacity,
                           "pdf: invalid or unsupported JPEG 2000 image structure");
    }
    return ok;
}
