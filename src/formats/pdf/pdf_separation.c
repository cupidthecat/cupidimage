#include "pdf_content_syntax.h"
#include "pdf_functions.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SEPARATION_STORE_LIMIT (64u * 1024u * 1024u)
#define SEPARATION_KEY_LIMIT (1024u * 1024u)

struct pdf_separation {
    struct pdf_separation *next;
    pdf_image_colors alternate;
    pdf_function_tree function;
    unsigned char *key;
    size_t key_length;
    int mode; /* 0: alternate transform; 1: All; 2: None. */
};

static int separation_name(const pdf_render_state *st, const pdf_object_view *view, size_t *pos,
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
static int separation_ignored_value(const pdf_render_state *st, const pdf_object_view *view,
                                    size_t *pos, int function) {
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

static int separation_same_alternate(const pdf_image_colors *a, const pdf_image_colors *b) {
    return a->components == b->components && a->icc == b->icc && a->calibrated == b->calibrated &&
           !memcmp(a->white, b->white, sizeof(a->white)) &&
           !memcmp(a->gamma, b->gamma, sizeof(a->gamma)) &&
           !memcmp(a->matrix, b->matrix, sizeof(a->matrix)) &&
           !memcmp(a->adaptation, b->adaptation, sizeof(a->adaptation)) &&
           !memcmp(a->lab_range, b->lab_range, sizeof(a->lab_range));
}

int cupidimage_pdf_separation_space(const pdf_render_state *st, const pdf_object_view *view,
                                    size_t start, size_t *pos, unsigned depth,
                                    pdf_image_colors *colors, int defaults) {
    size_t end = start;
    char name[128];
    if (!st->separations || depth >= 16 ||
        !cupidimage_pdf_content_array(view->data, view->len, &end, NULL, NULL) ||
        end - start > SEPARATION_KEY_LIMIT || !separation_name(st, view, pos, name))
        return 0;
    int mode = !strcmp(name, "All") ? 1 : !strcmp(name, "None") ? 2 : 0;
    pdf_image_colors alternate = {0};
    if (mode) {
        if (!separation_ignored_value(st, view, pos, 0))
            return 0;
    } else if (!cupidimage_pdf_color_space_value(st, view, pos, depth + 1, &alternate, defaults) ||
               alternate.indexed || alternate.pattern || alternate.separation ||
               alternate.components < 1 || alternate.components > 4)
        return 0;
    size_t function_pos = *pos;
    if (!separation_ignored_value(st, view, pos, 1))
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    if (*pos >= end || view->data[(*pos)++] != ']' || *pos != end)
        return 0;
    size_t length = end - start;
    for (pdf_separation *item = st->separations->head; item; item = item->next)
        if (item->key_length == length && !memcmp(item->key, view->data + start, length) &&
            separation_same_alternate(&item->alternate, &alternate)) {
            colors->components = 1;
            colors->separation = item;
            return 1;
        }
    if (st->separations->count == 256)
        return 0;
    pdf_separation *item = calloc(1, sizeof(*item));
    if (!item)
        return 0;
    item->alternate = alternate;
    item->mode = mode;
    int ok = mode || cupidimage_pdf_load_function_value(st, view, &function_pos, 1,
                                                        alternate.components, &item->function);
    size_t cost = sizeof(*item) + length + cupidimage_pdf_function_bytes(&item->function) -
                  sizeof(item->function);
    if (!ok || cost > SEPARATION_STORE_LIMIT - st->separations->bytes)
        goto fail;
    item->key = malloc(length);
    if (!item->key)
        goto fail;
    memcpy(item->key, view->data + start, length);
    item->key_length = length;
    item->next = st->separations->head;
    st->separations->head = item;
    st->separations->count++;
    st->separations->bytes += cost;
    colors->components = 1;
    colors->separation = item;
    return 1;
fail:
    cupidimage_pdf_free_function(&item->function);
    free(item->key);
    free(item);
    return 0;
}

int cupidimage_pdf_separation_none(const pdf_separation *space) {
    return space && space->mode == 2;
}

int cupidimage_pdf_empty_color(const pdf_image_colors *space, const pdf_pattern *pattern) {
    return (space->pattern && !pattern) || cupidimage_pdf_separation_none(space->separation);
}

void cupidimage_pdf_separation_paint(pdf_render_state *st, int x, int y, const double values[4],
                                     const pdf_separation *space, double alpha, double shape) {
    if (space->mode == 2)
        return;
    double input[2] = {fmax(0, fmin(1, values[0])), 0}, result[4] = {0};
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

void cupidimage_pdf_separation_free(pdf_separation_store *store) {
    while (store->head) {
        pdf_separation *item = store->head;
        store->head = item->next;
        cupidimage_pdf_free_function(&item->function);
        free(item->key);
        free(item);
    }
    store->bytes = store->count = 0;
}
