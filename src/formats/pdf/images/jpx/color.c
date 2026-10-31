#include "../../color/matrix.h"
#include "boxes.h"
#include "source.h"
#include <math.h>
#include <string.h>

static int jpx_device_space(const pdf_render_state *st, unsigned components,
                            pdf_image_colors *space) {
    const char *name = components == 1   ? "/DeviceGray"
                       : components == 3 ? "/DeviceRGB"
                       : components == 4 ? "/DeviceCMYK"
                                         : NULL;
    if (!name)
        return 0;
    pdf_object_view view = {(const unsigned char *)name, strlen(name), NULL, NULL};
    size_t pos = 0;
    return cupidimage_pdf_image_color_space(st, &view, &pos, 0, space);
}

static int jpx_enumerated_space(const pdf_render_state *st, unsigned enumeration,
                                unsigned components, pdf_image_colors *space) {
    if (enumeration == 12)
        return components != 4 ? 0 : jpx_device_space(st, 4, space) ? 1 : -1;
    if ((enumeration != 16 && enumeration != 17 && enumeration != 21) ||
        components != (enumeration == 17 ? 1u : 3u))
        return 0;
    memset(space, 0, sizeof(*space));
    space->components = (int)components;
    space->calibrated = components == 1 ? 1 : 2;
    space->encoded_transfer = enumeration == 21 ? 2 : 1;
    space->gamma[0] = space->gamma[1] = space->gamma[2] = 1;
    space->white[0] = .95047;
    space->white[1] = 1;
    space->white[2] = 1.08883;
    /* Use the inverse of the output matrix to avoid a second, differently
     * rounded sRGB matrix in the source-to-output round trip. */
    double to_rgb[9] = {3.2404542, -1.5371385, -.4985314, -.9692660, 1.8760108,
                        .0415560,  .0556434,   -.2040259, 1.0572252};
    if (enumeration == 21) {
        /* ICC ROMM RGB characterisation, normalized D50 XYZ to linear RGB. */
        const double romm[9] = {1.3460, -.2556, -.0511, -.5446, 1.5082, .0205, 0, 0, 1.2123};
        memcpy(to_rgb, romm, sizeof(romm));
        space->white[0] = .9642;
        space->white[2] = .8249;
    }
    double inverse[9];
    if (!cupidimage_pdf_color_matrix_inverse(to_rgb, inverse))
        return 0;
    for (unsigned r = 0; r < 3; r++)
        for (unsigned c = 0; c < 3; c++)
            space->matrix[c * 3 + r] = inverse[r * 3 + c];
    return cupidimage_pdf_calibrated_output(space);
}

static int jpx_sycc_space(const pdf_render_state *st, const pdf_jpx_source *source,
                          pdf_image_colors *space) {
    if (!jpx_enumerated_space(st, 16, source->file.ordinary, space))
        return 0;
    space->encoded_transfer = 4;
    /* ICC sYCC characterisation, IEC 61966-2-1 Amendment 1. Keep extended
     * gamut RGB through the symmetric transfer curve; clip only at output. */
    const double rgb_to_ycc[9] = {.2990, .5870, .1140,  -.1687, -.3312,
                                  .5000, .5000, -.4187, -.0813};
    if (!cupidimage_pdf_color_matrix_inverse(rgb_to_ycc, space->source_matrix))
        return 0;
    for (unsigned c = 0; c < 3; c++) {
        uint64_t value;
        unsigned precision;
        int is_signed;
        if (!cupidimage_pdf_jpx_channel_sample(&source->file, source->file.color_order[c], 0, 0,
                                               &value, &precision, &is_signed))
            return 0;
        double offset = c || is_signed ? (double)(UINT64_C(1) << (precision - 1)) /
                                             ((UINT64_C(1) << precision) - 1)
                                       : 0;
        space->source_range[2 * c] = -offset;
        space->source_range[2 * c + 1] = 1 - offset;
    }
    return 1;
}

static int jpx_lab_space(const pdf_jpx_source *source, const pdf_jpx_color *color,
                         pdf_image_colors *space) {
    if (source->file.ordinary != 3 || (color->parameters.length && color->parameters.length != 28))
        return 0;
    uint32_t illuminant = color->parameters.length ? cupidimage_jpx_u32(color->parameters.data + 24)
                                                   : UINT32_C(0x00443530);
    if (illuminant != UINT32_C(0x00443530) && illuminant != UINT32_C(0x00443635))
        return 0;
    memset(space, 0, sizeof(*space));
    space->components = 3;
    space->calibrated = 3;
    space->encoded_transfer = 3;
    space->white[0] = illuminant == UINT32_C(0x00443530) ? .9642 : .95047;
    space->white[1] = 1;
    space->white[2] = illuminant == UINT32_C(0x00443530) ? .8249 : 1.08883;
    const double ranges[3] = {100, 170, 200};
    for (unsigned c = 0; c < 3; c++) {
        uint64_t sample;
        unsigned precision;
        int is_signed;
        if (!cupidimage_pdf_jpx_channel_sample(&source->file, source->file.color_order[c], 0, 0,
                                               &sample, &precision, &is_signed) ||
            is_signed)
            return 0;
        double range = ranges[c];
        double offset = c == 0   ? 0
                        : c == 1 ? ldexp(1, (int)precision - 1)
                                 : ldexp(3, (int)precision - 3);
        if (color->parameters.length) {
            range = cupidimage_jpx_u32(color->parameters.data + c * 8);
            offset = cupidimage_jpx_u32(color->parameters.data + c * 8 + 4);
        }
        double minimum = -offset / ((UINT64_C(1) << precision) - 1) * range;
        space->source_range[2 * c] = minimum;
        space->source_range[2 * c + 1] = minimum + range;
        if (c) {
            space->lab_range[(c - 1) * 2] = minimum;
            space->lab_range[(c - 1) * 2 + 1] = minimum + range;
        }
    }
    return cupidimage_pdf_calibrated_output(space);
}

int cupidimage_pdf_jpx_color_space(const pdf_render_state *st, const pdf_jpx_source *source,
                                   pdf_image_colors *space) {
    const pdf_jpx_header *header = &source->file.header;
    unsigned used[PDF_JPX_COLORS] = {0};
    for (unsigned n = 0; n < header->color_count; n++) {
        unsigned best = PDF_JPX_COLORS;
        for (unsigned i = 0; i < header->color_count; i++) {
            if (used[i])
                continue;
            if (best == PDF_JPX_COLORS ||
                header->colors[i].precedence > header->colors[best].precedence ||
                (header->colors[i].precedence == header->colors[best].precedence &&
                 header->colors[i].approximation < header->colors[best].approximation))
                best = i;
        }
        used[best] = 1;
        const pdf_jpx_color *color = header->colors + best;
        int supported = 0;
        if (color->method == 1)
            supported =
                color->enumeration == 14 ? jpx_lab_space(source, color, space)
                : color->enumeration == 18
                    ? jpx_sycc_space(st, source, space)
                    : jpx_enumerated_space(st, color->enumeration, source->file.ordinary, space);
        else if (color->method == 2 || color->method == 3)
            supported = cupidimage_pdf_icc_bytes(
                st, color->parameters.data, color->parameters.length, source->file.ordinary, space);
        if (supported)
            return supported > 0;
    }
    /* ISO 32000-2, 7.4.9: use a device space when no supported specification remains. */
    memset(space, 0, sizeof(*space));
    return jpx_device_space(st, source->file.ordinary, space);
}
