#include "decode.h"
#include "document.h"

static int jb2_page_information(pdf_jb2_document *document, pdf_jb2_segment *segment) {
    pdf_jb2_bits bits = {document->context, segment->data, segment->length, 0};
    uint32_t width, height, x_resolution, y_resolution, flags, stripes;
    if (document->page || segment->length != 19 || !cupidimage_pdf_jb2_read(&bits, 32, &width) ||
        !cupidimage_pdf_jb2_read(&bits, 32, &height) ||
        !cupidimage_pdf_jb2_read(&bits, 32, &x_resolution) ||
        !cupidimage_pdf_jb2_read(&bits, 32, &y_resolution) ||
        !cupidimage_pdf_jb2_read(&bits, 8, &flags) || !cupidimage_pdf_jb2_read(&bits, 16, &stripes))
        return cupidimage_pdf_jb2_fail(document->context);
    if (width != document->width || (height != UINT32_MAX && height != document->height) ||
        (height == UINT32_MAX && !(stripes & 0x8000)) || (flags & 128))
        return cupidimage_pdf_jb2_fail(document->context);
    document->page_flags = flags;
    document->striped = stripes >> 15;
    document->stripe_size = stripes & 0x7fff;
    document->unknown_height = height == UINT32_MAX;
    document->page =
        cupidimage_pdf_jb2_bitmap_new(document->context, width, document->height, (flags >> 2) & 1);
    return document->page != NULL;
}

static int jb2_metadata(pdf_jb2_document *document, pdf_jb2_segment *segment) {
    pdf_jb2_context *context = document->context;
    pdf_jb2_bits bits = {context, segment->data, segment->length, 0};
    uint32_t value;
    switch (segment->type) {
        case 48:
            return jb2_page_information(document, segment);
        case 49:
            if (segment->length || !document->page)
                return cupidimage_pdf_jb2_fail(context);
            document->ended = 1;
            return 1;
        case 50:
            if (!document->striped || segment->length != 4 ||
                !cupidimage_pdf_jb2_read(&bits, 32, &value) || value >= document->height ||
                (int64_t)value <= document->stripe_end ||
                (uint64_t)((int64_t)value - (document->stripe_end < 0 ? 0 : document->stripe_end)) >
                    document->stripe_size)
                return cupidimage_pdf_jb2_fail(context);
            document->stripe_end = value;
            return 1;
        case 51:
            return !segment->length || cupidimage_pdf_jb2_fail(context);
        case 52:
            if (!cupidimage_pdf_jb2_read(&bits, 32, &value) ||
                (uint64_t)value * 4 != segment->length - 4 ||
                !cupidimage_pdf_jb2_take(context, value))
                return cupidimage_pdf_jb2_fail(context);
            return 1;
        case 53: {
            pdf_jb2_huffman *table = cupidimage_pdf_jb2_alloc(context, 1, sizeof(*table));
            if (!table ||
                !cupidimage_pdf_jb2_huffman_custom(context, segment->data, segment->length, table))
                return 0;
            segment->result = table;
            return 1;
        }
        case 62:
            if (!cupidimage_pdf_jb2_read(&bits, 32, &value) || (value & 0x80000000u))
                return cupidimage_pdf_jb2_fail(context);
            /* Non-necessary extensions do not change the decoded page bitmap. */
            return 1;
        default:
            return cupidimage_pdf_jb2_fail(context);
    }
}

static int jb2_decode_segment(pdf_jb2_document *document, pdf_jb2_segment *segment) {
    pdf_jb2_context *context = document->context;
    if (segment->page && document->ended && segment->type != 51)
        return cupidimage_pdf_jb2_fail(context);
    for (uint32_t i = 0; i < segment->reference_count; i++) {
        pdf_jb2_segment *reference =
            cupidimage_pdf_jb2_segment_find(&document->segments, segment->references[i]);
        if (!reference || reference->state != 2 || !reference->retained)
            return cupidimage_pdf_jb2_fail(context);
    }
    segment->state = 1;
    int ok;
    if (segment->type == 0)
        ok = cupidimage_pdf_jb2_decode_dictionary(document, segment);
    else if (segment->type == 16) {
        pdf_jb2_bits bits = {context, segment->data, segment->length, 0};
        uint32_t flags, width, height, maximum;
        if (!cupidimage_pdf_jb2_read(&bits, 8, &flags) || (flags & 0xf8) ||
            !cupidimage_pdf_jb2_read(&bits, 8, &width) ||
            !cupidimage_pdf_jb2_read(&bits, 8, &height) ||
            !cupidimage_pdf_jb2_read(&bits, 32, &maximum) || maximum >= 65536)
            return cupidimage_pdf_jb2_fail(context);
        pdf_jb2_patterns *patterns;
        ok = cupidimage_pdf_jb2_patterns_decode(
            context, segment->data + bits.bit / 8, segment->length - bits.bit / 8, flags & 1,
            (flags >> 1) & 3, width, height, maximum + 1, &patterns);
        if (ok)
            segment->result = patterns;
    } else if (segment->type < 48)
        ok = cupidimage_pdf_jb2_decode_region(document, segment);
    else
        ok = jb2_metadata(document, segment);
    if (!ok)
        return 0;
    segment->state = 2;
    segment->retained = cupidimage_pdf_jb2_segment_retains(segment, 0);
    for (uint32_t i = 0; i < segment->reference_count; i++) {
        pdf_jb2_segment *reference =
            cupidimage_pdf_jb2_segment_find(&document->segments, segment->references[i]);
        if (!reference)
            return cupidimage_pdf_jb2_fail(context);
        reference->retained = cupidimage_pdf_jb2_segment_retains(segment, i + 1);
    }
    return 1;
}

int cupidimage_pdf_jbig2_decode(const unsigned char *data, size_t length,
                                const unsigned char *globals, size_t globals_length, uint32_t width,
                                uint32_t height, size_t *work, unsigned char **output,
                                size_t *output_length) {
    if (!output || !output_length)
        return 0;
    *output = NULL;
    *output_length = 0;
    if (!width || !height || width > 65535 || height > 65535 ||
        (uint64_t)width * height > PDF_JB2_PIXEL_LIMIT || length > PDF_JB2_INPUT_LIMIT ||
        globals_length > PDF_JB2_INPUT_LIMIT - length || !work)
        return 0;
    pdf_jb2_context context;
    if (!cupidimage_pdf_jb2_context_init(&context, work, PDF_JB2_MEMORY_LIMIT))
        return 0;
    pdf_jb2_document document = {.context = &context,
                                 .segments = {.context = &context},
                                 .width = width,
                                 .height = height,
                                 .stripe_end = -1};
    int ok = cupidimage_pdf_jb2_segments_read(&document.segments, globals, globals_length, 1) &&
             cupidimage_pdf_jb2_segments_read(&document.segments, data, length, 0) &&
             cupidimage_pdf_jb2_segments_index(&document.segments);
    for (size_t i = 0; ok && i < document.segments.count; i++) {
        pdf_jb2_segment *segment = document.segments.segments + i;
        if (segment->type == 51 && i + 1 != document.segments.count) {
            ok = 0;
            break;
        }
        ok = jb2_decode_segment(&document, segment);
    }
    if (!document.page || (document.unknown_height && document.stripe_end != (int64_t)height - 1))
        ok = 0;
    if (ok) {
        size_t bytes = document.page->stride * height;
        ok = cupidimage_pdf_jb2_take(&context, bytes);
        if (ok) {
            for (size_t i = 0; i < bytes; i++)
                document.page->data[i] ^= 255;
            *output = cupidimage_pdf_jb2_detach(&context, document.page->data);
            ok = *output != NULL;
            if (ok)
                *output_length = bytes;
        }
    }
    cupidimage_pdf_jb2_clear(&context);
    return ok;
}
