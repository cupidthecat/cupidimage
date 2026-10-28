#include "pdf_content_syntax.h"
#include "pdf_functions.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TINT_STORE_LIMIT (64u * 1024u * 1024u)
#define TINT_KEY_LIMIT (1024u * 1024u)

struct pdf_tint {
    struct pdf_tint *next;
    pdf_image_colors alternate;
    pdf_function_tree function;
    unsigned char *key;
    size_t key_length;
    int inputs;
    int mode; /* 0: alternate transform; 1: All; 2: None. */
};

static int tint_name(const pdf_render_state *st, const pdf_object_view *view, size_t *pos,
                     char name[128]) {
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    size_t end = *pos;
    pdf_object_id object;
    if (!cupidimage_pdf_parse_reference(view, &end, &object))
        return cupidimage_pdf_read_name(view, pos, name);
    pdf_object_view value = {0};
    size_t at = 0;
    int ok = cupidimage_pdf_resolve_value(st->document, st->document_size, view, *pos, &value, 0) &&
             cupidimage_pdf_read_name(&value, &at, name);
    cupidimage_pdf_object_view_release(&value);
    if (ok)
        *pos = end;
    return ok;
}

/* All and None ignore their alternate transform. Check operand kinds without
 * trying to render the unused profile or execute the unused function. */
static int tint_ignored_value(const pdf_render_state *st, const pdf_object_view *view, size_t *pos,
                              int function) {
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    size_t end = *pos;
    pdf_object_id object;
    int indirect = cupidimage_pdf_parse_reference(view, &end, &object);
    pdf_object_view value = {0}, dictionary = {0};
    int ok = cupidimage_pdf_resolve_value(st->document, st->document_size, view, *pos, &value, 0);
    size_t at = 0;
    cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &at);
    if (ok && function) {
        int type;
        ok = cupidimage_pdf_dictionary_view(&value, at, &dictionary, &at) &&
             cupidimage_pdf_image_integer(st, &dictionary, "FunctionType", &type) &&
             (type == 0 || type == 2 || type == 3 || type == 4);
    } else if (ok) {
        char name[128];
        if (at < value.len && value.data[at] == '[') {
            size_t family = at + 1;
            ok = cupidimage_pdf_read_name(&value, &family, name) &&
                 cupidimage_pdf_content_array(value.data, value.len, &at, NULL, NULL);
        } else
            ok = cupidimage_pdf_read_name(&value, &at, name);
        ok = ok && (!strcmp(name, "DeviceGray") || !strcmp(name, "DeviceRGB") ||
                    !strcmp(name, "DeviceCMYK") || !strcmp(name, "CalGray") ||
                    !strcmp(name, "CalRGB") || !strcmp(name, "Lab") || !strcmp(name, "ICCBased"));
    }
    if (ok)
        *pos = indirect ? end : *pos + at;
    cupidimage_pdf_object_view_release(&value);
    return ok;
}

static int tint_same_alternate(const pdf_image_colors *a, const pdf_image_colors *b) {
    return a->components == b->components && a->icc == b->icc && a->calibrated == b->calibrated &&
           !memcmp(a->white, b->white, sizeof(a->white)) &&
           !memcmp(a->gamma, b->gamma, sizeof(a->gamma)) &&
           !memcmp(a->matrix, b->matrix, sizeof(a->matrix)) &&
           !memcmp(a->adaptation, b->adaptation, sizeof(a->adaptation)) &&
           !memcmp(a->lab_range, b->lab_range, sizeof(a->lab_range));
}

static int tint_names(const pdf_render_state *st, const pdf_object_view *view, size_t *pos,
                      int *components, int *all_none) {
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    size_t end = *pos;
    pdf_object_id object;
    int indirect = cupidimage_pdf_parse_reference(view, &end, &object);
    pdf_object_view value = {0};
    if (!cupidimage_pdf_resolve_value(st->document, st->document_size, view, *pos, &value, 0))
        return 0;
    size_t at = 0;
    int count = 0, none = 1, ok = 0;
    char names[PDF_COLOR_COMPONENTS][128];
    cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &at);
    if (at == value.len || value.data[at++] != '[')
        goto done;
    for (;;) {
        cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &at);
        if (at < value.len && value.data[at] == ']') {
            at++;
            break;
        }
        if (count == PDF_COLOR_COMPONENTS || !tint_name(st, &value, &at, names[count]) ||
            !strcmp(names[count], "All"))
            goto done;
        if (strcmp(names[count], "None")) {
            none = 0;
            for (int i = 0; i < count; i++)
                if (!strcmp(names[i], names[count]))
                    goto done;
        }
        count++;
    }
    if (!count)
        goto done;
    *components = count;
    *all_none = none;
    *pos = indirect ? end : *pos + at;
    ok = 1;
done:
    cupidimage_pdf_object_view_release(&value);
    return ok;
}

static int tint_attributes(const pdf_render_state *st, const pdf_object_view *view, size_t *pos) {
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    size_t end = *pos;
    pdf_object_id object;
    int indirect = cupidimage_pdf_parse_reference(view, &end, &object);
    pdf_object_view value = {0}, dictionary = {0}, subtype = {0};
    int ok = cupidimage_pdf_resolve_value(st->document, st->document_size, view, *pos, &value, 0);
    size_t at = 0;
    cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &at);
    if (ok && value.len - at >= 4 && !memcmp(value.data + at, "null", 4) &&
        (value.len - at == 4 || cupidimage_pdf_is_ws(value.data[at + 4]) ||
         cupidimage_pdf_is_delim(value.data[at + 4])))
        at += 4;
    else {
        ok = ok && cupidimage_pdf_dictionary_view(&value, at, &dictionary, &at);
        if (ok) {
            int status = cupidimage_pdf_optional_value(st->document, st->document_size, &dictionary,
                                                       "Subtype", &subtype);
            size_t name_pos = 0;
            char name[128];
            ok =
                status == 0 || (status > 0 && cupidimage_pdf_read_name(&subtype, &name_pos, name) &&
                                !strcmp(name, "DeviceN"));
        }
    }
    if (ok)
        *pos = indirect ? end : *pos + at;
    cupidimage_pdf_object_view_release(&subtype);
    cupidimage_pdf_object_view_release(&value);
    return ok;
}

int cupidimage_pdf_tint_space(const pdf_render_state *st, const pdf_object_view *view, size_t start,
                              size_t *pos, unsigned depth, pdf_image_colors *colors, int defaults,
                              int device_n) {
    size_t end = start;
    if (!st->tints || depth >= 16 ||
        !cupidimage_pdf_content_array(view->data, view->len, &end, NULL, NULL) ||
        end - start > TINT_KEY_LIMIT)
        return 0;
    int inputs = 1, mode = 0;
    if (device_n) {
        int all_none;
        if (!tint_names(st, view, pos, &inputs, &all_none))
            return 0;
        mode = all_none ? 2 : 0;
    } else {
        char name[128];
        if (!tint_name(st, view, pos, name))
            return 0;
        mode = !strcmp(name, "All") ? 1 : !strcmp(name, "None") ? 2 : 0;
    }
    pdf_image_colors alternate = {0};
    if (mode) {
        if (!tint_ignored_value(st, view, pos, 0))
            return 0;
    } else if (!cupidimage_pdf_color_space_value(st, view, pos, depth + 1, &alternate, defaults) ||
               alternate.indexed || alternate.pattern || alternate.tint ||
               alternate.components < 1 || alternate.components > 4)
        return 0;
    size_t function_pos = *pos;
    if (!tint_ignored_value(st, view, pos, 1))
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    if (device_n && *pos < end && view->data[*pos] != ']') {
        if (!tint_attributes(st, view, pos))
            return 0;
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    }
    if (*pos >= end || view->data[(*pos)++] != ']' || *pos != end)
        return 0;
    size_t length = end - start;
    for (pdf_tint *item = st->tints->head; item; item = item->next)
        if (item->key_length == length && !memcmp(item->key, view->data + start, length) &&
            tint_same_alternate(&item->alternate, &alternate)) {
            colors->components = inputs;
            colors->tint = item;
            return 1;
        }
    if (st->tints->count == 256)
        return 0;
    pdf_tint *item = calloc(1, sizeof(*item));
    if (!item)
        return 0;
    item->alternate = alternate;
    item->mode = mode;
    item->inputs = inputs;
    int ok = mode || cupidimage_pdf_load_function_value(st, view, &function_pos, inputs,
                                                        alternate.components, &item->function);
    size_t cost = sizeof(*item) + length + cupidimage_pdf_function_bytes(&item->function) -
                  sizeof(item->function);
    if (!ok || cost > TINT_STORE_LIMIT - st->tints->bytes)
        goto fail;
    item->key = malloc(length);
    if (!item->key)
        goto fail;
    memcpy(item->key, view->data + start, length);
    item->key_length = length;
    item->next = st->tints->head;
    st->tints->head = item;
    st->tints->count++;
    st->tints->bytes += cost;
    colors->components = inputs;
    colors->tint = item;
    return 1;
fail:
    cupidimage_pdf_free_function(&item->function);
    free(item->key);
    free(item);
    return 0;
}

int cupidimage_pdf_tint_none(const pdf_tint *space) {
    return space && space->mode == 2;
}

int cupidimage_pdf_empty_color(const pdf_image_colors *space, const pdf_pattern *pattern) {
    return (space->pattern && !pattern) || cupidimage_pdf_tint_none(space->tint);
}

void cupidimage_pdf_tint_paint(pdf_render_state *st, int x, int y, const double *values,
                               const pdf_tint *space, double alpha, double shape) {
    if (space->mode == 2)
        return;
    double input[PDF_COLOR_COMPONENTS] = {0}, result[4] = {0};
    for (int i = 0; i < space->inputs; i++)
        input[i] = fmax(0, fmin(1, values[i]));
    if (space->mode == 1) {
        result[0] = 1 - input[0];
        cupidimage_pdf_composite_device(st, x, y, result, 1, alpha, shape);
        return;
    }
    if (!cupidimage_pdf_evaluate_function(&space->function, input, result,
                                          &st->form_budget->shade_evaluations_left)) {
        st->image_failed = 1;
        cupidimage_set_err(st->error, st->error_capacity,
                           "pdf: tint transform failed or exceeded work limits");
        return;
    }
    cupidimage_pdf_composite_space(st, x, y, result, &space->alternate, alpha, shape);
}

void cupidimage_pdf_tint_free(pdf_tint_store *store) {
    while (store->head) {
        pdf_tint *item = store->head;
        store->head = item->next;
        cupidimage_pdf_free_function(&item->function);
        free(item->key);
        free(item);
    }
    store->bytes = store->count = 0;
}
