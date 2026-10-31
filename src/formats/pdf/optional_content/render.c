#include "../pdf_render_internal.h"
#include "internal.h"

static int oc_render_value(pdf_render_state *state, const pdf_object_view *value,
                           pdf_object_id object, int *visible) {
    pdf_optional_content fallback;
    pdf_optional_content *store = state->optional_content;
    if (!store) {
        if (!cupidimage_pdf_optional_init(&fallback, state->document, state->document_size,
                                          state->xref))
            return 0;
        store = &fallback;
    }
    int ok = cupidimage_pdf_optional_visible(store, value, object, visible);
    if (store == &fallback)
        cupidimage_pdf_optional_free(&fallback);
    return ok;
}

int cupidimage_pdf_optional_resource(pdf_render_state *state, const char *name, int *visible) {
    *visible = 1;
    pdf_object_view value = {0};
    pdf_object_id object = 0;
    int status = cupidimage_pdf_find_resource_checked(state, "Properties", name, &value, &object);
    int ok = status == 0 || (status > 0 && oc_render_value(state, &value, object, visible));
    cupidimage_pdf_object_view_release(&value);
    return ok;
}

int cupidimage_pdf_optional_dictionary_visibility(pdf_render_state *state,
                                                  const pdf_object_view *value, int *visible) {
    return oc_render_value(state, value, 0, visible);
}

int cupidimage_pdf_optional_object_visibility(pdf_render_state *state,
                                              const pdf_object_view *object, int *visible) {
    *visible = 1;
    size_t position;
    if (!cupidimage_pdf_page_value(object, "OC", &position))
        return 1;
    pdf_optional_content fallback;
    pdf_optional_content *store = state->optional_content;
    if (!store) {
        if (!cupidimage_pdf_optional_init(&fallback, state->document, state->document_size,
                                          state->xref))
            return 0;
        store = &fallback;
    }
    pdf_object_view value = {0};
    pdf_object_id identity;
    int ok = cupidimage_pdf_oc_item(store, object, &position, &value, &identity) &&
             cupidimage_pdf_optional_visible(store, &value, identity, visible);
    cupidimage_pdf_object_view_release(&value);
    if (store == &fallback)
        cupidimage_pdf_optional_free(&fallback);
    return ok;
}
