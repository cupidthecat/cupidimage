#include "pdf_functions.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ICC_PROFILE_LIMIT (16u * 1024u * 1024u)
#define ICC_STORE_LIMIT (32u * 1024u * 1024u)

typedef struct {
    const unsigned char *samples;
    unsigned count, function;
    int parametric;
    double parameters[7];
} pdf_icc_curve;

struct pdf_icc_profile {
    struct pdf_icc_profile *next;
    pdf_object_id object;
    unsigned char *data;
    size_t length;
    int components, lab_pcs;
    pdf_icc_curve curves[3];
    pdf_image_colors output;
};

static unsigned icc_u16(const unsigned char *p) {
    return (unsigned)p[0] * 256 + p[1];
}

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

static int icc_curve_value(const pdf_icc_curve *curve, double input, double *output) {
    double x = fmax(0, fmin(1, input)), value;
    if (curve->parametric) {
        const double *p = curve->parameters;
        if (!curve->function)
            value = pow(x, p[0]);
        else if (curve->function <= 2)
            value = (x >= -p[2] / p[1] ? pow(p[1] * x + p[2], p[0]) : 0) +
                    (curve->function == 2 ? p[3] : 0);
        else if (x >= p[4])
            value = pow(p[1] * x + p[2], p[0]) + (curve->function == 4 ? p[5] : 0);
        else
            value = p[3] * x + (curve->function == 4 ? p[6] : 0);
    } else if (!curve->count)
        value = x;
    else if (curve->count == 1)
        value = pow(x, curve->parameters[0]);
    else {
        double at = x * (curve->count - 1);
        unsigned index = (unsigned)floor(at);
        value = icc_u16(curve->samples + index * 2) / 65535.0;
        if (index + 1 < curve->count)
            value += (at - index) * (icc_u16(curve->samples + (index + 1) * 2) / 65535.0 - value);
    }
    if (!isfinite(value))
        return 0;
    *output = fmax(0, fmin(1, value));
    return 1;
}

static int icc_curve(const unsigned char *data, size_t length, pdf_icc_curve *curve) {
    if (length < 12 || icc_u32(data + 4))
        return 0;
    if (!memcmp(data, "curv", 4)) {
        curve->count = icc_u32(data + 8);
        if (curve->count > 65536 || curve->count > (length - 12) / 2)
            return 0;
        curve->samples = data + 12;
        if (curve->count == 1) {
            curve->parameters[0] = icc_u16(data + 12) / 256.0;
            if (curve->parameters[0] <= 0)
                return 0;
        }
        return 1;
    }
    if (memcmp(data, "para", 4) || icc_u16(data + 10))
        return 0;
    static const unsigned parameters[] = {1, 3, 4, 5, 7};
    curve->parametric = 1;
    curve->function = icc_u16(data + 8);
    if (curve->function > 4 || length < 12 + parameters[curve->function] * 4)
        return 0;
    for (unsigned i = 0; i < parameters[curve->function]; i++)
        curve->parameters[i] = icc_fixed(data + 12 + i * 4);
    if (curve->parameters[0] <= 0 || (curve->function && curve->parameters[1] <= 0))
        return 0;
    double ignored;
    return icc_curve_value(curve, 0, &ignored) && icc_curve_value(curve, 1, &ignored) &&
           (curve->function < 3 || icc_curve_value(curve, curve->parameters[4], &ignored));
}

static int icc_parse(pdf_icc_profile *profile) {
    const unsigned char *p = profile->data;
    if (profile->length < 132 || icc_u32(p) != profile->length || memcmp(p + 36, "acsp", 4) ||
        (p[8] != 2 && p[8] != 4))
        return 0;
    int gray = !memcmp(p + 16, "GRAY", 4), rgb = !memcmp(p + 16, "RGB ", 4);
    if ((!gray && !rgb) || profile->components != (gray ? 1 : 3) ||
        (memcmp(p + 12, "scnr", 4) && memcmp(p + 12, "mntr", 4) &&
         (!gray || memcmp(p + 12, "prtr", 4))))
        return 0;
    profile->lab_pcs = !memcmp(p + 20, "Lab ", 4);
    if ((memcmp(p + 20, "XYZ ", 4) && !profile->lab_pcs) || (rgb && profile->lab_pcs))
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
        if (!memcmp(entry, "A2B", 3) || !memcmp(entry, "D2B", 3))
            return 0;
    }
    pdf_image_colors *out = &profile->output;
    out->components = rgb || profile->lab_pcs ? 3 : 1;
    out->calibrated = profile->lab_pcs ? 3 : rgb ? 2 : 1;
    out->white[0] = icc_fixed(p + 68);
    out->white[1] = 1;
    out->white[2] = icc_fixed(p + 76);
    out->gamma[0] = out->gamma[1] = out->gamma[2] = 1;
    out->lab_range[0] = out->lab_range[2] = -128;
    out->lab_range[1] = out->lab_range[3] = 127;
    static const char *columns[] = {"rXYZ", "gXYZ", "bXYZ"};
    static const char *curves[] = {"rTRC", "gTRC", "bTRC"};
    for (int i = 0; i < profile->components; i++) {
        const unsigned char *tag = NULL;
        size_t length = 0;
        if (icc_tag(profile, gray ? "kTRC" : curves[i], &tag, &length) != 1 ||
            !icc_curve(tag, length, profile->curves + i))
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
             (profile->components == 1 || profile->components == 3);
    double range[6] = {0, 1, 0, 1, 0, 1};
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
                             double output[3]) {
    double linear[4] = {0};
    for (int i = 0; i < profile->components; i++)
        if (!icc_curve_value(profile->curves + i, values[i], linear + i))
            return 0;
    if (profile->lab_pcs)
        linear[0] *= 100;
    cupidimage_pdf_calibrated_color(&profile->output, linear, gray, output);
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
