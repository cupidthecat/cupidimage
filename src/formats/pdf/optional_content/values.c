#include "../pdf_content_syntax.h"
#include "internal.h"
#include <string.h>

int cupidimage_pdf_oc_charge(pdf_optional_content *store, size_t amount) {
    if (amount > store->work_left)
        return 0;
    store->work_left -= amount;
    return 1;
}

int cupidimage_pdf_oc_null(const pdf_object_view *view) {
    size_t pos = 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    return view->len - pos >= 4 && !memcmp(view->data + pos, "null", 4) &&
           (view->len - pos == 4 || cupidimage_pdf_is_ws(view->data[pos + 4]) ||
            cupidimage_pdf_is_delim(view->data[pos + 4]));
}

int cupidimage_pdf_oc_property(pdf_optional_content *store, const pdf_object_view *view,
                               const char *key, pdf_object_view *value) {
    if (!cupidimage_pdf_oc_charge(store, 1))
        return -1;
    return cupidimage_pdf_optional_value(store->document, store->document_size, view, key, value);
}

int cupidimage_pdf_oc_item(pdf_optional_content *store, const pdf_object_view *source,
                           size_t *position, pdf_object_view *out, pdf_object_id *identity) {
    memset(out, 0, sizeof(*out));
    *identity = 0;
    size_t start = *position, end;
    cupidimage_pdf_skip_ws_and_comments(source->data, source->len, &start);
    if (start == source->len || !cupidimage_pdf_oc_charge(store, 1))
        return 0;
    end = start;
    pdf_object_id object;
    if (cupidimage_pdf_parse_reference(source, &end, &object)) {
        int status =
            cupidimage_pdf_resolve_object(store->xref, store->document, store->document_size,
                                          object, out, identity, &store->work_left);
        if (status < 0)
            return 0;
        if (!status)
            *out = (pdf_object_view){(const unsigned char *)"null", 4, NULL, store->xref};
        *position = end;
        return 1;
    }
    unsigned char first = source->data[start];
    if (first == '[') {
        if (!cupidimage_pdf_content_array(source->data, source->len, &end, NULL, NULL))
            return 0;
    } else if (first == '<') {
        if (!cupidimage_pdf_content_dictionary(source->data, source->len, &end, NULL, NULL))
            return 0;
    } else if (first == '/') {
        char name[128];
        if (!cupidimage_pdf_read_name(source, &end, name))
            return 0;
    } else {
        pdf_object_view remainder = {source->data + start, source->len - start, NULL, source->xref};
        if (!cupidimage_pdf_oc_null(&remainder))
            return 0;
        end += 4;
    }
    if (!cupidimage_pdf_oc_charge(store, end - start))
        return 0;
    *out = (pdf_object_view){source->data + start, end - start, NULL, source->xref};
    *position = end;
    return 1;
}

const pdf_optional_group *cupidimage_pdf_oc_find(const pdf_optional_content *store,
                                                 pdf_object_id object) {
    unsigned low = 0, high = store->count;
    while (low < high) {
        unsigned middle = low + (high - low) / 2;
        if (store->groups[middle].object < object)
            low = middle + 1;
        else
            high = middle;
    }
    return low < store->count && store->groups[low].object == object ? store->groups + low : NULL;
}
