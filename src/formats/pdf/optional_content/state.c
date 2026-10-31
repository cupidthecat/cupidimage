#include "internal.h"
#include <stdlib.h>
#include <string.h>

#define OC_GROUP_LIMIT 65536u
#define OC_INTENT_LIMIT 64u

static int oc_name_matches(const pdf_optional_content *store, const char *name) {
    if (!store->explicit_intents)
        return !strcmp(name, "View");
    for (unsigned i = 0; i < store->intent_count; i++)
        if (!strcmp(name, store->intents[i]))
            return 1;
    return 0;
}

static int oc_intent_values(pdf_optional_content *store, const pdf_object_view *view, int configure,
                            int *matches) {
    size_t pos = 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    if (pos == view->len)
        return 0;
    int array = view->data[pos] == '[';
    if (array)
        pos++;
    unsigned count = 0;
    for (;;) {
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
        if (array && pos < view->len && view->data[pos] == ']')
            return 1;
        pdf_object_view item = {0};
        pdf_object_id ignored;
        char name[128];
        size_t at = 0;
        if (count++ == OC_INTENT_LIMIT ||
            !cupidimage_pdf_oc_item(store, view, &pos, &item, &ignored))
            return 0;
        int ok = cupidimage_pdf_read_name(&item, &at, name);
        cupidimage_pdf_object_view_release(&item);
        if (!ok)
            return 0;
        if (configure) {
            char (*next)[128] = realloc(store->intents, (size_t)count * sizeof(*next));
            if (!next)
                return 0;
            store->intents = next;
            memcpy(store->intents[store->intent_count++], name, strlen(name) + 1);
        } else if (oc_name_matches(store, name))
            *matches = 1;
        if (!array)
            return 1;
    }
}

int cupidimage_pdf_oc_intent(pdf_optional_content *store, const pdf_object_view *group,
                             int *matches) {
    pdf_object_view intent = {0};
    int status = cupidimage_pdf_oc_property(store, group, "Intent", &intent);
    *matches = 0;
    if (!status) {
        *matches = oc_name_matches(store, "View");
        return 1;
    }
    int ok = status > 0 && oc_intent_values(store, &intent, 0, matches);
    cupidimage_pdf_object_view_release(&intent);
    return ok;
}

static int oc_compare_groups(const void *left, const void *right) {
    pdf_object_id a = ((const pdf_optional_group *)left)->object;
    pdf_object_id b = ((const pdf_optional_group *)right)->object;
    return (a > b) - (a < b);
}

static int oc_groups(pdf_optional_content *store, const pdf_object_view *array, unsigned enabled) {
    size_t pos = 0;
    cupidimage_pdf_skip_ws_and_comments(array->data, array->len, &pos);
    if (pos == array->len || array->data[pos++] != '[')
        return 0;
    for (;;) {
        cupidimage_pdf_skip_ws_and_comments(array->data, array->len, &pos);
        if (pos < array->len && array->data[pos] == ']')
            break;
        pdf_object_view group = {0};
        pdf_object_id object;
        if (!cupidimage_pdf_oc_item(store, array, &pos, &group, &object))
            return 0;
        if (cupidimage_pdf_oc_null(&group)) {
            cupidimage_pdf_object_view_release(&group);
            continue;
        }
        int applicable = 0;
        int ok = object &&
                 cupidimage_pdf_entry_is_name(store->document, store->document_size, &group, "Type",
                                              "OCG") &&
                 cupidimage_pdf_oc_intent(store, &group, &applicable);
        cupidimage_pdf_object_view_release(&group);
        if (!ok || store->count == OC_GROUP_LIMIT)
            return 0;
        if (store->count == store->capacity) {
            unsigned capacity = store->capacity ? store->capacity * 2 : 64;
            if (!cupidimage_pdf_oc_charge(store, store->count))
                return 0;
            pdf_optional_group *next = realloc(store->groups, (size_t)capacity * sizeof(*next));
            if (!next)
                return 0;
            store->groups = next;
            store->capacity = capacity;
        }
        store->groups[store->count++] = (pdf_optional_group){object, enabled, (unsigned)applicable};
    }
    if (!cupidimage_pdf_oc_charge(store, (size_t)store->count * 16))
        return 0;
    if (store->count > 1)
        qsort(store->groups, store->count, sizeof(*store->groups), oc_compare_groups);
    unsigned used = 0;
    for (unsigned i = 0; i < store->count; i++)
        if (!used || store->groups[used - 1].object != store->groups[i].object)
            store->groups[used++] = store->groups[i];
    store->count = used;
    return 1;
}

static int oc_override(pdf_optional_content *store, const pdf_object_view *configuration,
                       const char *key, unsigned enabled) {
    pdf_object_view array = {0};
    int status = cupidimage_pdf_oc_property(store, configuration, key, &array);
    if (status <= 0)
        return status == 0;
    size_t pos = 0;
    cupidimage_pdf_skip_ws_and_comments(array.data, array.len, &pos);
    int ok = pos < array.len && array.data[pos++] == '[';
    while (ok) {
        cupidimage_pdf_skip_ws_and_comments(array.data, array.len, &pos);
        if (pos < array.len && array.data[pos] == ']')
            break;
        pdf_object_view value = {0};
        pdf_object_id object;
        ok = cupidimage_pdf_oc_item(store, &array, &pos, &value, &object);
        if (ok && !cupidimage_pdf_oc_null(&value)) {
            ok = object && cupidimage_pdf_entry_is_name(store->document, store->document_size,
                                                        &value, "Type", "OCG");
            const pdf_optional_group *group = cupidimage_pdf_oc_find(store, object);
            if (ok && group)
                store->groups[group - store->groups].enabled = enabled;
        }
        cupidimage_pdf_object_view_release(&value);
    }
    cupidimage_pdf_object_view_release(&array);
    return ok;
}

int cupidimage_pdf_optional_init(pdf_optional_content *store, const unsigned char *document,
                                 size_t length, const pdf_xref *xref) {
    memset(store, 0, sizeof(*store));
    store->document = document;
    store->document_size = length;
    store->xref = xref;
    store->work_left = 16u * 1024u * 1024u;
    if (!xref || !xref->root_obj)
        return 1;
    pdf_object_view catalog = {0}, properties = {0}, configuration = {0}, groups = {0}, value = {0};
    int ok = 0, ignored = 0;
    if (cupidimage_pdf_lookup_object(xref, document, length, xref->root_obj, &catalog) != 1)
        goto done;
    int status =
        cupidimage_pdf_optional_dictionary(document, length, &catalog, "OCProperties", &properties);
    if (status <= 0) {
        ok = status == 0;
        goto done;
    }
    if (cupidimage_pdf_optional_dictionary(document, length, &properties, "D", &configuration) != 1)
        goto done;
    status = cupidimage_pdf_oc_property(store, &configuration, "Intent", &value);
    if (status < 0)
        goto done;
    if (status) {
        store->explicit_intents = 1;
        if (!oc_intent_values(store, &value, 1, &ignored))
            goto done;
    }
    cupidimage_pdf_object_view_release(&value);
    unsigned enabled = 1;
    status = cupidimage_pdf_oc_property(store, &configuration, "BaseState", &value);
    if (status < 0)
        goto done;
    if (status) {
        size_t pos = 0;
        char name[128];
        if (!cupidimage_pdf_read_name(&value, &pos, name))
            goto done;
        if (!strcmp(name, "OFF"))
            enabled = 0;
        else if (strcmp(name, "ON") && strcmp(name, "Unchanged"))
            goto done;
    }
    if (cupidimage_pdf_oc_property(store, &properties, "OCGs", &groups) != 1 ||
        !oc_groups(store, &groups, enabled) || !oc_override(store, &configuration, "ON", 1) ||
        !oc_override(store, &configuration, "OFF", 0))
        goto done;
    ok = 1;
done:
    cupidimage_pdf_object_view_release(&catalog);
    cupidimage_pdf_object_view_release(&properties);
    cupidimage_pdf_object_view_release(&configuration);
    cupidimage_pdf_object_view_release(&groups);
    cupidimage_pdf_object_view_release(&value);
    if (!ok)
        cupidimage_pdf_optional_free(store);
    return ok;
}

void cupidimage_pdf_optional_free(pdf_optional_content *store) {
    free(store->groups);
    free(store->intents);
    memset(store, 0, sizeof(*store));
}
