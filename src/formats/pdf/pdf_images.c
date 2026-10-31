#include "core/internal.h"
#include "cupidimage.h"
#include "images/jpx/source.h"
#include "images/sample.h"
#include "pdf_render_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Borrow an inline dictionary or own a resolved indirect dictionary. The parent
 * view must stay alive until a borrowed result has been consumed. */
int cupidimage_pdf_image_dictionary(const pdf_render_state *st, const pdf_object_view *parent,
                                    const char *key, pdf_object_view *result) {
    pdf_object_view value = {0}, dictionary = {0};
    size_t end;
    if (cupidimage_pdf_optional_value(st->document, st->document_size, parent, key, &value) != 1)
        return 0;
    if (!cupidimage_pdf_dictionary_view(&value, 0, &dictionary, &end)) {
        cupidimage_pdf_object_view_release(&value);
        return 0;
    }
    /* Shading entries can be streams; retain the complete resolved object. */
    *result = value;
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
    pdf_image_stream_profile profile = {
        width, height, components, image_bits, st->document, st->document_size, 0};
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
    pdf_jpx_source jpx;
    const pdf_jpx_source *embedded;
    unsigned embedded_channel;
    int width, height, bits, soft, has_matte, interpolate;
    size_t row;
    double decode[2], matte[PDF_COLOR_COMPONENTS];
} pdf_image_mask;

static int pdf_image_numbers(const pdf_render_state *st, const pdf_object_view *view,
                             double *values, int count) {
    size_t pos = 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    if (pos >= view->len || view->data[pos++] != '[')
        return 0;
    for (int i = 0; i < count; i++) {
        if (!cupidimage_pdf_resolved_number(st->document, st->document_size, view, &pos,
                                            &values[i]) ||
            !isfinite(values[i]))
            return 0;
    }
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    return pos < view->len && view->data[pos] == ']';
}

/* Missing and null optional entries keep the caller's defaults. */
static int pdf_image_array(const pdf_render_state *st, const pdf_object_view *view, const char *key,
                           double *values, int count) {
    pdf_object_view array = {0};
    int status = cupidimage_pdf_optional_value(st->document, st->document_size, view, key, &array);
    if (status == 1 && !pdf_image_numbers(st, &array, values, count))
        status = -1;
    cupidimage_pdf_object_view_release(&array);
    return status;
}

static int pdf_image_present(const pdf_render_state *st, const pdf_object_view *view,
                             const char *key) {
    pdf_object_view value = {0};
    int status = cupidimage_pdf_optional_value(st->document, st->document_size, view, key, &value);
    cupidimage_pdf_object_view_release(&value);
    return status;
}

static int pdf_image_boolean(const pdf_render_state *st, const pdf_object_view *view,
                             const char *key, int *value) {
    pdf_object_view entry = {0};
    int status = cupidimage_pdf_optional_value(st->document, st->document_size, view, key, &entry);
    if (status <= 0)
        return status == 0;
    size_t pos = 0;
    cupidimage_pdf_skip_ws_and_comments(entry.data, entry.len, &pos);
    int ok = 0;
    for (int b = 0; b < 2; b++) {
        const char *word = b ? "true" : "false";
        size_t n = strlen(word);
        if (n <= entry.len - pos && memcmp(entry.data + pos, word, n) == 0 &&
            (pos + n == entry.len || cupidimage_pdf_is_ws(entry.data[pos + n]) ||
             cupidimage_pdf_is_delim(entry.data[pos + n]))) {
            *value = b;
            ok = 1;
            break;
        }
    }
    cupidimage_pdf_object_view_release(&entry);
    return ok;
}

static int pdf_image_type(const pdf_render_state *st, const pdf_object_view *view) {
    int present = pdf_image_present(st, view, "Type");
    return present == 0 ||
           (present == 1 &&
            cupidimage_pdf_entry_is_name(st->document, st->document_size, view, "Type", "XObject"));
}

static int pdf_load_image_mask(const pdf_render_state *st, const pdf_object_view *view, int soft,
                               int width, int height, int components, const pdf_image_colors *space,
                               pdf_image_mask *mask) {
    int stencil = 0;
    mask->soft = soft;
    mask->decode[0] = 0;
    mask->decode[1] = 1;
    if (!cupidimage_pdf_entry_is_name(st->document, st->document_size, view, "Subtype", "Image") ||
        !pdf_image_type(st, view) || !pdf_image_boolean(st, view, "ImageMask", &stencil) ||
        !pdf_image_boolean(st, view, "Interpolate", &mask->interpolate) || stencil == mask->soft ||
        pdf_image_present(st, view, "Mask") != 0 || pdf_image_present(st, view, "SMask") != 0 ||
        !cupidimage_pdf_image_integer(st, view, "Width", &mask->width) ||
        !cupidimage_pdf_image_integer(st, view, "Height", &mask->height) || mask->width < 1 ||
        mask->height < 1 || (double)mask->width * mask->height > 64.0 * 1024 * 1024)
        return 0;
    if (!cupidimage_pdf_jpx_source_load(st, view, &mask->jpx))
        return 0;
    mask->bits = 1;
    int has_bits = mask->jpx.present ? 0 : pdf_image_present(st, view, "BitsPerComponent");
    if (has_bits < 0 || (!mask->jpx.present && (mask->soft || has_bits) &&
                         !cupidimage_pdf_image_integer(st, view, "BitsPerComponent", &mask->bits)))
        return 0;
    if (mask->soft) {
        if (!cupidimage_pdf_entry_is_name(st->document, st->document_size, view, "ColorSpace",
                                          "DeviceGray") ||
            (mask->bits != 1 && mask->bits != 2 && mask->bits != 4 && mask->bits != 8 &&
             mask->bits != 16))
            return 0;
    } else if (mask->bits != 1 || pdf_image_present(st, view, "ColorSpace") != 0)
        return 0;
    if (pdf_image_array(st, view, "Decode", mask->decode, 2) < 0)
        return 0;
    if (!mask->soft && !((mask->decode[0] == 0 && mask->decode[1] == 1) ||
                         (mask->decode[0] == 1 && mask->decode[1] == 0)))
        return 0;
    int matte = pdf_image_array(st, view, "Matte", mask->matte, components);
    if (matte < 0)
        return 0;
    mask->has_matte = matte == 1;
    if (mask->has_matte) {
        if (!mask->soft || mask->width != width || mask->height != height)
            return 0;
        for (int c = 0; c < components; c++)
            if (cupidimage_pdf_color_component(space, c, mask->matte[c]) != mask->matte[c])
                return 0;
    }
    if (mask->jpx.present) {
        uint64_t value;
        unsigned precision;
        int is_signed;
        if (mask->jpx.file.ordinary != 1 ||
            !cupidimage_pdf_jpx_channel_sample(&mask->jpx.file, mask->jpx.file.color_order[0], 0, 0,
                                               &value, &precision, &is_signed) ||
            (!mask->soft && precision != 1))
            return 0;
        mask->bits = (int)precision;
    } else if (!pdf_image_samples(st, view, mask->width, mask->height, 1, mask->bits,
                                  &mask->samples))
        return 0;
    mask->row = ((size_t)mask->width * mask->bits + 7u) / 8u;
    return 1;
}

static double pdf_image_mask_alpha(const pdf_image_mask *mask, double u, double v) {
    if (!mask->samples && !mask->jpx.present && !mask->embedded)
        return 1;
    pdf_image_tap taps[4];
    unsigned count;
    if (!cupidimage_pdf_image_taps(mask->width, mask->height, u, v, mask->interpolate, taps,
                                   &count))
        return 0;
    double result = 0;
    for (unsigned i = 0; i < count; i++) {
        uint64_t sample;
        unsigned precision = (unsigned)mask->bits;
        int is_signed;
        if (mask->jpx.present || mask->embedded) {
            const pdf_jpx_file *file = mask->embedded ? &mask->embedded->file : &mask->jpx.file;
            unsigned channel = mask->embedded ? mask->embedded_channel : file->color_order[0];
            if (!cupidimage_pdf_jpx_channel_sample(file, channel, taps[i].x, taps[i].y, &sample,
                                                   &precision, &is_signed))
                return 0;
        } else
            sample = pdf_packed_sample(mask->samples + (size_t)taps[i].y * mask->row, taps[i].x,
                                       mask->bits);
        double alpha = mask->decode[0] + (double)sample / ((UINT64_C(1) << precision) - 1) *
                                             (mask->decode[1] - mask->decode[0]);
        alpha = fmin(1, fmax(0, alpha));
        result += taps[i].weight * (mask->soft ? alpha : 1 - alpha);
    }
    return result;
}

typedef struct {
    const unsigned char *samples;
    const double *decode;
    const uint64_t *color_key;
    const pdf_image_colors *colors;
    const pdf_image_mask *mask;
    size_t row;
    int width, height, components, bits, stencil, has_color_key;
    const pdf_jpx_source *jpx;
    const double *sample_zero;
    int premultiplied;
} pdf_image_grid;

static uint64_t pdf_image_grid_sample(const pdf_image_grid *grid, int x, int y, int c,
                                      uint64_t *maximum) {
    unsigned precision = (unsigned)grid->bits;
    uint64_t value = 0;
    if (grid->jpx && grid->jpx->present) {
        int is_signed;
        cupidimage_pdf_jpx_channel_sample(&grid->jpx->file, grid->jpx->file.color_order[c],
                                          (unsigned)x, (unsigned)y, &value, &precision, &is_signed);
    } else
        value = pdf_packed_sample(grid->samples + (size_t)y * grid->row,
                                  (size_t)x * grid->components + c, grid->bits);
    *maximum = (UINT64_C(1) << precision) - 1;
    return value;
}

/* Interpolate decoded components, expanding palette indices first. Color-key
 * coverage is filtered with associated components so hidden colors do not bleed. */
static double pdf_image_filtered_values(const pdf_image_grid *grid, double u, double v,
                                        double values[PDF_COLOR_COMPONENTS]) {
    pdf_image_tap taps[4];
    unsigned count;
    if (!cupidimage_pdf_image_taps(grid->width, grid->height, u, v, 1, taps, &count))
        return 0;
    double coverage = 0, color_weight = 0;
    for (unsigned i = 0; i < count; i++) {
        double decoded[PDF_COLOR_COMPONENTS] = {0};
        int transparent = grid->has_color_key;
        double embedded_alpha =
            grid->premultiplied ? pdf_image_mask_alpha(grid->mask, (taps[i].x + .5) / grid->width,
                                                       1 - (taps[i].y + .5) / grid->height)
                                : 1;
        for (int c = 0; c < grid->components; c++) {
            uint64_t maximum;
            uint64_t raw = pdf_image_grid_sample(grid, taps[i].x, taps[i].y, c, &maximum);
            if (grid->has_color_key &&
                (raw < grid->color_key[2 * c] || raw > grid->color_key[2 * c + 1]))
                transparent = 0;
            double unit = (double)raw / maximum;
            if (grid->premultiplied)
                unit = embedded_alpha > 0
                           ? grid->sample_zero[c] + (unit - grid->sample_zero[c]) / embedded_alpha
                           : grid->sample_zero[c];
            decoded[c] =
                grid->decode[2 * c] + unit * (grid->decode[2 * c + 1] - grid->decode[2 * c]);
        }
        if (transparent || (grid->stencil && decoded[0] >= .5))
            continue;
        double weight = taps[i].weight;
        coverage += weight;
        if (grid->stencil)
            continue;
        if (grid->colors->indexed) {
            unsigned index = (unsigned)floor(fmin(grid->colors->high, fmax(0, decoded[0])) + .5);
            for (int c = 0; c < grid->colors->components; c++)
                decoded[c] = cupidimage_pdf_color_palette_value(
                    grid->colors, c, grid->colors->palette[index * grid->colors->components + c]);
        }
        double alpha = grid->mask->has_matte
                           ? pdf_image_mask_alpha(grid->mask, (taps[i].x + .5) / grid->width,
                                                  1 - (taps[i].y + .5) / grid->height)
                           : 1;
        /* Matte recovery must not let an undefined fully transparent color
         * contaminate neighboring samples. Filter associated recovered colors. */
        if (grid->mask->has_matte)
            weight *= alpha;
        if (grid->premultiplied)
            weight *= embedded_alpha;
        color_weight += weight;
        for (int c = 0; c < grid->colors->components; c++) {
            double value = cupidimage_pdf_color_component(grid->colors, c, decoded[c]);
            if (grid->mask->has_matte)
                value = alpha > 0 ? grid->mask->matte[c] + (value - grid->mask->matte[c]) / alpha
                                  : grid->mask->matte[c];
            values[c] += weight * cupidimage_pdf_color_component(grid->colors, c, value);
        }
    }
    if (color_weight > 0 && !grid->stencil)
        for (int c = 0; c < grid->colors->components; c++)
            values[c] /= color_weight;
    return coverage;
}

int cupidimage_pdf_draw_image_view(pdf_render_state *st, const pdf_object_view *view) {
    st->paint_serial = ++st->form_budget->paint_serial;
    pdf_object_view image = *view;
    image.owned = NULL; /* The caller retains the dictionary and stream. */
    unsigned char *samples = NULL;
    pdf_jpx_source jpx = {0};
    pdf_image_mask mask = {0};
    pdf_object_view mask_view = {0};
    pdf_soft_mask *graphics_mask = st->gs.soft_mask;
    int graphics_intent = st->gs.rendering_intent;
    pdf_image_colors colors = {0};
    int width, height, bits, components, ok = 0, in_data = 0;
    double sample_zero[PDF_COLOR_COMPONENTS] = {0};
    size_t pos;
    if (!cupidimage_pdf_entry_is_name(st->document, st->document_size, &image, "Subtype",
                                      "Image") ||
        !pdf_image_type(st, &image))
        goto done;
    int stencil = 0, interpolate = 0;
    if (!cupidimage_pdf_read_intent(st, &image, "Intent", &st->gs.rendering_intent) ||
        !pdf_image_boolean(st, &image, "ImageMask", &stencil) ||
        !pdf_image_boolean(st, &image, "Interpolate", &interpolate))
        goto done;
    if ((st->glyph_uncolored || st->pattern_uncolored) && !stencil)
        goto done;
    if (!cupidimage_pdf_jpx_source_load(st, &image, &jpx))
        goto done;
    int explicit_colors = pdf_image_present(st, &image, "ColorSpace");
    if (explicit_colors < 0)
        goto done;
    if (!stencil) {
        if (explicit_colors) {
            if (!cupidimage_pdf_page_value(&image, "ColorSpace", &pos) ||
                !cupidimage_pdf_image_color_space(st, &image, &pos, 0, &colors))
                goto done;
        } else if (!jpx.present || !cupidimage_pdf_jpx_color_space(st, &jpx, &colors))
            goto done;
    }
    if (colors.pattern)
        goto done;
    components = stencil || colors.indexed ? 1 : colors.components;
    bits = stencil ? 1 : 8;
    int has_bits = jpx.present ? 0 : pdf_image_present(st, &image, "BitsPerComponent");
    if (has_bits < 0 || !components || !cupidimage_pdf_image_integer(st, &image, "Width", &width) ||
        !cupidimage_pdf_image_integer(st, &image, "Height", &height) ||
        (!jpx.present && (!stencil || has_bits) &&
         !cupidimage_pdf_image_integer(st, &image, "BitsPerComponent", &bits)) ||
        (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 16) ||
        (stencil && (bits != 1 || pdf_image_present(st, &image, "ColorSpace") != 0)) || width < 1 ||
        height < 1 || (double)width * height * components > 64.0 * 1024 * 1024)
        goto done;
    if (jpx.present) {
        uint64_t value;
        unsigned precision;
        int is_signed;
        if (jpx.file.ordinary != (unsigned)components ||
            !cupidimage_pdf_jpx_channel_sample(&jpx.file, jpx.file.color_order[0], 0, 0, &value,
                                               &precision, &is_signed) ||
            (stencil && precision != 1))
            goto done;
        bits = (int)precision;
        int has_in_data = pdf_image_present(st, &image, "SMaskInData");
        if (has_in_data < 0 ||
            (has_in_data && !cupidimage_pdf_image_integer(st, &image, "SMaskInData", &in_data)) ||
            in_data < 0 || in_data > 2)
            goto done;
        for (int c = 0; c < components; c++) {
            if (!cupidimage_pdf_jpx_channel_sample(&jpx.file, jpx.file.color_order[c], 0, 0, &value,
                                                   &precision, &is_signed))
                goto done;
            if (is_signed)
                sample_zero[c] =
                    (double)(UINT64_C(1) << (precision - 1)) / ((UINT64_C(1) << precision) - 1);
        }
        if (in_data) {
            if (stencil || jpx.file.opacity_channels != 1 || jpx.file.alpha[0] == 65535 ||
                pdf_image_present(st, &image, "SMask") != 0)
                goto done;
            for (int c = 1; c < components; c++)
                if (jpx.file.alpha[c] != jpx.file.alpha[0])
                    goto done;
            mask.embedded = &jpx;
            mask.embedded_channel = jpx.file.alpha[0];
            mask.width = width;
            mask.height = height;
            mask.soft = 1;
            mask.interpolate = interpolate;
            mask.decode[1] = 1;
        }
    }
    double decode[2 * PDF_COLOR_COMPONENTS] = {0, 1};
    if (colors.indexed)
        decode[1] = (double)((UINT64_C(1) << bits) - 1);
    else if (!stencil)
        for (int component = 0; component < components; component++)
            cupidimage_pdf_color_range(&colors, component, &decode[2 * component],
                                       &decode[2 * component + 1]);
    if (jpx.present && !explicit_colors &&
        (colors.icc || colors.encoded_transfer == 1 || colors.encoded_transfer == 2))
        for (int c = 0; c < components; c++) {
            decode[2 * c] -= sample_zero[c];
            decode[2 * c + 1] -= sample_zero[c];
        }
    if ((!jpx.present || explicit_colors || stencil) &&
        pdf_image_array(st, &image, "Decode", decode, 2 * components) < 0)
        goto done;
    if (stencil && !((decode[0] == 0 && decode[1] == 1) || (decode[0] == 1 && decode[1] == 0)))
        goto done;
    uint64_t color_key[2 * PDF_COLOR_COMPONENTS] = {0};
    int has_color_key = 0;
    int soft = 1;
    int has_mask =
        cupidimage_pdf_optional_value(st->document, st->document_size, &image, "SMask", &mask_view);
    if (!has_mask && !in_data) {
        soft = 0;
        has_mask = cupidimage_pdf_optional_value(st->document, st->document_size, &image, "Mask",
                                                 &mask_view);
    }
    if (has_mask < 0 || (has_mask && stencil))
        goto done;
    if (has_mask) {
        pos = 0;
        cupidimage_pdf_skip_ws_and_comments(mask_view.data, mask_view.len, &pos);
        has_color_key = !soft && pos < mask_view.len && mask_view.data[pos] == '[';
        if (has_color_key) {
            double keys[2 * PDF_COLOR_COMPONENTS];
            if (!pdf_image_numbers(st, &mask_view, keys, 2 * components))
                goto done;
            for (int i = 0; i < components * 2; i++) {
                unsigned precision = (unsigned)bits;
                if (jpx.present) {
                    uint64_t value;
                    int is_signed;
                    if (!cupidimage_pdf_jpx_channel_sample(&jpx.file, jpx.file.color_order[i / 2],
                                                           0, 0, &value, &precision, &is_signed))
                        goto done;
                }
                uint64_t maximum = (UINT64_C(1) << precision) - 1;
                if (keys[i] < 0 || keys[i] > maximum || floor(keys[i]) != keys[i])
                    goto done;
                color_key[i] = (uint64_t)keys[i];
                if ((i & 1) && color_key[i] < color_key[i - 1])
                    goto done;
            }
        } else if (!pdf_load_image_mask(st, &mask_view, soft, width, height, colors.components,
                                        &colors, &mask)) {
            goto done;
        }
    }
    if (!jpx.present && !pdf_image_samples(st, &image, width, height, components, bits, &samples))
        goto done;
    size_t sample_row = ((size_t)width * components * bits + 7u) / 8u;
    const double *m = st->gs.ctm;
    double det = (double)m[0] * m[3] - (double)m[1] * m[2];
    if (!isfinite(det))
        goto done;
    if (det == 0) {
        ok = 1;
        goto done;
    }
    double min_x = st->img->width, max_x = 0, min_y = st->img->height, max_y = 0;
    for (int corner = 0; corner < 4; corner++) {
        double x, y;
        cupidimage_pdf_user_to_pixel(st, (corner & 1), (corner >> 1), &x, &y);
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
    pdf_image_grid grid = {samples,    decode,        color_key, &colors,     &mask,
                           sample_row, width,         height,    components,  bits,
                           stencil,    has_color_key, &jpx,      sample_zero, in_data == 2};
    if (interpolate || mask.interpolate) {
        size_t pixels = (size_t)(x1 - x0) * (y1 - y0);
        size_t per_pixel = (size_t)(components + colors.components + 1) * 4 +
                           ((mask.samples || mask.jpx.present || mask.embedded) ? 16 : 0);
        if (pixels > st->clip_budget->tests_left / per_pixel)
            goto done;
        st->clip_budget->tests_left -= pixels * per_pixel;
    }
    /* An image's explicit, color-key or soft mask replaces the graphics-state mask. */
    if (mask.samples || mask.jpx.present || mask.embedded || has_color_key)
        st->gs.soft_mask = NULL;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            double tx = (x + 0.5) * (72.0 / 96.0) + st->page_x0_pts - m[4];
            double ty = st->page_top_pts - (y + 0.5) * (72.0 / 96.0) - m[5];
            double u = (m[3] * tx - m[2] * ty) / det;
            double v = (m[0] * ty - m[1] * tx) / det;
            if (!isfinite(u) || !isfinite(v))
                goto done;
            if (u < 0 || u >= 1 || v <= 0 || v > 1)
                continue;
            double values[PDF_COLOR_COMPONENTS] = {0};
            double alpha = pdf_image_mask_alpha(&mask, u, v), coverage = 1;
            if (alpha <= 0 && (!mask.soft || st->gs.alpha_is_shape))
                continue;
            if (interpolate) {
                coverage = pdf_image_filtered_values(&grid, u, v, values);
                if (coverage <= 0)
                    continue;
            } else {
                int sx = (int)(u * width), sy = (int)((1 - v) * height);
                if (sx >= width || sy >= height)
                    continue;
                int transparent = has_color_key;
                for (int c = 0; c < components; c++) {
                    uint64_t maximum;
                    uint64_t value = pdf_image_grid_sample(&grid, sx, sy, c, &maximum);
                    if (has_color_key && (value < color_key[c * 2] || value > color_key[c * 2 + 1]))
                        transparent = 0;
                    double unit = (double)value / maximum;
                    if (in_data == 2)
                        unit = alpha > 0 ? sample_zero[c] + (unit - sample_zero[c]) / alpha
                                         : sample_zero[c];
                    values[c] = decode[2 * c] + unit * (decode[2 * c + 1] - decode[2 * c]);
                }
                if (transparent || (stencil && values[0] >= 0.5))
                    continue;
                if (colors.indexed) {
                    unsigned index = (unsigned)floor(fmin(colors.high, fmax(0, values[0])) + 0.5);
                    for (int c = 0; c < colors.components; c++)
                        values[c] = cupidimage_pdf_color_palette_value(
                            &colors, c, colors.palette[index * colors.components + c]);
                }
                if (mask.has_matte && alpha > 0) {
                    for (int c = 0; c < colors.components; c++)
                        values[c] = mask.matte[c] + (values[c] - mask.matte[c]) / alpha;
                }
            }
            double shape =
                coverage *
                ((mask.samples || mask.jpx.present || mask.embedded) && !mask.soft ? alpha : 1);
            alpha *= coverage;
            if (stencil)
                cupidimage_pdf_paint_pixel(st, x, y, 0, alpha);
            else
                cupidimage_pdf_composite_space(st, x, y, values, &colors, alpha * st->gs.fill_alpha,
                                               st->gs.alpha_is_shape ? alpha * st->gs.fill_alpha
                                                                     : shape);
        }
    }
    ok = !st->image_failed;
done:
    cupidimage_pdf_patterns_end_paint(st);
    st->gs.soft_mask = graphics_mask;
    st->gs.rendering_intent = graphics_intent;
    cupidimage_pdf_jpx_source_free(&mask.jpx);
    cupidimage_pdf_jpx_source_free(&jpx);
    free(mask.samples);
    cupidimage_pdf_object_view_release(&mask_view);
    free(samples);
    cupidimage_pdf_object_view_release(&image);
    if (!ok && st->error && st->error_capacity && !st->error[0])
        cupidimage_set_err(st->error, st->error_capacity,
                           "pdf: invalid or unsupported image XObject");
    return ok;
}

int cupidimage_pdf_draw_image(pdf_render_state *st, const char *name) {
    pdf_object_view image = {0};
    pdf_object_id object;
    int ok = 0;
    if (st->optional_hidden)
        return 1;
    if (pdf_image_object(st, name, &object) && cupidimage_pdf_state_object(st, object, &image)) {
        int visible = 1;
        if (!cupidimage_pdf_optional_object_visibility(st, &image, &visible))
            ok = 0;
        else if (!visible)
            ok = 1;
        else if (cupidimage_pdf_entry_is_name(st->document, st->document_size, &image, "Subtype",
                                              "Form"))
            ok = cupidimage_pdf_draw_form(st, object, &image);
        else
            ok = cupidimage_pdf_draw_image_view(st, &image);
    }
    cupidimage_pdf_object_view_release(&image);
    if (!ok && st->error && st->error_capacity && !st->error[0])
        cupidimage_set_err(st->error, st->error_capacity, "pdf: invalid image XObject");
    return ok;
}
