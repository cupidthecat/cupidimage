#include "../pdf_functions.h"
#include "internal.h"
#include <math.h>
#include <string.h>

static int annotation_known(const char *name) {
    static const char *const names[] = {
        "Text",      "Link",     "FreeText",   "Line",        "Square",         "Circle",
        "Polygon",   "PolyLine", "Stamp",      "Highlight",   "Underline",      "Squiggly",
        "StrikeOut", "Caret",    "Ink",        "Popup",       "FileAttachment", "Sound",
        "Movie",     "Widget",   "Screen",     "PrinterMark", "TrapNet",        "Watermark",
        "3D",        "Redact",   "Projection", "RichMedia"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(*names); i++)
        if (!strcmp(name, names[i]))
            return 1;
    return 0;
}

static int annotation_name(pdf_render_state *st, const pdf_object_view *dict, const char *key,
                           char name[128]) {
    pdf_object_view value = {0};
    name[0] = 0;
    int status = cupidimage_pdf_optional_value(st->document, st->document_size, dict, key, &value);
    size_t pos = 0;
    if (status > 0 && !cupidimage_pdf_read_name(&value, &pos, name))
        status = -1;
    cupidimage_pdf_object_view_release(&value);
    return status;
}

static int annotation_stream(const pdf_object_view *value) {
    size_t end;
    if (!cupidimage_pdf_annotation_dictionary(value, &end))
        return 0;
    cupidimage_pdf_skip_ws_and_comments(value->data, value->len, &end);
    return value->len - end >= 6 && !memcmp(value->data + end, "stream", 6) &&
           (value->len - end == 6 || cupidimage_pdf_is_ws(value->data[end + 6]));
}

static int annotation_rect(pdf_render_state *st, const pdf_object_view *value, const char *key,
                           double rect[4]) {
    if (!cupidimage_pdf_function_array(st, value, key, rect, 4))
        return 0;
    for (unsigned i = 0; i < 4; i++)
        if (!isfinite(rect[i]) || fabs(rect[i]) > 1e30)
            return 0;
    for (unsigned i = 0; i < 2; i++)
        if (rect[i] > rect[i + 2]) {
            double swap = rect[i];
            rect[i] = rect[i + 2];
            rect[i + 2] = swap;
        }
    return 1;
}

static int annotation_transform(pdf_render_state *st, const pdf_object_view *form,
                                const double rect[4], int rotation, int *empty) {
    double box[4], bounds[4] = {INFINITY, INFINITY, -INFINITY, -INFINITY};
    float m[6];
    *empty = 0;
    if (!annotation_rect(st, form, "BBox", box) || !cupidimage_pdf_form_matrix(st, form, m))
        return 0;
    for (unsigned i = 0; i < 4; i++) {
        double x = box[(i & 1) ? 2 : 0], y = box[(i & 2) ? 3 : 1];
        double tx = m[0] * x + m[2] * y + m[4], ty = m[1] * x + m[3] * y + m[5];
        bounds[0] = fmin(bounds[0], tx);
        bounds[1] = fmin(bounds[1], ty);
        bounds[2] = fmax(bounds[2], tx);
        bounds[3] = fmax(bounds[3], ty);
    }
    if (box[0] == box[2] || box[1] == box[3] || bounds[0] == bounds[2] || bounds[1] == bounds[3]) {
        *empty = 1;
        return 1;
    }
    if (rotation) {
        float c = rotation == 180 ? -1 : 0, s = rotation == 90 ? 1 : rotation == 270 ? -1 : 0;
        float pivot[6] = {c,
                          s,
                          -s,
                          c,
                          (float)(rect[0] * (1 - c) + rect[3] * s),
                          (float)(rect[3] * (1 - c) - rect[0] * s)};
        cupidimage_pdf_matrix_concat(st->gs.ctm, pivot);
    }
    double sx = (rect[2] - rect[0]) / (bounds[2] - bounds[0]);
    double sy = (rect[3] - rect[1]) / (bounds[3] - bounds[1]);
    double a[6] = {sx, 0, 0, sy, rect[0] - sx * bounds[0], rect[1] - sy * bounds[1]};
    float mapping[6];
    for (unsigned i = 0; i < 6; i++) {
        if (!isfinite(a[i]) || fabs(a[i]) > 1e30)
            return 0;
        mapping[i] = (float)a[i];
    }
    cupidimage_pdf_matrix_concat(st->gs.ctm, mapping);
    return 1;
}

int cupidimage_pdf_annotation_paint(pdf_render_state *st, const pdf_object_view *annotation,
                                    const pdf_page_geometry *geometry, size_t *work) {
    pdf_object_view ap = {0}, normal = {0}, selected = {0};
    pdf_object_id object = 0, ignored;
    size_t pos;
    int ok = 0, visible = 1, flags = 0, empty;
    char subtype[128], state[128];
    if (annotation_name(st, annotation, "Subtype", subtype) < 0)
        goto done;
    /* Popup windows have no appearance stream (ISO 32000-1 12.5.6.14). */
    if (!strcmp(subtype, "Popup")) {
        ok = 1;
        goto done;
    }
    pdf_object_view flag_value = {0};
    int status = cupidimage_pdf_optional_value(st->document, st->document_size, annotation, "F",
                                               &flag_value);
    cupidimage_pdf_object_view_release(&flag_value);
    if (status < 0 || (status && !cupidimage_pdf_image_integer(st, annotation, "F", &flags)))
        goto done;
    if ((flags & (2 | 32)) || ((flags & 1) && !annotation_known(subtype))) {
        ok = 1;
        goto done;
    }
    if (!cupidimage_pdf_optional_object_visibility(st, annotation, &visible))
        goto done;
    if (!visible || !cupidimage_pdf_page_value(annotation, "AP", &pos)) {
        ok = 1;
        goto done;
    }
    if (!cupidimage_pdf_annotation_item(st, annotation, &pos, &ap, &ignored, work))
        goto done;
    if (cupidimage_pdf_annotation_null(&ap)) {
        ok = 1;
        goto done;
    }
    if (annotation_stream(&ap))
        goto done;
    if (!cupidimage_pdf_page_value(&ap, "N", &pos)) {
        ok = 1;
        goto done;
    }
    if (!cupidimage_pdf_annotation_item(st, &ap, &pos, &normal, &object, work))
        goto done;
    const pdf_object_view *appearance = &normal;
    if (!cupidimage_pdf_annotation_null(&normal) && !annotation_stream(&normal)) {
        status = annotation_name(st, annotation, "AS", state);
        if (status < 0)
            goto done;
        if (!status || !cupidimage_pdf_page_value(&normal, state, &pos)) {
            ok = 1;
            goto done;
        }
        if (!cupidimage_pdf_annotation_item(st, &normal, &pos, &selected, &object, work))
            goto done;
        appearance = &selected;
    }
    if (cupidimage_pdf_annotation_null(appearance)) {
        ok = 1;
        goto done;
    }
    char form_subtype[128];
    status = annotation_name(st, appearance, "Subtype", form_subtype);
    if (!object || !annotation_stream(appearance) || status < 0 ||
        (status && strcmp(form_subtype, "Form")))
        goto done;
    if (!cupidimage_pdf_optional_object_visibility(st, appearance, &visible))
        goto done;
    if (!visible) {
        ok = 1;
        goto done;
    }
    double rect[4];
    if (!annotation_rect(st, annotation, "Rect", rect))
        goto done;
    if (rect[0] == rect[2] || rect[1] == rect[3]) {
        ok = 1;
        goto done;
    }
    st->gs = st->initial_gs;
    int rotation = (flags & 16) || !strcmp(subtype, "Text") ? geometry->rotation : 0;
    if (!annotation_transform(st, appearance, rect, rotation, &empty))
        goto done;
    ok = empty || cupidimage_pdf_draw_appearance(st, object, appearance);
done:
    cupidimage_pdf_object_view_release(&selected);
    cupidimage_pdf_object_view_release(&normal);
    cupidimage_pdf_object_view_release(&ap);
    return ok;
}
