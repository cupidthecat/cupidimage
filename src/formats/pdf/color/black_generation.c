#include "../pdf_functions.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define COLOR_FUNCTION_LIMIT (16u * 1024u * 1024u)

struct pdf_color_function {
    struct pdf_color_function *next;
    pdf_function_tree tree;
    unsigned char *key;
    size_t length;
    uint64_t hash;
};

static int color_function_select(pdf_render_state *st, const pdf_object_view *view,
                                 const char *legacy, const char *modern,
                                 const pdf_color_function **out) {
    pdf_object_view value = {0};
    int newer = 1;
    int present =
        cupidimage_pdf_optional_value(st->document, st->document_size, view, modern, &value);
    if (!present) {
        newer = 0;
        present =
            cupidimage_pdf_optional_value(st->document, st->document_size, view, legacy, &value);
    }
    if (present <= 0) {
        cupidimage_pdf_object_view_release(&value);
        return present == 0;
    }
    int ok = 0;
    size_t pos = 0;
    char name[128];
    if (cupidimage_pdf_read_name(&value, &pos, name)) {
        if (newer && !strcmp(name, "Default")) {
            *out = NULL;
            ok = 1;
        }
        goto done;
    }
    pdf_color_function_store *store = st->color_functions;
    if (!store || !st->clip_budget || value.len > COLOR_FUNCTION_LIMIT ||
        value.len > st->clip_budget->tests_left)
        goto done;
    st->clip_budget->tests_left -= value.len;
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < value.len; i++) {
        hash ^= value.data[i];
        hash *= UINT64_C(1099511628211);
    }
    for (pdf_color_function *entry = store->head; entry; entry = entry->next)
        if (entry->hash == hash && entry->length == value.len) {
            if (value.len > st->clip_budget->tests_left)
                goto done;
            st->clip_budget->tests_left -= value.len;
            if (!memcmp(entry->key, value.data, value.len)) {
                *out = entry;
                ok = 1;
                goto done;
            }
        }
    if (store->count == 256 || store->bytes > COLOR_FUNCTION_LIMIT ||
        sizeof(pdf_color_function) > COLOR_FUNCTION_LIMIT - store->bytes)
        goto done;
    pdf_color_function *entry = calloc(1, sizeof(*entry));
    if (!entry)
        goto done;
    pos = 0;
    ok = cupidimage_pdf_load_function_value(st, &value, &pos, 1, 1, &entry->tree);
    size_t function_bytes = cupidimage_pdf_function_bytes(&entry->tree);
    size_t cost = sizeof(*entry) - sizeof(entry->tree) + value.len;
    if (function_bytes > COLOR_FUNCTION_LIMIT || cost > COLOR_FUNCTION_LIMIT - function_bytes)
        ok = 0;
    else
        cost += function_bytes;
    if (!ok || cost > COLOR_FUNCTION_LIMIT - store->bytes)
        goto failed;
    entry->key = malloc(value.len ? value.len : 1);
    if (!entry->key)
        goto failed;
    memcpy(entry->key, value.data, value.len);
    entry->length = value.len;
    entry->hash = hash;
    entry->next = store->head;
    store->head = entry;
    store->bytes += cost;
    store->count++;
    *out = entry;
    ok = 1;
    goto done;
failed:
    cupidimage_pdf_free_function(&entry->tree);
    free(entry->key);
    free(entry);
    ok = 0;
done:
    cupidimage_pdf_object_view_release(&value);
    return ok;
}

int cupidimage_pdf_color_function_state(pdf_render_state *st, const pdf_object_view *view,
                                        pdf_graphics_state *next) {
    return color_function_select(st, view, "BG", "BG2", &next->black_generation) &&
           color_function_select(st, view, "UCR", "UCR2", &next->undercolor_removal);
}

int cupidimage_pdf_rgb_to_cmyk(const pdf_render_state *st, const double rgb[3], int use_functions,
                               double output[4]) {
    double cmy[3], k = 1;
    for (unsigned i = 0; i < 3; i++) {
        if (!isfinite(rgb[i]))
            return 0;
        cmy[i] = 1 - fmax(0, fmin(1, rgb[i]));
        k = fmin(k, cmy[i]);
    }
    /* The fixed additive output target uses identity BG and UCR defaults. */
    double black = k, removal = k;
    if (use_functions) {
        if (!st || !st->clip_budget)
            return 0;
        double input[PDF_COLOR_COMPONENTS] = {k}, result[PDF_COLOR_COMPONENTS] = {0};
        if (st->gs.black_generation) {
            if (!cupidimage_pdf_evaluate_function(&st->gs.black_generation->tree, input, result,
                                                  &st->clip_budget->tests_left))
                return 0;
            black = result[0];
        }
        if (st->gs.undercolor_removal) {
            if (!cupidimage_pdf_evaluate_function(&st->gs.undercolor_removal->tree, input, result,
                                                  &st->clip_budget->tests_left))
                return 0;
            removal = result[0];
        }
    }
    for (unsigned i = 0; i < 3; i++)
        output[i] = fmax(0, fmin(1, cmy[i] - removal));
    output[3] = fmax(0, fmin(1, black));
    return 1;
}

void cupidimage_pdf_color_functions_free(pdf_color_function_store *store) {
    while (store->head) {
        pdf_color_function *entry = store->head;
        store->head = entry->next;
        cupidimage_pdf_free_function(&entry->tree);
        free(entry->key);
        free(entry);
    }
    store->bytes = store->count = 0;
}
