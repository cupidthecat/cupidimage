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

static uint32_t pdf_packed_sample(const unsigned char *row, size_t index, int bits) {
    if (bits == 8)
        return row[index];
    if (bits == 16)
        return (uint32_t)row[index * 2] * 256u + row[index * 2 + 1];
    size_t bit = index * (unsigned)bits;
    return (row[bit / 8] >> (8 - bits - (int)(bit & 7))) & ((1u << bits) - 1u);
}

static int pdf_image_samples(const pdf_render_state *st, const pdf_object_view *view, int width,
                             int height, int components, int image_bits, unsigned char **samples) {
    const unsigned char *stream;
    size_t length, row = ((size_t)width * components * image_bits + 7u) / 8u;
    size_t bytes = row * height, decoded_length = 0;
    if (bytes > 64u * 1024u * 1024u)
        return 0;
    pdf_image_stream_profile profile = {width,      height,       components,
                                        image_bits, st->document, st->document_size};
    unsigned char *decoded = NULL;
    if (!cupidimage_pdf_image_stream(st, view, &stream, &length) ||
        !cupidimage_pdf_decode_image_stream(view, stream, length, &decoded, &decoded_length,
                                            &profile))
        return 0;
    if (decoded_length != bytes) {
        free(decoded);
        return 0;
    }
    *samples = decoded;
    return 1;
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
