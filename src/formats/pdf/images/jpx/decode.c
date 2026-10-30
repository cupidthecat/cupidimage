#include "budget.h"
#include "core/internal.h"
#include "jpx.h"
#include "third_party/openjpeg/prefix.h"
#define OPJ_STATIC
#include "third_party/openjpeg/openjpeg.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define JPX_SAMPLE_LIMIT (16u * 1024u * 1024u)
#define JPX_CODEC_MEMORY (128u * 1024u * 1024u)

typedef struct {
    const unsigned char *bytes;
    size_t length, position;
    char *error;
    size_t error_capacity;
    int warned;
} pdf_jpx_input;

static OPJ_SIZE_T jpx_read(void *destination, OPJ_SIZE_T amount, void *opaque) {
    pdf_jpx_input *input = opaque;
    if (!amount)
        return 0;
    if (input->position == input->length)
        return (OPJ_SIZE_T)-1;
    if (amount > input->length - input->position)
        amount = input->length - input->position;
    if (!cupidimage_jpx_codec_work(1 + amount / 64))
        return (OPJ_SIZE_T)-1;
    memcpy(destination, input->bytes + input->position, amount);
    input->position += amount;
    return amount;
}

static OPJ_OFF_T jpx_skip(OPJ_OFF_T amount, void *opaque) {
    pdf_jpx_input *input = opaque;
    if (!cupidimage_jpx_codec_work(1))
        return -1;
    if (amount < 0) {
        uint64_t distance = (uint64_t)(-(amount + 1)) + 1;
        if (distance > input->position)
            return -1;
        input->position -= (size_t)distance;
    } else {
        if ((uint64_t)amount > input->length - input->position)
            return -1;
        input->position += (size_t)amount;
    }
    return amount;
}

static OPJ_BOOL jpx_seek(OPJ_OFF_T position, void *opaque) {
    pdf_jpx_input *input = opaque;
    if (position < 0 || (uint64_t)position > input->length || !cupidimage_jpx_codec_work(1))
        return OPJ_FALSE;
    input->position = (size_t)position;
    return OPJ_TRUE;
}

static void jpx_error(const char *message, void *opaque) {
    pdf_jpx_input *input = opaque;
    cupidimage_set_err(input->error, input->error_capacity, message);
}

static void jpx_warning(const char *message, void *opaque) {
    pdf_jpx_input *input = opaque;
    input->warned = 1;
    cupidimage_set_err(input->error, input->error_capacity, message);
}

static int jpx_dimensions(const opj_image_t *image, size_t *samples) {
    *samples = 0;
    if (!image || !image->numcomps || image->numcomps > PDF_JPX_COMPONENTS ||
        image->x1 <= image->x0 || image->y1 <= image->y0 || image->x1 > INT32_MAX ||
        image->y1 > INT32_MAX ||
        (uint64_t)(image->x1 - image->x0) * (image->y1 - image->y0) > JPX_SAMPLE_LIMIT)
        return 0;
    for (unsigned c = 0; c < image->numcomps; c++) {
        const opj_image_comp_t *component = image->comps + c;
        uint64_t count = (uint64_t)component->w * component->h;
        if (!component->w || !component->h || !component->dx || !component->dy ||
            !component->prec || component->prec > 31 || component->sgnd > 1 ||
            count > JPX_SAMPLE_LIMIT - *samples)
            return 0;
        *samples += (size_t)count;
    }
    return 1;
}

void cupidimage_pdf_jpx_samples_free(pdf_jpx_samples *image) {
    free(image->storage);
    memset(image, 0, sizeof(*image));
}

int cupidimage_pdf_jpx_codestream(const unsigned char *data, size_t length, pdf_jpx_samples *out,
                                  size_t *work_left, char *error, size_t error_capacity) {
    memset(out, 0, sizeof(*out));
    if (!data || length < 2 || length > 64u * 1024u * 1024u || data[0] != 0xff || data[1] != 0x4f ||
        !work_left)
        return 0;
    pdf_jpx_budget budget;
    cupidimage_jpx_budget_begin(&budget, JPX_CODEC_MEMORY, work_left);
    pdf_jpx_input input = {data, length, 0, error, error_capacity, 0};
    opj_codec_t *codec = opj_create_decompress(OPJ_CODEC_J2K);
    opj_stream_t *stream = NULL;
    opj_image_t *image = NULL;
    int ok = 0;
    size_t count;
    if (!codec)
        goto done;
    opj_set_error_handler(codec, jpx_error, &input);
    opj_set_warning_handler(codec, jpx_warning, &input);
    opj_set_info_handler(codec, NULL, NULL);
    opj_dparameters_t parameters;
    opj_set_default_decoder_parameters(&parameters);
    if (!opj_setup_decoder(codec, &parameters) || !opj_decoder_set_strict_mode(codec, OPJ_TRUE) ||
        opj_has_thread_support())
        goto done;
    stream = opj_stream_create(16384, OPJ_TRUE);
    if (!stream)
        goto done;
    opj_stream_set_user_data(stream, &input, NULL);
    opj_stream_set_user_data_length(stream, length);
    opj_stream_set_read_function(stream, jpx_read);
    opj_stream_set_skip_function(stream, jpx_skip);
    opj_stream_set_seek_function(stream, jpx_seek);
    if (!opj_read_header(stream, codec, &image) || input.warned || !jpx_dimensions(image, &count) ||
        !opj_decode(codec, stream, image) || !opj_end_decompress(codec, stream) ||
        !jpx_dimensions(image, &count) || input.warned || !cupidimage_jpx_codec_work(count))
        goto done;
    out->storage = malloc(count * sizeof(*out->storage));
    if (!out->storage)
        goto done;
    out->x0 = image->x0;
    out->y0 = image->y0;
    out->width = image->x1 - image->x0;
    out->height = image->y1 - image->y0;
    out->components = image->numcomps;
    size_t offset = 0;
    for (unsigned c = 0; c < image->numcomps; c++) {
        const opj_image_comp_t *component = image->comps + c;
        size_t pixels = (size_t)component->w * component->h;
        if (!component->data)
            goto done;
        int64_t bias = component->sgnd ? INT64_C(1) << (component->prec - 1) : 0;
        int64_t maximum = (INT64_C(1) << component->prec) - 1;
        for (size_t i = 0; i < pixels; i++) {
            int64_t value = (int64_t)component->data[i] + bias;
            if (value < 0 || value > maximum)
                goto done;
            out->storage[offset + i] = (uint32_t)value;
        }
        out->planes[c] = (pdf_jpx_plane){component->x0,   component->y0,   component->w,
                                         component->h,    component->dx,   component->dy,
                                         component->prec, component->sgnd, out->storage + offset};
        offset += pixels;
    }
    ok = 1;
done:
    if (image)
        opj_image_destroy(image);
    if (stream)
        opj_stream_destroy(stream);
    if (codec)
        opj_destroy_codec(codec);
    if (!cupidimage_jpx_budget_end(&budget))
        ok = 0;
    if (!ok) {
        cupidimage_pdf_jpx_samples_free(out);
        cupidimage_set_err(error, error_capacity,
                           "pdf: invalid JPEG 2000 codestream or decoder limit exceeded");
    }
    return ok;
}
