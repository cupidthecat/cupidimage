#include "segments.h"
#include <stdlib.h>
#include <string.h>

static int jb2_segment_type(unsigned type) {
    switch (type) {
        case 0:
        case 4:
        case 6:
        case 7:
        case 16:
        case 20:
        case 22:
        case 23:
        case 36:
        case 38:
        case 39:
        case 40:
        case 42:
        case 43:
        case 48:
        case 49:
        case 50:
        case 51:
        case 52:
        case 53:
        case 62:
            return 1;
        default:
            return 0;
    }
}

int cupidimage_pdf_jb2_segment_retains(const pdf_jb2_segment *segment, unsigned index) {
    return segment && segment->retention && index <= segment->reference_count &&
           ((segment->retention[index / 8] >> (index % 8)) & 1);
}

static int jb2_unknown_length(pdf_jb2_bits *bits, pdf_jb2_segment *segment) {
    size_t start = bits->bit / 8, remaining = bits->length - start;
    if (segment->type != 38 || remaining < 24)
        return cupidimage_pdf_jb2_fail(bits->context);
    const unsigned char *data = bits->data + start;
    unsigned flags = data[17], mmr = flags & 1, template_id = (flags >> 1) & 3;
    size_t attributes = mmr ? 0 : template_id ? 2 : (flags & 16) ? 24 : 8;
    size_t offset = 18 + attributes;
    if (offset > remaining)
        return cupidimage_pdf_jb2_fail(bits->context);
    for (; remaining - offset >= 6; offset++) {
        if (!cupidimage_pdf_jb2_take(bits->context, 1))
            return 0;
        if (data[offset] == (mmr ? 0 : 255) && data[offset + 1] == (mmr ? 0 : 172)) {
            pdf_jb2_bits count = {bits->context, data + offset + 2, 4, 0};
            if (!cupidimage_pdf_jb2_read(&count, 32, &segment->rows))
                return 0;
            pdf_jb2_bits height = {bits->context, data + 4, 4, 0};
            uint32_t maximum;
            if (!cupidimage_pdf_jb2_read(&height, 32, &maximum) || segment->rows > maximum)
                return cupidimage_pdf_jb2_fail(bits->context);
            segment->length = offset + 6;
            return 1;
        }
    }
    return cupidimage_pdf_jb2_fail(bits->context);
}

static int jb2_header(pdf_jb2_bits *bits, pdf_jb2_segment *segment, unsigned global,
                      int header_only) {
    memset(segment, 0, sizeof(*segment));
    uint32_t flags, count, length;
    if (!cupidimage_pdf_jb2_read(bits, 32, &segment->number) ||
        !cupidimage_pdf_jb2_read(bits, 8, &flags))
        return 0;
    segment->type = flags & 63;
    segment->deferred = flags >> 7;
    segment->global = global;
    if (!jb2_segment_type(segment->type))
        return cupidimage_pdf_jb2_fail(bits->context);
    segment->retention = bits->data + bits->bit / 8;
    if (!cupidimage_pdf_jb2_read(bits, 8, &count))
        return 0;
    unsigned short_count = count >> 5;
    if (short_count == 7) {
        uint32_t rest;
        if (!cupidimage_pdf_jb2_read(bits, 24, &rest))
            return 0;
        count = ((count & 31) << 24) | rest;
        if (count < 5 || count > 65536)
            return cupidimage_pdf_jb2_fail(bits->context);
        size_t bytes = ((size_t)count + 8) / 8;
        segment->retention = bits->data + bits->bit / 8;
        for (size_t i = 0; i < bytes; i++) {
            uint32_t retained;
            if (!cupidimage_pdf_jb2_read(bits, 8, &retained))
                return 0;
            if (i + 1 == bytes && (count + 1) % 8 && (retained >> ((count + 1) % 8)))
                return cupidimage_pdf_jb2_fail(bits->context);
        }
    } else {
        if (short_count > 4 || ((count & 31) >> (short_count + 1)))
            return cupidimage_pdf_jb2_fail(bits->context);
        count = short_count;
    }
    segment->reference_count = count;
    if (count) {
        segment->references = cupidimage_pdf_jb2_alloc(bits->context, count, sizeof(uint32_t));
        if (!segment->references)
            return 0;
    }
    unsigned reference_bits = segment->number <= 256 ? 8 : segment->number <= 65536 ? 16 : 32;
    for (uint32_t i = 0; i < count; i++)
        if (!cupidimage_pdf_jb2_read(bits, reference_bits, segment->references + i) ||
            segment->references[i] >= segment->number)
            return cupidimage_pdf_jb2_fail(bits->context);
    if (!cupidimage_pdf_jb2_read(bits, flags & 64 ? 32 : 8, &segment->page) ||
        !cupidimage_pdf_jb2_read(bits, 32, &length))
        return 0;
    if (segment->page > 1 || (global && segment->page) ||
        (!segment->page && ((segment->type & 4) && segment->type < 48)) ||
        (!segment->page && segment->type >= 48 && segment->type <= 50))
        return cupidimage_pdf_jb2_fail(bits->context);
    /* Refinement types have bit 3, rather than bit 2, set. */
    if (!segment->page && (segment->type == 40 || segment->type == 42 || segment->type == 43))
        return cupidimage_pdf_jb2_fail(bits->context);
    segment->data = bits->data + bits->bit / 8;
    segment->length = length;
    segment->unknown_length = length == UINT32_MAX;
    if (header_only) {
        segment->data = NULL;
        return 1;
    }
    if (segment->unknown_length && !jb2_unknown_length(bits, segment))
        return 0;
    if (segment->length > bits->length - bits->bit / 8)
        return cupidimage_pdf_jb2_fail(bits->context);
    bits->bit += segment->length * 8;
    return 1;
}

int cupidimage_pdf_jb2_segments_read(pdf_jb2_segments *list, const unsigned char *data,
                                     size_t length, unsigned global) {
    if (!list || !list->context)
        return 0;
    if (global > 1 || length > PDF_JB2_INPUT_LIMIT || (length && !data))
        return cupidimage_pdf_jb2_fail(list->context);
    pdf_jb2_bits bits = {list->context, data, length, 0};
    int random_access = 0, found_end = 0;
    size_t first_segment = list->count;
    static const unsigned char signature[] = {151, 74, 66, 50, 13, 10, 26, 10};
    if (length >= sizeof(signature) && !memcmp(data, signature, sizeof(signature))) {
        uint32_t flags, pages;
        bits.bit = sizeof(signature) * 8;
        if (global || !cupidimage_pdf_jb2_read(&bits, 8, &flags) || (flags & 0xf8) ||
            (!(flags & 2) && (!cupidimage_pdf_jb2_read(&bits, 32, &pages) || pages != 1)))
            return cupidimage_pdf_jb2_fail(list->context);
        random_access = !(flags & 1);
    }
    while (bits.bit < length * 8) {
        if (list->count == 65536)
            return cupidimage_pdf_jb2_fail(list->context);
        if (list->count == list->capacity) {
            size_t capacity = list->capacity ? list->capacity * 2 : 16;
            pdf_jb2_segment *segments =
                cupidimage_pdf_jb2_alloc(list->context, capacity, sizeof(*segments));
            if (!segments)
                return 0;
            if (list->count) {
                if (!cupidimage_pdf_jb2_take(list->context, list->count))
                    return 0;
                memcpy(segments, list->segments, list->count * sizeof(*segments));
                if (!cupidimage_pdf_jb2_release(list->context, list->segments))
                    return 0;
            }
            list->segments = segments;
            list->capacity = capacity;
        }
        pdf_jb2_segment *segment = list->segments + list->count;
        if (!jb2_header(&bits, segment, global, random_access))
            return 0;
        list->reference_count += segment->reference_count;
        if (list->reference_count > 1048576)
            return cupidimage_pdf_jb2_fail(list->context);
        list->count++;
        if (random_access && segment->type == 51) {
            found_end = 1;
            break;
        }
    }
    if (random_access) {
        if (!found_end)
            return cupidimage_pdf_jb2_fail(list->context);
        for (size_t i = first_segment; i < list->count; i++) {
            pdf_jb2_segment *segment = list->segments + i;
            segment->data = bits.data + bits.bit / 8;
            if ((segment->unknown_length && !jb2_unknown_length(&bits, segment)) ||
                segment->length > bits.length - bits.bit / 8)
                return cupidimage_pdf_jb2_fail(list->context);
            bits.bit += segment->length * 8;
        }
        if (bits.bit != length * 8)
            return cupidimage_pdf_jb2_fail(list->context);
    }
    return 1;
}

static int jb2_segment_compare(const void *left, const void *right) {
    uint32_t a = ((const pdf_jb2_segment *)left)->number;
    uint32_t b = ((const pdf_jb2_segment *)right)->number;
    return (a > b) - (a < b);
}

pdf_jb2_segment *cupidimage_pdf_jb2_segment_find(pdf_jb2_segments *list, uint32_t number) {
    size_t start = 0, end = list->count;
    while (start < end) {
        if (!cupidimage_pdf_jb2_take(list->context, 1))
            return NULL;
        size_t middle = start + (end - start) / 2;
        pdf_jb2_segment *candidate = list->segments + middle;
        if (candidate->number == number)
            return candidate;
        if (candidate->number < number)
            start = middle + 1;
        else
            end = middle;
    }
    return NULL;
}

static int jb2_intermediate(unsigned type) {
    return type == 4 || type == 20 || type == 36 || type == 40;
}

int cupidimage_pdf_jb2_segments_index(pdf_jb2_segments *list) {
    if (!list || !cupidimage_pdf_jb2_take(list->context, list->count * 32))
        return 0;
    if (list->count)
        qsort(list->segments, list->count, sizeof(*list->segments), jb2_segment_compare);
    for (size_t i = 0; i < list->count; i++) {
        pdf_jb2_segment *segment = list->segments + i;
        if (i && list->segments[i - 1].number == segment->number)
            return cupidimage_pdf_jb2_fail(list->context);
        unsigned tables = 0;
        for (uint32_t j = 0; j < segment->reference_count; j++) {
            pdf_jb2_segment *reference =
                cupidimage_pdf_jb2_segment_find(list, segment->references[j]);
            if (!reference || reference->number >= segment->number ||
                (reference->page && reference->page != segment->page))
                return cupidimage_pdf_jb2_fail(list->context);
            unsigned type = segment->type, target = reference->type;
            int allowed = type == 62 ||
                          ((type == 0 || type == 4 || type == 6 || type == 7) &&
                           (target == 0 || target == 53)) ||
                          ((type == 20 || type == 22 || type == 23) && target == 16) ||
                          ((type == 40 || type == 42 || type == 43) && jb2_intermediate(target));
            if (!allowed)
                return cupidimage_pdf_jb2_fail(list->context);
            if (target == 53)
                tables++;
            if (jb2_intermediate(target) && type != 62 && ++reference->intermediate_users > 1)
                return cupidimage_pdf_jb2_fail(list->context);
        }
        unsigned type = segment->type, count = segment->reference_count;
        if ((type == 0 && tables > 4) || ((type == 4 || type == 6 || type == 7) && tables > 8) ||
            ((type == 20 || type == 22 || type == 23 || type == 40) && count != 1) ||
            ((type == 42 || type == 43) && count > 1))
            return cupidimage_pdf_jb2_fail(list->context);
    }
    return 1;
}
