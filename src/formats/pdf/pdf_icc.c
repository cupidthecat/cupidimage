#include "pdf_functions.h"
#include "pdf_icc_curve.h"
#include "pdf_icc_lut.h"
#include "pdf_icc_mab.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ICC_PROFILE_LIMIT (16u * 1024u * 1024u)
#define ICC_STORE_LIMIT (32u * 1024u * 1024u)

typedef struct {
    pdf_icc_curve curves[3];
    pdf_icc_lut lut;
    pdf_icc_mab mab;
    pdf_image_colors output;
    int valid, reference_black;
} pdf_icc_transform;

struct pdf_icc_profile {
    struct pdf_icc_profile *next;
    pdf_object_id object;
    unsigned char *data;
    size_t length;
    int components, lab_pcs;
    pdf_icc_transform transforms[4];
};

static uint32_t icc_u32(const unsigned char *p) {
    return (uint32_t)p[0] * 16777216u + (uint32_t)p[1] * 65536u + (unsigned)p[2] * 256u + p[3];
}

static double icc_fixed(const unsigned char *p) {
    uint32_t value = icc_u32(p);
    return (value <= INT32_MAX ? (double)value : (double)value - 4294967296.0) / 65536.0;
}

static int icc_tag(const pdf_icc_profile *profile, const char name[4], const unsigned char **data,
                   size_t *length) {
    unsigned count = icc_u32(profile->data + 128);
    int found = 0;
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *entry = profile->data + 132 + i * 12;
        if (!memcmp(entry, name, 4)) {
            if (found)
                return -1;
            *data = profile->data + icc_u32(entry + 4);
            *length = icc_u32(entry + 8);
            found = 1;
        }
    }
    return found;
}

static int icc_model(pdf_icc_profile *, pdf_icc_transform *, const char[4]);

static int icc_parse(pdf_icc_profile *profile) {
    const unsigned char *p = profile->data;
    if (profile->length < 132 || icc_u32(p) != profile->length || memcmp(p + 36, "acsp", 4) ||
        (p[8] != 2 && p[8] != 4))
        return 0;
    int gray = !memcmp(p + 16, "GRAY", 4), rgb = !memcmp(p + 16, "RGB ", 4),
        cmyk = !memcmp(p + 16, "CMYK", 4);
    if ((!gray && !rgb && !cmyk) ||
        profile->components != (gray  ? 1
                                : rgb ? 3
                                      : 4) ||
        (memcmp(p + 12, "scnr", 4) && memcmp(p + 12, "mntr", 4) &&
         (memcmp(p + 12, "prtr", 4) && memcmp(p + 12, "spac", 4))))
        return 0;
    profile->lab_pcs = !memcmp(p + 20, "Lab ", 4);
    if (memcmp(p + 20, "XYZ ", 4) && !profile->lab_pcs)
        return 0;
    if (icc_u32(p + 68) != 0xf6d6 || icc_u32(p + 72) != 0x10000 || icc_u32(p + 76) != 0xd32d)
        return 0;
    unsigned count = icc_u32(p + 128);
    if (!count || count > 4096 || count > (profile->length - 132) / 12)
        return 0;
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *entry = p + 132 + i * 12;
        size_t offset = icc_u32(entry + 4), length = icc_u32(entry + 8);
        if ((offset & 3) || offset < 132u + count * 12u || offset > profile->length || length < 8 ||
            length > profile->length - offset)
            return 0;
        /* Do not silently substitute matrix tags for a higher-priority LUT transform. */
        if (!memcmp(entry, "D2B", 3))
            return 0;
    }
    static const char *tags[] = {"A2B1", "A2B0", "A2B2"};
    int valid = 0;
    for (unsigned i = 0; i < 3; i++) {
        profile->transforms[i].valid = icc_model(profile, &profile->transforms[i], tags[i]);
        valid |= profile->transforms[i].valid;
    }
    /* ICC v4 media white is already chromatically adapted to the PCS. Legacy
     * v2 absolute handling also needs its profile-class/adaptation conventions. */
    if (p[8] == 4 && profile->transforms[0].valid) {
        const unsigned char *white = NULL;
        size_t length = 0;
        if (icc_tag(profile, "wtpt", &white, &length) == 1 && length == 20 &&
            !memcmp(white, "XYZ ", 4) && !icc_u32(white + 4)) {
            pdf_icc_transform *absolute = &profile->transforms[3];
            *absolute = profile->transforms[0];
            for (unsigned column = 0; column < 3; column++) {
                double media = icc_fixed(white + 8 + column * 4);
                if (media <= 0)
                    absolute->valid = 0;
                double scale = media / absolute->output.white[column];
                for (unsigned row = 0; row < 3; row++)
                    absolute->output.adaptation[row * 3 + column] *= scale;
            }
        }
    }
    return valid;
}

static int icc_model(pdf_icc_profile *profile, pdf_icc_transform *transform,
                     const char tag_name[4]) {
    const unsigned char *p = profile->data;
    int gray = profile->components == 1, rgb = profile->components == 3,
        cmyk = profile->components == 4;
    pdf_image_colors *out = &transform->output;
    out->components = rgb || profile->lab_pcs ? 3 : 1;
    out->calibrated = profile->lab_pcs ? 3 : rgb ? 2 : 1;
    out->white[0] = icc_fixed(p + 68);
    out->white[1] = 1;
    out->white[2] = icc_fixed(p + 76);
    out->gamma[0] = out->gamma[1] = out->gamma[2] = 1;
    out->lab_range[0] = out->lab_range[2] = -128;
    out->lab_range[1] = out->lab_range[3] = 127;
    const unsigned char *table = NULL;
    size_t table_length = 0;
    int selected = icc_tag(profile, tag_name, &table, &table_length);
    if (!selected)
        selected = icc_tag(profile, "A2B0", &table, &table_length);
    if (selected < 0)
        return 0;
    if (selected) {
        if (!memcmp(table, "mAB ", 4)) {
            if (p[8] != 4 || !cupidimage_pdf_icc_mab_parse(&transform->mab, table, table_length,
                                                           (unsigned)profile->components))
                return 0;
        } else if (!cupidimage_pdf_icc_lut_parse(&transform->lut, table, table_length,
                                                 (unsigned)profile->components, profile->lab_pcs))
            return 0;
        out->components = 3;
        out->calibrated = profile->lab_pcs ? 3 : 2;
        transform->reference_black = p[8] == 4 && !memcmp(tag_name, "A2B0", 4);
        if (p[8] == 4 && !memcmp(tag_name, "A2B2", 4)) {
            const unsigned char *gamut = NULL;
            size_t gamut_length = 0;
            int present = icc_tag(profile, "rig2", &gamut, &gamut_length);
            if (present < 0 ||
                (present && (gamut_length != 12 || memcmp(gamut, "sig ", 4) || icc_u32(gamut + 4))))
                return 0;
            transform->reference_black = present && !memcmp(gamut + 8, "prmg", 4);
        }
        if (profile->lab_pcs && transform->lut.bytes == 2)
            out->lab_range[1] = out->lab_range[3] = 128;
        return cupidimage_pdf_calibrated_output(out);
    }
    if (cmyk || (rgb && (profile->lab_pcs || !memcmp(p + 12, "prtr", 4))) ||
        !memcmp(p + 12, "spac", 4))
        return 0;
    static const char *columns[] = {"rXYZ", "gXYZ", "bXYZ"};
    static const char *curves[] = {"rTRC", "gTRC", "bTRC"};
    for (int i = 0; i < profile->components; i++) {
        const unsigned char *tag = NULL;
        size_t length = 0;
        if (icc_tag(profile, gray ? "kTRC" : curves[i], &tag, &length) != 1 ||
            !cupidimage_pdf_icc_curve_parse(tag, length, transform->curves + i, NULL))
            return 0;
        if (rgb) {
            if (icc_tag(profile, columns[i], &tag, &length) != 1 || length != 20 ||
                memcmp(tag, "XYZ ", 4) || icc_u32(tag + 4))
                return 0;
            for (unsigned j = 0; j < 3; j++)
                out->matrix[i * 3 + j] = icc_fixed(tag + 8 + j * 4);
        }
    }
    return cupidimage_pdf_calibrated_output(out);
}

int cupidimage_pdf_icc_space(const pdf_render_state *st, pdf_object_id object,
                             pdf_image_colors *space) {
    if (!st->icc_store)
        return 0;
    for (pdf_icc_profile *p = st->icc_store->head; p; p = p->next)
        if (p->object == object) {
            space->calibrated = 0;
            space->icc = p;
            space->components = p->components;
            return 1;
        }
    if (st->icc_store->count >= 256 || st->icc_store->bytes > ICC_STORE_LIMIT)
        return 0;
    pdf_object_view source = {0}, view = {0};
    pdf_icc_profile *profile = calloc(1, sizeof(*profile));
    int ok = profile && cupidimage_pdf_state_object(st, object, &source) &&
             cupidimage_pdf_resolve_value(st->document, st->document_size, &source, 0, &view, 0) &&
             cupidimage_pdf_image_integer(st, &view, "N", &profile->components) &&
             (profile->components == 1 || profile->components == 3 || profile->components == 4);
    double range[8] = {0, 1, 0, 1, 0, 1, 0, 1};
    pdf_object_view range_view = {0};
    int present = ok ? cupidimage_pdf_optional_value(st->document, st->document_size, &view,
                                                     "Range", &range_view)
                     : -1;
    cupidimage_pdf_object_view_release(&range_view);
    ok = ok &&
         (present == 0 || (present == 1 && cupidimage_pdf_function_array(st, &view, "Range", range,
                                                                         profile->components * 2)));
    for (int i = 0; ok && i < profile->components; i++)
        ok = range[i * 2] == 0 && range[i * 2 + 1] == 1;
    const unsigned char *raw = NULL;
    size_t length = 0;
    ok = ok &&
         cupidimage_pdf_object_stream(st->document, st->document_size, &view, &raw, &length) &&
         cupidimage_pdf_decode_stream(&view, raw, length, &profile->data, &profile->length) &&
         profile->length <= ICC_PROFILE_LIMIT && icc_parse(profile);
    cupidimage_pdf_object_view_release(&view);
    cupidimage_pdf_object_view_release(&source);
    size_t cost = profile ? sizeof(*profile) + profile->length : 0;
    if (!ok || cost > ICC_STORE_LIMIT - st->icc_store->bytes) {
        if (profile)
            free(profile->data);
        free(profile);
        return 0;
    }
    profile->object = object;
    profile->next = st->icc_store->head;
    st->icc_store->head = profile;
    st->icc_store->bytes += cost;
    st->icc_store->count++;
    space->calibrated = 0;
    space->icc = profile;
    space->components = profile->components;
    return 1;
}

int cupidimage_pdf_icc_color(const pdf_icc_profile *profile, const double values[4], int gray,
                             int intent, double output[3]) {
    if (intent < 0 || intent >= 4 || !profile->transforms[intent].valid)
        return 0;
    const pdf_icc_transform *transform = &profile->transforms[intent];
    double xyz[3];
    if (transform->lut.inputs || transform->mab.inputs) {
        double pcs[4] = {0};
        int ok = transform->mab.inputs ? cupidimage_pdf_icc_mab_value(&transform->mab, values, pcs)
                                       : cupidimage_pdf_icc_lut_value(&transform->lut, values, pcs);
        if (!ok)
            return 0;
        if (profile->lab_pcs) {
            double factor = transform->lut.bytes == 2 ? 65535.0 / 65280.0 : 1;
            pcs[0] *= 100 * factor;
            pcs[1] = pcs[1] * 255 * factor - 128;
            pcs[2] = pcs[2] * 255 * factor - 128;
            cupidimage_pdf_calibrated_xyz(&transform->output, pcs, xyz);
        } else {
            for (unsigned i = 0; i < 3; i++)
                xyz[i] = pcs[i] * 65535.0 / 32768.0;
        }
    } else {
        double linear[4] = {0};
        for (int i = 0; i < profile->components; i++)
            if (!cupidimage_pdf_icc_curve_value(transform->curves + i, values[i], linear + i))
                return 0;
        if (profile->lab_pcs)
            linear[0] *= 100;
        cupidimage_pdf_calibrated_xyz(&transform->output, linear, xyz);
    }
    if (transform->reference_black) {
        /* Inverse of ICC.1:2022 6.3.4.3's reference-medium encoding, for the
         * fixed output target whose black point is zero. */
        static const double black[3] = {.003357, .003479, .002869};
        for (unsigned i = 0; i < 3; i++)
            xyz[i] = (xyz[i] - black[i]) / (1 - black[i] / transform->output.white[i]);
    }
    cupidimage_pdf_xyz_color(&transform->output, xyz, gray, output);
    return 1;
}

void cupidimage_pdf_icc_free(pdf_icc_store *store) {
    while (store->head) {
        pdf_icc_profile *profile = store->head;
        store->head = profile->next;
        free(profile->data);
        free(profile);
    }
    store->count = 0;
    store->bytes = 0;
}
