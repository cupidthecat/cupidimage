#include "core/internal.h"
#include "cupidimage.h"
#include "pdf_render_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Borrow an inline dictionary or own a resolved indirect dictionary. The parent
 * view must stay alive until a borrowed result has been consumed. */
int cupidimage_pdf_image_dictionary(const pdf_render_state *st, const pdf_object_view *parent,
                                    const char *key, pdf_object_view *result) {
    size_t pos;
    pdf_object_id object;
    if (!cupidimage_pdf_page_value(parent, key, &pos))
        return 0;
    if (cupidimage_pdf_page_reference(parent, pos, &object)) {
        if (!cupidimage_pdf_state_object(st, object, result))
            return 0;
        pos = 0;
        cupidimage_pdf_skip_ws_and_comments(result->data, result->len, &pos);
        if (result->len - pos < 2 || result->data[pos] != '<' || result->data[pos + 1] != '<') {
            cupidimage_pdf_object_view_release(result);
            return 0;
        }
        return 1;
    }
    if (pos + 1 >= parent->len || parent->data[pos] != '<' || parent->data[pos + 1] != '<')
        return 0;
    result->data = parent->data + pos;
    result->len = parent->len - pos;
    return 1;
}

static int pdf_image_object(const pdf_render_state *st, const char *name, pdf_object_id *object) {
    return cupidimage_pdf_find_resource(st, "XObject", name, NULL, object) && *object > 0;
}

int cupidimage_pdf_image_integer(const pdf_render_state *st, const pdf_object_view *view,
                                 const char *key, int *value) {
    return cupidimage_pdf_dictionary_integer(st->document, st->document_size, view, key, value);
}

int cupidimage_pdf_image_name_is(const pdf_object_view *view, const char *key, const char *name) {
    size_t pos, length = strlen(name);
    if (!cupidimage_pdf_page_value(view, key, &pos) || pos >= view->len || view->data[pos++] != '/')
        return 0;
    return length <= view->len - pos && memcmp(view->data + pos, name, length) == 0 &&
           (pos + length == view->len || cupidimage_pdf_is_ws(view->data[pos + length]) ||
            cupidimage_pdf_is_delim(view->data[pos + length]));
}

int cupidimage_pdf_image_stream(const pdf_render_state *st, const pdf_object_view *view,
                                const unsigned char **stream, size_t *length) {
    return cupidimage_pdf_object_stream(st->document, st->document_size, view, stream, length);
}

static unsigned char pdf_image_paeth(int a, int b, int c) {
    int p = a + b - c, da = abs(p - a), db = abs(p - b), dc = abs(p - c);
    return (unsigned char)(da <= db && da <= dc ? a : db <= dc ? b : c);
}

int cupidimage_pdf_image_filter_is(const pdf_object_view *view, const char *name) {
    if (cupidimage_pdf_image_name_is(view, "Filter", name))
        return 1;
    size_t pos, length = strlen(name);
    if (!cupidimage_pdf_page_value(view, "Filter", &pos) || pos >= view->len ||
        view->data[pos++] != '[')
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    if (pos >= view->len || view->data[pos++] != '/' || length > view->len - pos ||
        memcmp(view->data + pos, name, length) != 0)
        return 0;
    pos += length;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    return pos < view->len && view->data[pos] == ']';
}

/* Check the frame before invoking the decoder so a small image dictionary cannot
 * hide a JPEG frame with enormous dimensions or a different component count. */
static int pdf_image_jpeg_matches(const unsigned char *stream, size_t length, int width, int height,
                                  int components, int transform) {
    if (length < 2 || stream[0] != 0xff || stream[1] != 0xd8)
        return 0;
    size_t pos = 2;
    int have_frame = 0;
    while (pos < length) {
        if (stream[pos++] != 0xff)
            return 0;
        while (pos < length && stream[pos] == 0xff)
            pos++;
        if (pos >= length)
            return 0;
        unsigned marker = stream[pos++];
        if (marker == 0xda)
            return have_frame;
        if (marker == 0xd9 || marker == 0)
            return 0;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
            continue;
        if (length - pos < 2)
            return 0;
        size_t segment = (size_t)stream[pos] * 256u + stream[pos + 1];
        if (segment < 2 || segment > length - pos)
            return 0;
        if (components == 3 && marker == 0xee && segment >= 14 &&
            memcmp(stream + pos + 2, "Adobe", 5) == 0 && stream[pos + 13] == 0 && transform != 1)
            return 0;
        if (marker == 0xc0 || marker == 0xc2) {
            if (have_frame || !(segment >= 8 && stream[pos + 2] == 8 &&
                                (int)stream[pos + 3] * 256 + stream[pos + 4] == height &&
                                (int)stream[pos + 5] * 256 + stream[pos + 6] == width &&
                                stream[pos + 7] == components))
                return 0;
            have_frame = 1;
        }
        pos += segment;
    }
    return 0;
}

static int pdf_image_jpeg_samples(const pdf_render_state *st, const pdf_object_view *view,
                                  int width, int height, int components, unsigned char **samples) {
    pdf_object_view params = {0};
    size_t pos;
    int transform = -1;
    if (cupidimage_pdf_page_value(view, "DecodeParms", &pos)) {
        int ok = cupidimage_pdf_image_dictionary(st, view, "DecodeParms", &params) &&
                 (!cupidimage_pdf_page_value(&params, "ColorTransform", &pos) ||
                  cupidimage_pdf_image_integer(st, &params, "ColorTransform", &transform));
        cupidimage_pdf_object_view_release(&params);
        if (!ok || (transform != -1 && transform != 1))
            return 0;
    }
    const unsigned char *stream;
    size_t length;
    if (!cupidimage_pdf_image_stream(st, view, &stream, &length) ||
        !pdf_image_jpeg_matches(stream, length, width, height, components, transform))
        return 0;
    cupidimage_image image = {0};
    if (!cupidimage_load_jpeg(stream, length, &image, NULL, 0))
        return 0;
    size_t count = (size_t)width * height;
    unsigned char *decoded = (unsigned char *)malloc(count * components);
    if (!decoded || image.width != (uint32_t)width || image.height != (uint32_t)height) {
        free(decoded);
        cupidimage_free(&image);
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        for (int c = 0; c < components; c++)
            decoded[i * components + c] = image.rgba[i * 4 + c];
    }
    cupidimage_free(&image);
    *samples = decoded;
    return 1;
}

static uint32_t pdf_packed_sample(const unsigned char *row, size_t index, int bits) {
    if (bits == 8)
        return row[index];
    if (bits == 16)
        return (uint32_t)row[index * 2] * 256u + row[index * 2 + 1];
    size_t bit = index * (unsigned)bits;
    return (row[bit / 8] >> (8 - bits - (int)(bit & 7))) & ((1u << bits) - 1u);
}

static void pdf_store_packed_sample(unsigned char *row, size_t index, int bits, uint32_t value) {
    if (bits == 8) {
        row[index] = (unsigned char)value;
        return;
    }
    if (bits == 16) {
        row[index * 2] = (unsigned char)(value >> 8);
        row[index * 2 + 1] = (unsigned char)value;
        return;
    }
    size_t bit = index * (unsigned)bits;
    unsigned shift = 8u - (unsigned)bits - (unsigned)(bit & 7);
    unsigned mask = ((1u << bits) - 1u) << shift;
    row[bit / 8] = (unsigned char)((row[bit / 8] & ~mask) | ((value << shift) & mask));
}

static int pdf_image_samples(const pdf_render_state *st, const pdf_object_view *view, int width,
                             int height, int components, int image_bits, unsigned char **samples) {
    const unsigned char *stream;
    size_t length, row = ((size_t)width * components * image_bits + 7u) / 8u;
    size_t bytes = row * height, pos;
    if (bytes > 64u * 1024u * 1024u)
        return 0;
    if (cupidimage_pdf_image_filter_is(view, "DCTDecode"))
        return image_bits == 8 && (components == 1 || components == 3) &&
               pdf_image_jpeg_samples(st, view, width, height, components, samples);
    int predictor = 1, flate = cupidimage_pdf_image_filter_is(view, "FlateDecode");
    if (cupidimage_pdf_page_value(view, "Filter", &pos) && !flate) {
        unsigned char *decoded = NULL;
        size_t decoded_length = 0;
        if (!cupidimage_pdf_image_stream(st, view, &stream, &length) ||
            !cupidimage_pdf_decode_stream(view, stream, length, &decoded, &decoded_length))
            return 0;
        if (decoded_length != bytes) {
            free(decoded);
            return 0;
        }
        *samples = decoded;
        return 1;
    }
    pdf_object_view params = {0};
    if (cupidimage_pdf_page_value(view, "DecodeParms", &pos)) {
        if (!flate || !cupidimage_pdf_image_dictionary(st, view, "DecodeParms", &params))
            return 0;
        int columns = 1, colors = 1, bits = 8;
        int ok = (!cupidimage_pdf_page_value(&params, "Predictor", &pos) ||
                  cupidimage_pdf_image_integer(st, &params, "Predictor", &predictor)) &&
                 (!cupidimage_pdf_page_value(&params, "Columns", &pos) ||
                  cupidimage_pdf_image_integer(st, &params, "Columns", &columns)) &&
                 (!cupidimage_pdf_page_value(&params, "Colors", &pos) ||
                  cupidimage_pdf_image_integer(st, &params, "Colors", &colors)) &&
                 (!cupidimage_pdf_page_value(&params, "BitsPerComponent", &pos) ||
                  cupidimage_pdf_image_integer(st, &params, "BitsPerComponent", &bits));
        cupidimage_pdf_object_view_release(&params);
        if (!ok ||
            (predictor != 1 && (columns != width || colors != components || bits != image_bits)))
            return 0;
    }
    if (predictor != 1 && predictor != 2 && (predictor < 10 || predictor > 15))
        return 0;
    if (!cupidimage_pdf_image_stream(st, view, &stream, &length))
        return 0;
    size_t encoded_row = row + (predictor >= 10 ? 1u : 0u);
    size_t expected = encoded_row * height;
    if (expected > 64u * 1024u * 1024u)
        return 0;
    unsigned char *decoded = (unsigned char *)malloc(expected);
    if (!decoded)
        return 0;
    size_t actual = 0;
    if (flate) {
        if (!cupidimage_inflate_zlib(stream, length, decoded, expected, &actual) ||
            actual != expected)
            goto fail;
    } else {
        if (length != bytes)
            goto fail;
        memcpy(decoded, stream, bytes);
    }
    size_t bpp = ((unsigned)components * (unsigned)image_bits + 7u) / 8u;
    uint32_t maximum = (1u << image_bits) - 1u;
    for (int y = 0; y < height; y++) {
        unsigned char *scan = decoded + (size_t)y * encoded_row;
        int filter = predictor >= 10 ? *scan++ : 0;
        if (filter > 4)
            goto fail;
        if (predictor == 2) {
            for (size_t sample = (size_t)components; sample < (size_t)width * components;
                 sample++) {
                uint32_t value = pdf_packed_sample(scan, sample, image_bits) +
                                 pdf_packed_sample(scan, sample - components, image_bits);
                pdf_store_packed_sample(scan, sample, image_bits, value & maximum);
            }
        } else
            for (size_t x = 0; x < row; x++) {
                int left = x >= bpp ? scan[x - bpp] : 0;
                int up = y ? decoded[(size_t)(y - 1) * row + x] : 0;
                int upper_left = y && x >= bpp ? decoded[(size_t)(y - 1) * row + x - bpp] : 0;
                if (filter == 1)
                    scan[x] = (unsigned char)(scan[x] + left);
                else if (filter == 2)
                    scan[x] = (unsigned char)(scan[x] + up);
                else if (filter == 3)
                    scan[x] = (unsigned char)(scan[x] + (left + up) / 2);
                else if (filter == 4)
                    scan[x] = (unsigned char)(scan[x] + pdf_image_paeth(left, up, upper_left));
            }
        memmove(decoded + (size_t)y * row, scan, row);
    }
    *samples = decoded;
    return 1;
fail:
    free(decoded);
    return 0;
}

/* Image masks share the parent image's unit square, not its sample grid.
 * ISO 32000-1, 8.9.6.3 and 11.6.5.3. Never follow masks recursively. */
typedef struct {
    unsigned char *samples;
    int width, height, bits, soft, has_matte;
    size_t row;
    double decode[2], matte[4];
} pdf_image_mask;

static int pdf_image_array(const pdf_object_view *view, const char *key, double *values,
                           int count) {
    size_t pos;
    if (!cupidimage_pdf_page_value(view, key, &pos) || pos >= view->len || view->data[pos++] != '[')
        return 0;
    for (int i = 0; i < count; i++) {
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
        if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, &pos, &values[i]) ||
            !isfinite(values[i]))
            return 0;
    }
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    return pos < view->len && view->data[pos] == ']';
}

static int pdf_image_boolean(const pdf_object_view *view, const char *key, int *value) {
    size_t pos;
    if (!cupidimage_pdf_page_value(view, key, &pos))
        return 1;
    for (int b = 0; b < 2; b++) {
        const char *word = b ? "true" : "false";
        size_t n = strlen(word);
        if (n <= view->len - pos && memcmp(view->data + pos, word, n) == 0 &&
            (pos + n == view->len || cupidimage_pdf_is_ws(view->data[pos + n]) ||
             cupidimage_pdf_is_delim(view->data[pos + n]))) {
            *value = b;
            return 1;
        }
    }
    return 0;
}

static int pdf_load_image_mask(const pdf_render_state *st, const pdf_object_view *parent,
                               const char *key, int width, int height, int components,
                               pdf_image_mask *mask) {
    pdf_object_view view = {0};
    pdf_object_id object;
    int stencil = 0, ok = 0;
    size_t pos;
    mask->soft = strcmp(key, "SMask") == 0;
    mask->decode[0] = 0;
    mask->decode[1] = 1;
    if (!cupidimage_pdf_page_value(parent, key, &pos) ||
        !cupidimage_pdf_page_reference(parent, pos, &object) ||
        !cupidimage_pdf_state_object(st, object, &view))
        goto done;
    if (!cupidimage_pdf_image_name_is(&view, "Subtype", "Image") ||
        (cupidimage_pdf_page_value(&view, "Type", &pos) &&
         !cupidimage_pdf_image_name_is(&view, "Type", "XObject")) ||
        !pdf_image_boolean(&view, "ImageMask", &stencil) || stencil == mask->soft ||
        cupidimage_pdf_page_value(&view, "Mask", &pos) ||
        cupidimage_pdf_page_value(&view, "SMask", &pos) ||
        !cupidimage_pdf_image_integer(st, &view, "Width", &mask->width) ||
        !cupidimage_pdf_image_integer(st, &view, "Height", &mask->height) || mask->width < 1 ||
        mask->height < 1 || (double)mask->width * mask->height > 64.0 * 1024 * 1024)
        goto done;
    mask->bits = 1;
    if ((mask->soft || cupidimage_pdf_page_value(&view, "BitsPerComponent", &pos)) &&
        !cupidimage_pdf_image_integer(st, &view, "BitsPerComponent", &mask->bits))
        goto done;
    if (mask->soft) {
        if (!cupidimage_pdf_image_name_is(&view, "ColorSpace", "DeviceGray") ||
            (mask->bits != 1 && mask->bits != 2 && mask->bits != 4 && mask->bits != 8 &&
             mask->bits != 16))
            goto done;
    } else if (mask->bits != 1 || cupidimage_pdf_page_value(&view, "ColorSpace", &pos))
        goto done;
    if (cupidimage_pdf_page_value(&view, "Decode", &pos) &&
        !pdf_image_array(&view, "Decode", mask->decode, 2))
        goto done;
    if (!mask->soft && !((mask->decode[0] == 0 && mask->decode[1] == 1) ||
                         (mask->decode[0] == 1 && mask->decode[1] == 0)))
        goto done;
    mask->has_matte = cupidimage_pdf_page_value(&view, "Matte", &pos);
    if (mask->has_matte) {
        if (!mask->soft || mask->width != width || mask->height != height ||
            !pdf_image_array(&view, "Matte", mask->matte, components))
            goto done;
        for (int c = 0; c < components; c++)
            if (mask->matte[c] < 0 || mask->matte[c] > 1)
                goto done;
    }
    if (!pdf_image_samples(st, &view, mask->width, mask->height, 1, mask->bits, &mask->samples))
        goto done;
    mask->row = ((size_t)mask->width * mask->bits + 7u) / 8u;
    ok = 1;
done:
    cupidimage_pdf_object_view_release(&view);
    return ok;
}

static double pdf_image_mask_alpha(const pdf_image_mask *mask, double u, double v) {
    if (!mask->samples)
        return 1;
    int x = (int)(u * mask->width), y = (int)((1 - v) * mask->height);
    if (x >= mask->width || y >= mask->height)
        return 0;
    uint32_t sample =
        pdf_packed_sample(mask->samples + (size_t)y * mask->row, (size_t)x, mask->bits);
    double alpha = mask->decode[0] +
                   (double)sample / ((1u << mask->bits) - 1u) * (mask->decode[1] - mask->decode[0]);
    alpha = fmin(1, fmax(0, alpha));
    return mask->soft ? alpha : 1 - alpha;
}

int cupidimage_pdf_draw_image_view(pdf_render_state *st, const pdf_object_view *view) {
    st->paint_serial = ++st->form_budget->paint_serial;
    pdf_object_view image = *view;
    image.owned = NULL; /* The caller retains the dictionary and stream. */
    unsigned char *samples = NULL;
    pdf_image_mask mask = {0};
    pdf_soft_mask *graphics_mask = st->gs.soft_mask;
    pdf_image_colors colors = {0};
    int width, height, bits, components, ok = 0;
    size_t pos;
    if (!cupidimage_pdf_image_name_is(&image, "Subtype", "Image"))
        goto done;
    int stencil = 0;
    if (cupidimage_pdf_page_value(&image, "ImageMask", &pos)) {
        size_t n = image.len - pos;
        if (n >= 4 && memcmp(image.data + pos, "true", 4) == 0 &&
            (n == 4 || cupidimage_pdf_is_ws(image.data[pos + 4]) ||
             cupidimage_pdf_is_delim(image.data[pos + 4])))
            stencil = 1;
        else if (!(n >= 5 && memcmp(image.data + pos, "false", 5) == 0 &&
                   (n == 5 || cupidimage_pdf_is_ws(image.data[pos + 5]) ||
                    cupidimage_pdf_is_delim(image.data[pos + 5]))))
            goto done;
    }
    if ((st->glyph_uncolored || st->pattern_uncolored) && !stencil)
        goto done;
    if (!stencil && (!cupidimage_pdf_page_value(&image, "ColorSpace", &pos) ||
                     !cupidimage_pdf_image_color_space(st, &image, &pos, 0, &colors)))
        goto done;
    if (colors.pattern)
        goto done;
    components = stencil || colors.indexed ? 1 : colors.components;
    bits = stencil ? 1 : 8;
    if (!components || !cupidimage_pdf_image_integer(st, &image, "Width", &width) ||
        !cupidimage_pdf_image_integer(st, &image, "Height", &height) ||
        ((!stencil || cupidimage_pdf_page_value(&image, "BitsPerComponent", &pos)) &&
         !cupidimage_pdf_image_integer(st, &image, "BitsPerComponent", &bits)) ||
        (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 16) ||
        (stencil && (bits != 1 || cupidimage_pdf_page_value(&image, "ColorSpace", &pos))) ||
        width < 1 || height < 1 || (double)width * height * components > 64.0 * 1024 * 1024)
        goto done;
    double decode[8] = {0, 1, 0, 1, 0, 1, 0, 1};
    if (colors.indexed)
        decode[1] = (double)((1u << bits) - 1u);
    if (cupidimage_pdf_page_value(&image, "Decode", &pos)) {
        if (pos >= image.len || image.data[pos++] != '[')
            goto done;
        for (int i = 0; i < 2 * components; i++) {
            cupidimage_pdf_skip_ws_and_comments(image.data, image.len, &pos);
            if (!cupidimage_pdf_parse_pdf_number(image.data, image.len, &pos, &decode[i]))
                goto done;
        }
        cupidimage_pdf_skip_ws_and_comments(image.data, image.len, &pos);
        if (pos >= image.len || image.data[pos] != ']')
            goto done;
    }
    if (stencil && !((decode[0] == 0 && decode[1] == 1) || (decode[0] == 1 && decode[1] == 0)))
        goto done;
    uint32_t color_key[8] = {0};
    uint32_t maximum = (1u << bits) - 1u;
    int has_color_key = 0;
    if (cupidimage_pdf_page_value(&image, "SMask", &pos)) {
        if (stencil || !pdf_load_image_mask(st, &image, "SMask", width, height, components, &mask))
            goto done;
    } else if (cupidimage_pdf_page_value(&image, "Mask", &pos)) {
        if (stencil)
            goto done;
        if (pos < image.len && image.data[pos] == '[')
            has_color_key = 1;
        else if (!pdf_load_image_mask(st, &image, "Mask", width, height, components, &mask))
            goto done;
    }
    if (colors.indexed && mask.has_matte)
        goto done;
    if (has_color_key) {
        if (stencil || pos >= image.len || image.data[pos++] != '[')
            goto done;
        for (int i = 0; i < components * 2; i++) {
            double value;
            cupidimage_pdf_skip_ws_and_comments(image.data, image.len, &pos);
            if (!cupidimage_pdf_parse_pdf_number(image.data, image.len, &pos, &value) ||
                value < 0 || value > maximum || floor(value) != value)
                goto done;
            color_key[i] = (uint32_t)value;
            if ((i & 1) && color_key[i] < color_key[i - 1])
                goto done;
        }
        cupidimage_pdf_skip_ws_and_comments(image.data, image.len, &pos);
        if (pos >= image.len || image.data[pos] != ']')
            goto done;
    }
    if (!pdf_image_samples(st, &image, width, height, components, bits, &samples))
        goto done;
    size_t sample_row = ((size_t)width * components * bits + 7u) / 8u;
    const float *m = st->gs.ctm;
    double det = (double)m[0] * m[3] - (double)m[1] * m[2];
    if (!isfinite(det))
        goto done;
    if (det == 0) {
        ok = 1;
        goto done;
    }
    double min_x = st->img->width, max_x = 0, min_y = st->img->height, max_y = 0;
    for (int corner = 0; corner < 4; corner++) {
        float x, y;
        cupidimage_pdf_user_to_pixel(st, (float)(corner & 1), (float)(corner >> 1), &x, &y);
        if (!isfinite(x) || !isfinite(y))
            goto done;
        min_x = fmin(min_x, x);
        max_x = fmax(max_x, x);
        min_y = fmin(min_y, y);
        max_y = fmax(max_y, y);
    }
    /* Clamp before integer conversion, including for enormous off-page transforms. */
    int x0 = (int)fmax(0, fmin(st->img->width, floor(min_x)));
    int x1 = (int)fmax(0, fmin(st->img->width, ceil(max_x)));
    int y0 = (int)fmax(0, fmin(st->img->height, floor(min_y)));
    int y1 = (int)fmax(0, fmin(st->img->height, ceil(max_y)));
    /* An image's explicit, color-key or soft mask replaces the graphics-state mask. */
    if (mask.samples || has_color_key)
        st->gs.soft_mask = NULL;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            double tx = (x + 0.5) * (72.0 / 96.0) + st->page_x0_pts - m[4];
            double ty = st->page_top_pts - (y + 0.5) * (72.0 / 96.0) - m[5];
            double u = (m[3] * tx - m[2] * ty) / det;
            double v = (m[0] * ty - m[1] * tx) / det;
            if (u < 0 || u >= 1 || v <= 0 || v > 1)
                continue;
            int sx = (int)(u * width), sy = (int)((1 - v) * height);
            if (sx >= width || sy >= height)
                continue;
            const unsigned char *sample = samples + (size_t)sy * sample_row;
            double values[4] = {0};
            int transparent = has_color_key;
            for (int c = 0; c < components; c++) {
                uint32_t value = pdf_packed_sample(sample, (size_t)sx * components + c, bits);
                if (has_color_key && (value < color_key[c * 2] || value > color_key[c * 2 + 1]))
                    transparent = 0;
                values[c] =
                    decode[2 * c] + ((double)value / maximum) * (decode[2 * c + 1] - decode[2 * c]);
            }
            if (transparent || (stencil && values[0] >= 0.5))
                continue;
            double alpha = pdf_image_mask_alpha(&mask, u, v);
            if (alpha <= 0 && (!mask.soft || st->gs.alpha_is_shape))
                continue;
            int paint_components = components;
            if (colors.indexed) {
                unsigned index = (unsigned)floor(fmin(colors.high, fmax(0, values[0])) + 0.5);
                paint_components = colors.components;
                for (int c = 0; c < paint_components; c++)
                    values[c] = colors.palette[index * paint_components + c] / 255.0;
            }
            if (mask.has_matte && alpha > 0) {
                for (int c = 0; c < components; c++)
                    values[c] = mask.matte[c] + (values[c] - mask.matte[c]) / alpha;
            }
            if (stencil)
                cupidimage_pdf_paint_pixel(st, x, y, 0, alpha);
            else
                cupidimage_pdf_composite_device(
                    st, x, y, values, paint_components, alpha * st->gs.fill_alpha,
                    st->gs.alpha_is_shape ? alpha * st->gs.fill_alpha : 1);
        }
    }
    ok = 1;
done:
    cupidimage_pdf_patterns_end_paint(st);
    st->gs.soft_mask = graphics_mask;
    free(mask.samples);
    free(samples);
    cupidimage_pdf_object_view_release(&image);
    if (!ok)
        cupidimage_set_err(st->error, st->error_capacity,
                           "pdf: invalid or unsupported image XObject");
    return ok;
}

int cupidimage_pdf_draw_image(pdf_render_state *st, const char *name) {
    pdf_object_view image = {0};
    pdf_object_id object;
    int ok = 0;
    if (pdf_image_object(st, name, &object) && cupidimage_pdf_state_object(st, object, &image)) {
        if (cupidimage_pdf_image_name_is(&image, "Subtype", "Form"))
            ok = cupidimage_pdf_draw_form(st, object, &image);
        else
            ok = cupidimage_pdf_draw_image_view(st, &image);
    }
    cupidimage_pdf_object_view_release(&image);
    if (!ok && st->error && st->error_capacity && !st->error[0])
        cupidimage_set_err(st->error, st->error_capacity, "pdf: invalid image XObject");
    return ok;
}
