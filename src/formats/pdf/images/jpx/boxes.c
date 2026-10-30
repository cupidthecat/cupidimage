#include "boxes.h"
#include <stdlib.h>
#include <string.h>

uint16_t cupidimage_jpx_u16(const unsigned char *p) {
    return (uint16_t)((unsigned)p[0] * 256 + p[1]);
}

uint32_t cupidimage_jpx_u32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

uint64_t cupidimage_jpx_u64(const unsigned char *p) {
    return (uint64_t)cupidimage_jpx_u32(p) << 32 | cupidimage_jpx_u32(p + 4);
}

int cupidimage_jpx_charge(pdf_jpx_reader *reader, size_t amount) {
    if (!reader->work || amount > *reader->work)
        return 0;
    *reader->work -= amount;
    return 1;
}

int cupidimage_jpx_box_next(pdf_jpx_reader *reader, pdf_jpx_slice source, size_t *position,
                            int allow_to_end, pdf_jpx_box *box) {
    if (*position > source.length || source.length - *position < 8 || reader->boxes == 4096 ||
        !cupidimage_jpx_charge(reader, 16))
        return 0;
    reader->boxes++;
    const unsigned char *p = source.data + *position;
    uint64_t length = cupidimage_jpx_u32(p);
    size_t header = 8, remaining = source.length - *position;
    box->type = cupidimage_jpx_u32(p + 4);
    box->to_end = length == 0;
    if (!length) {
        if (!allow_to_end)
            return 0;
        length = remaining;
    } else if (length == 1) {
        if (remaining < 16)
            return 0;
        header = 16;
        length = cupidimage_jpx_u64(p + 8);
    }
    if (length < header || length > remaining)
        return 0;
    box->payload = (pdf_jpx_slice){p + header, (size_t)length - header};
    *position += (size_t)length;
    return 1;
}

/* Baseline fragments remain inside this file and appear in physical order. */
int cupidimage_jpx_fragments(pdf_jpx_reader *reader, pdf_jpx_slice source, size_t limit,
                             unsigned char **result, size_t *length, size_t *first, size_t *last) {
    *result = NULL;
    *length = 0;
    size_t at = 0;
    pdf_jpx_box box;
    if (!cupidimage_jpx_box_next(reader, source, &at, 0, &box) || at != source.length ||
        box.type != JPX_BOX('f', 'l', 's', 't') || box.payload.length < 2)
        return 0;
    unsigned count = cupidimage_jpx_u16(box.payload.data);
    if (!count || count > 4096 || box.payload.length != 2u + (size_t)count * 14 ||
        !cupidimage_jpx_charge(reader, count))
        return 0;
    size_t previous = 0, total = 0;
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *p = box.payload.data + 2 + i * 14;
        uint64_t offset = cupidimage_jpx_u64(p);
        uint32_t bytes = cupidimage_jpx_u32(p + 8);
        if (cupidimage_jpx_u16(p + 12) || offset < 12 || offset > reader->length ||
            bytes > reader->length - (size_t)offset || (bytes && offset < previous) ||
            bytes > limit - total)
            return 0;
        if (bytes) {
            if (!total)
                *first = (size_t)offset;
            previous = (size_t)offset + bytes;
            total += bytes;
        }
    }
    if (!total || !cupidimage_jpx_charge(reader, total))
        return 0;
    unsigned char *data = malloc(total);
    if (!data)
        return 0;
    at = 0;
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *p = box.payload.data + 2 + i * 14;
        size_t offset = (size_t)cupidimage_jpx_u64(p), bytes = cupidimage_jpx_u32(p + 8);
        memcpy(data + at, reader->data + offset, bytes);
        at += bytes;
    }
    *result = data;
    *length = total;
    *last = previous;
    return 1;
}
