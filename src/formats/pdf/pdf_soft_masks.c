#include "pdf_functions.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int mask_null(const pdf_object_view *view) {
    size_t p = 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &p);
    return view->len - p >= 4 && !memcmp(view->data + p, "null", 4) &&
           (view->len - p == 4 || cupidimage_pdf_is_ws(view->data[p + 4]) ||
            cupidimage_pdf_is_delim(view->data[p + 4]));
}

void cupidimage_pdf_soft_mask_retain(pdf_soft_mask *mask) {
    if (mask)
        mask->references++;
}

void cupidimage_pdf_soft_mask_release(pdf_render_state *st, pdf_soft_mask *mask) {
    if (mask && --mask->references == 0) {
        st->form_budget->transparency_bytes_live -= mask->bytes;
        free(mask);
    }
}

int cupidimage_pdf_load_soft_mask(pdf_render_state *st, const pdf_graphics_state *graphics,
                                  const pdf_object_view *view, pdf_soft_mask **out, int *replace) {
    pdf_object_view dictionary = {0}, form = {0}, attributes = {0}, transfer = {0};
    pdf_render_state *temporary = NULL;
    pdf_function_tree *function = NULL;
    pdf_transparency_group surface = {0};
    pdf_soft_mask *mask = NULL;
    pdf_object_id object;
    int ok = 0, luminosity, gray;
    size_t pos = 0;
    *out = NULL;
    *replace = 0;
    if (!cupidimage_pdf_dictionary_value(st->document, st->document_size, view, "SMask",
                                         &dictionary))
        goto done;
    if (mask_null(&dictionary)) {
        ok = 1;
        goto done;
    }
    char name[128];
    if (cupidimage_pdf_read_name(&dictionary, &pos, name)) {
        if (strcmp(name, "None"))
            goto done;
        *replace = ok = 1;
        goto done;
    }
    pos = 0;
    cupidimage_pdf_skip_ws_and_comments(dictionary.data, dictionary.len, &pos);
    if (dictionary.len - pos < 2 || memcmp(dictionary.data + pos, "<<", 2))
        goto done;
    luminosity = cupidimage_pdf_entry_is_name(st->document, st->document_size, &dictionary, "S",
                                              "Luminosity");
    if (!luminosity &&
        !cupidimage_pdf_entry_is_name(st->document, st->document_size, &dictionary, "S", "Alpha"))
        goto done;
    if (!cupidimage_pdf_page_value(&dictionary, "G", &pos) ||
        !cupidimage_pdf_page_reference(&dictionary, pos, &object) || object <= 0 ||
        !cupidimage_pdf_state_object(st, object, &form) ||
        !cupidimage_pdf_entry_is_name(st->document, st->document_size, &form, "Subtype", "Form") ||
        !cupidimage_pdf_dictionary_value(st->document, st->document_size, &form, "Group",
                                         &attributes) ||
        !cupidimage_pdf_entry_is_name(st->document, st->document_size, &attributes, "S",
                                      "Transparency"))
        goto done;
    temporary = malloc(sizeof(*temporary));
    if (!temporary)
        goto done;
    *temporary = *st;
    temporary->text_group = NULL;
    temporary->gs = *graphics;
    temporary->gs.clip = NULL;
    temporary->gs.soft_mask = NULL;
    temporary->gs.fill_alpha = temporary->gs.stroke_alpha = 1;
    temporary->gs.blend_mode = 0;
    temporary->gs_stack_len = 0;
    temporary->text_clip = NULL;
    temporary->text_winding = NULL;
    temporary->text_winding_bytes = 0;
    temporary->in_text = temporary->glyph_active = 0;
    temporary->glyph_uncolored = temporary->pattern_uncolored = 0;
    temporary->mask_rendering = 1;
    temporary->image_failed = 0;
    if (cupidimage_pdf_page_value(&form, "Resources", &pos)) {
        temporary->resource_override = &form;
        temporary->page_object = object;
    }
    gray = st->group ? st->group->gray : 0;
    if (!cupidimage_pdf_group_space(temporary, &attributes, luminosity, &gray))
        goto done;
    double background[3] = {0, 0, 0};
    if (luminosity && cupidimage_pdf_page_value(&dictionary, "BC", &pos)) {
        pdf_object_view bc = {0};
        int valid = cupidimage_pdf_dictionary_value(st->document, st->document_size, &dictionary,
                                                    "BC", &bc);
        if (valid && !mask_null(&bc))
            valid = cupidimage_pdf_function_array(temporary, &dictionary, "BC", background,
                                                  gray ? 1 : 3);
        cupidimage_pdf_object_view_release(&bc);
        if (!valid)
            goto done;
    }
    if (gray)
        background[1] = background[2] = background[0];
    for (unsigned i = 0; i < 3; i++)
        background[i] = fmax(0, fmin(1, background[i]));
    if (cupidimage_pdf_page_value(&dictionary, "TR", &pos)) {
        if (!cupidimage_pdf_dictionary_value(st->document, st->document_size, &dictionary, "TR",
                                             &transfer))
            goto done;
        pos = 0;
        if (!mask_null(&transfer) &&
            !(cupidimage_pdf_read_name(&transfer, &pos, name) && !strcmp(name, "Identity"))) {
            function = calloc(1, sizeof(*function));
            if (!function ||
                !cupidimage_pdf_load_function(temporary, &dictionary, "TR", 1, 1, function))
                goto done;
        }
    }
    if (!cupidimage_pdf_group_surface(st, &surface, 1, luminosity ? background : NULL))
        goto done;
    surface.gray = gray;
    temporary->img = &surface.image;
    temporary->group = &surface;
    temporary->initial_gs = temporary->gs;
    if (!cupidimage_pdf_draw_form(temporary, object, &form))
        goto done;
    size_t pixels = (size_t)surface.image.width * surface.image.height;
    size_t live = st->form_budget->transparency_bytes_live;
    if (live > PDF_TRANSPARENCY_MEMORY_LIMIT ||
        sizeof(*mask) > PDF_TRANSPARENCY_MEMORY_LIMIT - live ||
        pixels > (PDF_TRANSPARENCY_MEMORY_LIMIT - live - sizeof(*mask)) / sizeof(double) ||
        pixels > st->clip_budget->tests_left)
        goto done;
    size_t bytes = sizeof(*mask) + pixels * sizeof(double);
    mask = malloc(bytes);
    if (!mask)
        goto done;
    mask->references = 1;
    mask->width = surface.image.width;
    mask->height = surface.image.height;
    mask->bytes = bytes;
    st->form_budget->transparency_bytes_live += bytes;
    st->clip_budget->tests_left -= pixels;
    for (size_t pixel = 0; pixel < pixels; pixel++) {
        const double *color = surface.color + pixel * 3;
        double value = luminosity
                           ? (gray ? color[0] : .3 * color[0] + .59 * color[1] + .11 * color[2])
                           : surface.alpha[pixel];
        if (function) {
            double input[2] = {value, 0}, output[4];
            if (!cupidimage_pdf_evaluate_function(function, input, output,
                                                  &st->form_budget->shade_evaluations_left))
                goto done;
            value = output[0];
        }
        if (!isfinite(value))
            goto done;
        mask->values[pixel] = fmax(0, fmin(1, value));
    }
    *out = mask;
    mask = NULL;
    *replace = ok = 1;
done:
    cupidimage_pdf_soft_mask_release(st, mask);
    cupidimage_pdf_group_free(st, &surface);
    if (function) {
        cupidimage_pdf_free_function(function);
        free(function);
    }
    free(temporary);
    cupidimage_pdf_object_view_release(&transfer);
    cupidimage_pdf_object_view_release(&attributes);
    cupidimage_pdf_object_view_release(&form);
    cupidimage_pdf_object_view_release(&dictionary);
    return ok;
}
