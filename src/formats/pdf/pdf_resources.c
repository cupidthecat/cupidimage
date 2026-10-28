#include "pdf_render_internal.h"
#include <stdlib.h>
#include <string.h>

/* Resolve a resource in the closest dictionary, never merging resource scopes.
   Returned views retain temporary object storage when necessary; original
   document and override bytes remain borrowed. The override supports font-local
   and inline scopes. */
int cupidimage_pdf_find_resource_checked(const pdf_render_state *st, const char *category,
                                         const char *name, pdf_object_view *value,
                                         pdf_object_id *reference) {
    if (value)
        memset(value, 0, sizeof(*value));
    if (reference)
        *reference = 0;
    pdf_object_id object = st->page_object;
    int override = st->resource_override != NULL;
    for (unsigned depth = 0; (object > 0 || override) && depth < 64; depth++) {
        pdf_object_view node = {0}, resources = {0}, entries = {0};
        pdf_object_id parent = 0;
        if (override) {
            node = *st->resource_override;
            node.owned = NULL;
            override = 0;
            parent = object;
        } else if (!cupidimage_pdf_state_object(st, object, &node))
            return -1;
        size_t pos;
        int present = cupidimage_pdf_optional_dictionary(st->document, st->document_size, &node,
                                                         "Resources", &resources);
        if (present != 0) {
            int ok = present < 0
                         ? -1
                         : cupidimage_pdf_optional_dictionary(st->document, st->document_size,
                                                              &resources, category, &entries);
            pos = 0;
            if (ok > 0 && name)
                ok = cupidimage_pdf_page_value(&entries, name, &pos);
            pdf_object_id target = 0;
            if (ok > 0 && name && cupidimage_pdf_page_reference(&entries, pos, &target)) {
                if (reference)
                    *reference = target;
                if (value)
                    ok = cupidimage_pdf_lookup_object(st->xref, st->document, st->document_size,
                                                      target, value);
            } else if (ok > 0 && value) {
                value->data = entries.data + pos;
                value->len = entries.len - pos;
                value->xref = entries.xref;
                if (entries.owned) {
                    value->owned = entries.owned;
                    entries.owned = NULL;
                } else if (resources.owned) {
                    value->owned = resources.owned;
                    resources.owned = NULL;
                } else if (node.owned) {
                    value->owned = node.owned;
                    node.owned = NULL;
                }
            } else if (ok > 0)
                ok = 0;
            cupidimage_pdf_object_view_release(&entries);
            cupidimage_pdf_object_view_release(&resources);
            cupidimage_pdf_object_view_release(&node);
            return ok;
        }
        if (cupidimage_pdf_page_value(&node, "Parent", &pos))
            cupidimage_pdf_page_reference(&node, pos, &parent);
        cupidimage_pdf_object_view_release(&node);
        object = parent;
    }
    return object > 0 || override ? -1 : 0;
}

int cupidimage_pdf_find_resource(const pdf_render_state *st, const char *category, const char *name,
                                 pdf_object_view *value, pdf_object_id *reference) {
    return cupidimage_pdf_find_resource_checked(st, category, name, value, reference) == 1;
}

int cupidimage_pdf_state_object(const pdf_render_state *st, pdf_object_id object,
                                pdf_object_view *view) {
    return cupidimage_pdf_lookup_object(st->xref, st->document, st->document_size, object, view) ==
           1;
}
