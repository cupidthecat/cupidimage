#include "source.h"
#include <stdlib.h>
#include <string.h>

void cupidimage_pdf_jpx_source_free(pdf_jpx_source *source) {
    cupidimage_pdf_jpx_file_free(&source->file);
    free(source->encoded);
    memset(source, 0, sizeof(*source));
}

int cupidimage_pdf_jpx_source_load(const pdf_render_state *st, const pdf_object_view *view,
                                   pdf_jpx_source *source) {
    pdf_image_stream_profile profile = {0, 0, 0, 0, st->document, st->document_size, 1};
    char filters[8][32];
    int count;
    memset(source, 0, sizeof(*source));
    if (!cupidimage_pdf_image_filters(view, &profile, filters, &count))
        return 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(filters[i], "JPXDecode"))
            continue;
        if (i + 1 != count)
            return 0;
        source->present = 1;
    }
    if (!source->present)
        return 1;
    const unsigned char *stream;
    size_t length, decoded_length;
    int width, height;
    if (!cupidimage_pdf_image_integer(st, view, "Width", &width) ||
        !cupidimage_pdf_image_integer(st, view, "Height", &height) || width < 1 || height < 1 ||
        !cupidimage_pdf_image_stream(st, view, &stream, &length) ||
        !cupidimage_pdf_decode_image_stream(view, stream, length, &source->encoded, &decoded_length,
                                            &profile) ||
        !cupidimage_pdf_jpx_file_decode(source->encoded, decoded_length, &source->file,
                                        &st->clip_budget->tests_left, st->error,
                                        st->error_capacity) ||
        source->file.image.width != (unsigned)width ||
        source->file.image.height != (unsigned)height) {
        cupidimage_pdf_jpx_source_free(source);
        return 0;
    }
    return 1;
}
