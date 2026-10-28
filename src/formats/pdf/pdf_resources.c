#include "pdf_render_internal.h"
#include <stdlib.h>
#include <string.h>

/* Resolve a resource in the closest dictionary, never merging resource scopes.
   Returned views retain temporary object storage when necessary; original
   document and override bytes remain borrowed. The override supports font-local
   and inline scopes. */
int cupidimage_pdf_find_resource(const pdf_render_state *st, const char *category, const char *name,
                                 pdf_object_view *value, int *reference) {
    if (value)
        memset(value, 0, sizeof(*value));
    if (reference)
        *reference = 0;
    int object = st->page_object, override = st->resource_override != NULL;
    for (unsigned depth = 0; (object > 0 || override) && depth < 64; depth++) {
        pdf_object_view node = {0}, resources = {0}, entries = {0};
        int parent = 0;
        if (override) {
            node = *st->resource_override;
            node.owned = NULL;
            override = 0;
            parent = object;
        } else if (!cupidimage_pdf_get_object_content(st->document, st->document_size, object,
                                                      &node))
            return 0;
        size_t pos;
        if (cupidimage_pdf_page_value(&node, "Resources", &pos)) {
            int ok = cupidimage_pdf_image_dictionary(st, &node, "Resources", &resources) &&
                     cupidimage_pdf_image_dictionary(st, &resources, category, &entries);
            pos = 0;
            if (ok && name)
                ok = cupidimage_pdf_page_value(&entries, name, &pos);
            int target = 0;
            if (ok && name && cupidimage_pdf_page_reference(&entries, pos, &target)) {
                if (reference)
                    *reference = target;
                if (value)
                    ok = cupidimage_pdf_get_object_content(st->document, st->document_size, target,
                                                           value);
            } else if (ok && value) {
                value->data = entries.data + pos;
                value->len = entries.len - pos;
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
            } else if (ok)
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
    return 0;
}
