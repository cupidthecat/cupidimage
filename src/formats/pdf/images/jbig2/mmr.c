#include "compression/ccitt.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>

int cupidimage_pdf_jb2_mmr(pdf_jb2_context *context, const unsigned char *data, size_t length,
                           uint32_t width, uint32_t height, int require_end, size_t *consumed_bytes,
                           pdf_jb2_bitmap **output) {
    if (!output || !consumed_bytes)
        return cupidimage_pdf_jb2_fail(context);
    *output = NULL;
    *consumed_bytes = 0;
    if ((!data && length) || length > PDF_JB2_INPUT_LIMIT || (require_end != 0 && require_end != 1))
        return cupidimage_pdf_jb2_fail(context);
    pdf_jb2_bitmap *bitmap = cupidimage_pdf_jb2_bitmap_new(context, width, height, 0);
    if (!bitmap)
        return 0;
    if (!width || !height) {
        if (require_end) {
            if (length < 3 || data[0] != 0 || data[1] != 0x10 || data[2] != 1 ||
                !cupidimage_pdf_jb2_take(context, 24))
                return cupidimage_pdf_jb2_fail(context);
            *consumed_bytes = 3;
        } else
            *consumed_bytes = length;
        *output = bitmap;
        return 1;
    }
    if (!cupidimage_pdf_jb2_take(context, (uint64_t)width * height * 3))
        return 0;
    size_t bytes = bitmap->stride * height;
    /* Reserve the fax decoder's two row buffers, transition array and worst
     * realloc peak. Its output cap prevents additional rows from exceeding it. */
    size_t temporary = bytes * 3 + bitmap->stride * 2 + ((size_t)width + 2) * sizeof(uint32_t);
    if (temporary > context->limit || context->live > context->limit - temporary)
        return cupidimage_pdf_jb2_fail(context);
    if (context->live + temporary > context->peak)
        context->peak = context->live + temporary;
    cupidimage_ccitt_options options = {-1, (int)width, (int)height, 0, 0, require_end, 1, 0};
    unsigned char *decoded = NULL;
    size_t decoded_length = 0, consumed_bits = 0;
    int ok = cupidimage_ccitt_decode_bounded(data, length, &options, &decoded, &decoded_length,
                                             bytes, context->work, 1, &consumed_bits);
    if (!ok || decoded_length != bytes) {
        free(decoded);
        return cupidimage_pdf_jb2_fail(context);
    }
    memcpy(bitmap->data, decoded, bytes);
    free(decoded);
    if (width % 8)
        for (unsigned y = 0; y < height; y++)
            bitmap->data[(size_t)y * bitmap->stride + bitmap->stride - 1] &=
                (unsigned char)(255u << (8 - width % 8));
    /* T.88 6.2.6 permits the optional marker to remain unread when the block's
     * complete byte count is supplied. Unknown-length blocks require EOFB. */
    *consumed_bytes = require_end ? (consumed_bits + 7) / 8 : length;
    *output = bitmap;
    return 1;
}
