#include "filter.h"
#include "decode.h"
#include <stdlib.h>

int cupidimage_pdf_jbig2_filter(const pdf_object_view *view,
                                const pdf_image_stream_profile *profile, pdf_object_id globals_id,
                                const unsigned char *data, size_t length, unsigned char **output,
                                size_t *output_length) {
    if (!profile || profile->width < 1 || profile->height < 1 || profile->components != 1 ||
        profile->bits != 1)
        return 0;
    unsigned char *globals = NULL;
    size_t globals_length = 0, work = 500000000;
    int ok = 1;
    if (globals_id) {
        pdf_object_view global_view = {0};
        pdf_object_id identity;
        const unsigned char *raw;
        size_t raw_length;
        ok = cupidimage_pdf_resolve_object(view->xref, profile->document, profile->document_size,
                                           globals_id, &global_view, &identity, &work) == 1 &&
             cupidimage_pdf_object_stream(profile->document, profile->document_size, &global_view,
                                          &raw, &raw_length) &&
             cupidimage_pdf_decode_stream(&global_view, raw, raw_length, &globals, &globals_length);
        cupidimage_pdf_object_view_release(&global_view);
    }
    if (ok)
        ok = cupidimage_pdf_jbig2_decode(data, length, globals, globals_length,
                                         (uint32_t)profile->width, (uint32_t)profile->height, &work,
                                         output, output_length);
    free(globals);
    return ok;
}
