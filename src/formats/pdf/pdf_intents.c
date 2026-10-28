#include "pdf_render_internal.h"
#include <string.h>

int cupidimage_pdf_intent_name(const char *name) {
    if (!strcmp(name, "Perceptual"))
        return 1;
    if (!strcmp(name, "Saturation"))
        return 2;
    if (!strcmp(name, "AbsoluteColorimetric"))
        return 3;
    /* ISO 32000-1 8.6.5.8 also assigns unknown names the relative intent. */
    return 0;
}

int cupidimage_pdf_read_intent(const pdf_render_state *st, const pdf_object_view *view,
                               const char *key, int *intent) {
    pdf_object_view value = {0};
    int present = cupidimage_pdf_optional_value(st->document, st->document_size, view, key, &value);
    if (present <= 0)
        return present == 0;
    size_t pos = 0, original = 0;
    pdf_object_id object;
    int indirect = cupidimage_pdf_page_value(view, key, &original) &&
                   cupidimage_pdf_page_reference(view, original, &object);
    char name[128];
    int ok = cupidimage_pdf_read_name(&value, &pos, name);
    if (indirect) {
        cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &pos);
        ok = ok && pos == value.len;
    }
    if (ok)
        *intent = cupidimage_pdf_intent_name(name);
    cupidimage_pdf_object_view_release(&value);
    return ok;
}
