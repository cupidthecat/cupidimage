#include "internal.h"
#include <string.h>

typedef struct {
    int applicable, visible;
} oc_result;

static int oc_group(pdf_optional_content *store, const pdf_object_view *view, pdf_object_id object,
                    oc_result *result) {
    if (!cupidimage_pdf_oc_charge(store, 1))
        return 0;
    const pdf_optional_group *group = cupidimage_pdf_oc_find(store, object);
    if (group) {
        *result = (oc_result){(int)group->applicable, (int)group->enabled};
        return 1;
    }
    if (!cupidimage_pdf_entry_is_name(store->document, store->document_size, view, "Type", "OCG"))
        return 0;
    result->visible = 1;
    return cupidimage_pdf_oc_intent(store, view, &result->applicable);
}

static int oc_expression(pdf_optional_content *store, const pdf_object_view *array, unsigned depth,
                         oc_result *result) {
    if (depth == 64 || !cupidimage_pdf_oc_charge(store, 1))
        return 0;
    size_t pos = 0;
    cupidimage_pdf_skip_ws_and_comments(array->data, array->len, &pos);
    if (pos == array->len || array->data[pos++] != '[')
        return 0;
    char operation[128];
    if (!cupidimage_pdf_read_name(array, &pos, operation))
        return 0;
    int negate = !strcmp(operation, "Not"), conjunction = !strcmp(operation, "And");
    if (!negate && !conjunction && strcmp(operation, "Or"))
        return 0;
    *result = (oc_result){0, conjunction};
    unsigned operands = 0;
    for (;;) {
        cupidimage_pdf_skip_ws_and_comments(array->data, array->len, &pos);
        if (pos < array->len && array->data[pos] == ']')
            return operands > 0 && (!negate || operands == 1);
        if (operands == 65536 || (negate && operands))
            return 0;
        operands++;
        pdf_object_view item = {0};
        pdf_object_id object;
        if (!cupidimage_pdf_oc_item(store, array, &pos, &item, &object))
            return 0;
        size_t at = 0;
        cupidimage_pdf_skip_ws_and_comments(item.data, item.len, &at);
        oc_result child = {0};
        int ok = at < item.len && item.data[at] == '['
                     ? oc_expression(store, &item, depth + 1, &child)
                     : oc_group(store, &item, object, &child);
        cupidimage_pdf_object_view_release(&item);
        if (!ok)
            return 0;
        if (!child.applicable)
            continue;
        result->applicable = 1;
        if (negate)
            result->visible = !child.visible;
        else if (conjunction)
            result->visible &= child.visible;
        else
            result->visible |= child.visible;
    }
}

static int oc_membership(pdf_optional_content *store, const pdf_object_view *view,
                         oc_result *result) {
    pdf_object_view value = {0};
    int status = cupidimage_pdf_oc_property(store, view, "VE", &value);
    if (status < 0)
        return 0;
    if (status) {
        int ok = oc_expression(store, &value, 0, result);
        cupidimage_pdf_object_view_release(&value);
        return ok;
    }
    char policy[128] = "AnyOn";
    status = cupidimage_pdf_oc_property(store, view, "P", &value);
    if (status < 0)
        return 0;
    size_t pos = 0;
    int ok = !status || cupidimage_pdf_read_name(&value, &pos, policy);
    cupidimage_pdf_object_view_release(&value);
    if (!ok)
        return 0;
    int all = !strcmp(policy, "AllOn") || !strcmp(policy, "AllOff");
    int off = !strcmp(policy, "AnyOff") || !strcmp(policy, "AllOff");
    if (!all && !off && strcmp(policy, "AnyOn"))
        return 0;
    *result = (oc_result){0, all};
    if (!cupidimage_pdf_page_value(view, "OCGs", &pos))
        return 1;
    pdf_object_id identity;
    if (!cupidimage_pdf_oc_item(store, view, &pos, &value, &identity))
        return 0;
    if (cupidimage_pdf_oc_null(&value)) {
        cupidimage_pdf_object_view_release(&value);
        return 1;
    }
    pos = 0;
    cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &pos);
    int array = pos < value.len && value.data[pos] == '[';
    if (array)
        pos++;
    unsigned count = 0;
    for (;;) {
        cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &pos);
        if (array && pos < value.len && value.data[pos] == ']')
            break;
        pdf_object_view item = {0};
        pdf_object_id object = identity;
        if (count++ == 65536) {
            ok = 0;
            break;
        }
        if (array) {
            if (!cupidimage_pdf_oc_item(store, &value, &pos, &item, &object)) {
                ok = 0;
                break;
            }
        } else
            item = (pdf_object_view){value.data, value.len, NULL, value.xref};
        oc_result group = {0};
        ok = cupidimage_pdf_oc_null(&item) || oc_group(store, &item, object, &group);
        cupidimage_pdf_object_view_release(&item);
        if (!ok)
            break;
        if (group.applicable) {
            result->applicable = 1;
            int visible = off ? !group.visible : group.visible;
            if (all)
                result->visible &= visible;
            else
                result->visible |= visible;
        }
        if (!array)
            break;
    }
    cupidimage_pdf_object_view_release(&value);
    return ok;
}

int cupidimage_pdf_optional_visible(pdf_optional_content *store, const pdf_object_view *view,
                                    pdf_object_id object, int *visible) {
    if (!store || !view || !visible || !cupidimage_pdf_oc_charge(store, 1))
        return 0;
    size_t position = 0;
    pdf_object_id reference;
    if (cupidimage_pdf_parse_reference(view, &position, &reference)) {
        pdf_object_view resolved = {0};
        position = 0;
        if (!cupidimage_pdf_oc_item(store, view, &position, &resolved, &reference))
            return 0;
        int ok = cupidimage_pdf_optional_visible(store, &resolved, reference, visible);
        cupidimage_pdf_object_view_release(&resolved);
        return ok;
    }
    oc_result result = {0};
    int ok = 1;
    if (cupidimage_pdf_entry_is_name(store->document, store->document_size, view, "Type", "OCG"))
        ok = oc_group(store, view, object, &result);
    else if (cupidimage_pdf_entry_is_name(store->document, store->document_size, view, "Type",
                                          "OCMD"))
        ok = oc_membership(store, view, &result);
    *visible = !result.applicable || result.visible;
    return ok;
}
