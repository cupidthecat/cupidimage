#include "pdf_functions.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

int cupidimage_pdf_function_array(const pdf_render_state *st, const pdf_object_view *parent,
                                  const char *key, double *values, int count) {
    size_t pos;
    if (!cupidimage_pdf_page_value(parent, key, &pos))
        return 0;
    pdf_object_view owned = {0};
    const pdf_object_view *view = parent;
    pdf_object_id object;
    int ok = 0;
    for (unsigned depth = 0; cupidimage_pdf_page_reference(view, pos, &object); depth++) {
        if (depth == 16)
            goto done;
        pdf_object_view next = {0};
        if (!cupidimage_pdf_state_object(st, object, &next))
            goto done;
        cupidimage_pdf_object_view_release(&owned);
        owned = next;
        view = &owned;
        pos = 0;
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    }
    if (pos == view->len || view->data[pos++] != '[')
        goto done;
    for (int i = 0; i < count; i++) {
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
        if (!cupidimage_pdf_read_number(st, view, pos, 0, &values[i]))
            goto done;
        double ignored;
        if (cupidimage_pdf_page_reference(view, pos, &object)) {
            cupidimage_pdf_parse_pdf_number(view->data, view->len, &pos, &ignored);
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
            cupidimage_pdf_parse_pdf_number(view->data, view->len, &pos, &ignored);
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
            pos++;
        } else if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, &pos, &ignored))
            goto done;
    }
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    ok = pos < view->len && view->data[pos] == ']';
done:
    cupidimage_pdf_object_view_release(&owned);
    return ok;
}

static int pdf_function_value(const pdf_render_state *, const pdf_object_view *, size_t *, int, int,
                              pdf_function_tree *, unsigned, int);

static int pdf_function_parts(const pdf_render_state *st, const pdf_object_view *view, size_t *pos,
                              int inputs, int components, pdf_function_tree *tree, unsigned depth,
                              pdf_function_node *node, int joining) {
    if (*pos == view->len || view->data[(*pos)++] != '[')
        return 0;
    for (;;) {
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
        if (*pos < view->len && view->data[*pos] == ']') {
            (*pos)++;
            break;
        }
        if (node->count == PDF_FUNCTION_PARTS || (joining && node->count == components))
            return 0;
        int child =
            pdf_function_value(st, view, pos, inputs, joining ? 1 : components, tree, depth + 1, 0);
        if (child < 0)
            return 0;
        node->children[node->count++] = child;
    }
    return node->count > 0 && (!joining || node->count == components);
}

static int pdf_stitching_parts(const pdf_render_state *st, const pdf_object_view *parent,
                               pdf_function_tree *tree, unsigned depth, pdf_function_node *node) {
    size_t pos;
    if (!cupidimage_pdf_page_value(parent, "Functions", &pos))
        return 0;
    pdf_object_view owned = {0};
    const pdf_object_view *view = parent;
    pdf_object_id object;
    int ok = 0;
    for (unsigned refs = 0; cupidimage_pdf_page_reference(view, pos, &object); refs++) {
        if (refs == 16)
            goto done;
        pdf_object_view next = {0};
        if (!cupidimage_pdf_state_object(st, object, &next))
            goto done;
        cupidimage_pdf_object_view_release(&owned);
        owned = next;
        view = &owned;
        pos = 0;
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    }
    ok = pdf_function_parts(st, view, &pos, 1, node->components, tree, depth, node, 0);
done:
    cupidimage_pdf_object_view_release(&owned);
    return ok;
}

static int pdf_function_value(const pdf_render_state *st, const pdf_object_view *parent,
                              size_t *pos, int inputs, int components, pdf_function_tree *tree,
                              unsigned depth, int allow_array) {
    if (depth >= 16 || tree->count == PDF_FUNCTION_NODES)
        return -1;
    cupidimage_pdf_skip_ws_and_comments(parent->data, parent->len, pos);
    pdf_object_id object;
    if (cupidimage_pdf_page_reference(parent, *pos, &object)) {
        double ignored;
        cupidimage_pdf_parse_pdf_number(parent->data, parent->len, pos, &ignored);
        cupidimage_pdf_skip_ws_and_comments(parent->data, parent->len, pos);
        cupidimage_pdf_parse_pdf_number(parent->data, parent->len, pos, &ignored);
        cupidimage_pdf_skip_ws_and_comments(parent->data, parent->len, pos);
        (*pos)++;
        pdf_object_view view = {0};
        if (!cupidimage_pdf_state_object(st, object, &view))
            return -1;
        size_t p = 0;
        int result =
            pdf_function_value(st, &view, &p, inputs, components, tree, depth + 1, allow_array);
        cupidimage_pdf_object_view_release(&view);
        return result;
    }
    int index = tree->count++;
    pdf_function_node *node = &tree->nodes[index];
    node->components = components;
    node->inputs = inputs;
    if (*pos < parent->len && parent->data[*pos] == '[' && allow_array) {
        node->type = -1;
        if (!pdf_function_parts(st, parent, pos, inputs, components, tree, depth, node, 1))
            return -1;
        for (int axis = 0; axis < inputs; axis++) {
            node->domain[axis * 2] = tree->nodes[node->children[0]].domain[axis * 2];
            node->domain[axis * 2 + 1] = tree->nodes[node->children[0]].domain[axis * 2 + 1];
            for (int i = 1; i < node->count; i++) {
                node->domain[axis * 2] =
                    fmax(node->domain[axis * 2], tree->nodes[node->children[i]].domain[axis * 2]);
                node->domain[axis * 2 + 1] =
                    fmin(node->domain[axis * 2 + 1],
                         tree->nodes[node->children[i]].domain[axis * 2 + 1]);
            }
            if (node->domain[axis * 2] > node->domain[axis * 2 + 1])
                return -1;
        }
        return index;
    }
    pdf_object_view view = {0};
    size_t end, p, value_start = *pos;
    if (!cupidimage_pdf_dictionary_view(parent, *pos, &view, &end))
        return -1;
    *pos = end;
    if (!cupidimage_pdf_image_integer(st, &view, "FunctionType", &node->type) ||
        (node->type != 0 && node->type != 2 && node->type != 3 && node->type != 4) ||
        ((node->type == 2 || node->type == 3) && inputs != 1) ||
        !cupidimage_pdf_function_array(st, &view, "Domain", node->domain, inputs * 2))
        return -1;
    for (int axis = 0; axis < inputs; axis++)
        if (node->domain[axis * 2] > node->domain[axis * 2 + 1])
            return -1;
    node->has_range = cupidimage_pdf_page_value(&view, "Range", &p);
    if (node->has_range) {
        if (!cupidimage_pdf_function_array(st, &view, "Range", node->range, components * 2))
            return -1;
        for (int i = 0; i < components; i++)
            if (node->range[i * 2] > node->range[i * 2 + 1])
                return -1;
    }
    if (node->type == 4) {
        if (!node->has_range)
            return -1;
        pdf_object_view source = {parent->data + value_start, parent->len - value_start, NULL,
                                  parent->xref};
        const unsigned char *raw;
        size_t length, decoded_length;
        unsigned char *decoded = NULL;
        if (!cupidimage_pdf_object_stream(st->document, st->document_size, &source, &raw,
                                          &length) ||
            !cupidimage_pdf_decode_stream(&source, raw, length, &decoded, &decoded_length))
            return -1;
        if (decoded_length > 64u * 1024u * 1024u - tree->sample_bytes) {
            free(decoded);
            return -1;
        }
        node->calculator = cupidimage_pdf_calculator_compile(decoded, decoded_length);
        free(decoded);
        if (!node->calculator)
            return -1;
        tree->sample_bytes += decoded_length;
    } else if (node->type == 0) {
        double sizes[PDF_COLOR_COMPONENTS];
        int order = 1;
        if (!node->has_range || !cupidimage_pdf_function_array(st, &view, "Size", sizes, inputs) ||
            !cupidimage_pdf_image_integer(st, &view, "BitsPerSample", &node->bits) ||
            (node->bits != 1 && node->bits != 2 && node->bits != 4 && node->bits != 8 &&
             node->bits != 12 && node->bits != 16 && node->bits != 24 && node->bits != 32) ||
            (cupidimage_pdf_page_value(&view, "Order", &p) &&
             !cupidimage_pdf_image_integer(st, &view, "Order", &order)) ||
            order != 1)
            return -1;
        uint64_t count = (unsigned)components;
        for (int axis = 0; axis < inputs; axis++) {
            if (sizes[axis] < 1 || sizes[axis] > INT_MAX || floor(sizes[axis]) != sizes[axis])
                return -1;
            node->size[axis] = (int)sizes[axis];
            if (count >
                (64u * 1024u * 1024u * 8u) / (unsigned)node->bits / (unsigned)node->size[axis])
                return -1;
            count *= node->size[axis];
            node->encode[axis * 2] = 0;
            node->encode[axis * 2 + 1] = node->size[axis] - 1;
        }
        size_t expected = (size_t)((count * (unsigned)node->bits + 7) / 8);
        if (expected > 64u * 1024u * 1024u - tree->sample_bytes)
            return -1;
        if (cupidimage_pdf_page_value(&view, "Encode", &p) &&
            !cupidimage_pdf_function_array(st, &view, "Encode", node->encode, inputs * 2))
            return -1;
        memcpy(node->decode, node->range, (size_t)components * 2 * sizeof(double));
        if (cupidimage_pdf_page_value(&view, "Decode", &p) &&
            !cupidimage_pdf_function_array(st, &view, "Decode", node->decode, components * 2))
            return -1;
        pdf_object_view source = {parent->data + value_start, parent->len - value_start, NULL,
                                  parent->xref};
        const unsigned char *raw;
        size_t length, decoded_length;
        unsigned char *decoded = NULL;
        if (!cupidimage_pdf_object_stream(st->document, st->document_size, &source, &raw,
                                          &length) ||
            !cupidimage_pdf_decode_stream(&source, raw, length, &decoded, &decoded_length))
            return -1;
        if (decoded_length < expected ||
            decoded_length > 64u * 1024u * 1024u - tree->sample_bytes) {
            free(decoded);
            return -1;
        }
        node->samples = decoded;
        tree->sample_bytes += decoded_length;
    } else if (node->type == 2) {
        if (!cupidimage_pdf_page_value(&view, "N", &p) ||
            !cupidimage_pdf_read_number(st, &view, p, 0, &node->exponent))
            return -1;
        if ((floor(node->exponent) != node->exponent && node->domain[0] < 0) ||
            (node->exponent < 0 && node->domain[0] <= 0 && node->domain[1] >= 0))
            return -1;
        if (cupidimage_pdf_page_value(&view, "C0", &p)) {
            if (!cupidimage_pdf_function_array(st, &view, "C0", node->c0, components))
                return -1;
        } else if (components != 1)
            return -1;
        if (cupidimage_pdf_page_value(&view, "C1", &p)) {
            if (!cupidimage_pdf_function_array(st, &view, "C1", node->c1, components))
                return -1;
        } else {
            if (components != 1)
                return -1;
            node->c1[0] = 1;
        }
    } else {
        if (!pdf_stitching_parts(st, &view, tree, depth, node) ||
            !cupidimage_pdf_function_array(st, &view, "Bounds", node->bounds, node->count - 1) ||
            !cupidimage_pdf_function_array(st, &view, "Encode", node->encode, node->count * 2))
            return -1;
        if (node->count > 1 && node->domain[0] == node->domain[1])
            return -1;
        double previous = node->domain[0];
        for (int i = 0; i < node->count - 1; i++) {
            if (node->bounds[i] <= previous || node->bounds[i] > node->domain[1] ||
                (node->bounds[i] == node->domain[1] && i != node->count - 2))
                return -1;
            previous = node->bounds[i];
        }
    }
    return index;
}

int cupidimage_pdf_load_function(const pdf_render_state *st, const pdf_object_view *parent,
                                 const char *key, int inputs, int components,
                                 pdf_function_tree *tree) {
    size_t pos;
    cupidimage_pdf_free_function(tree);
    memset(tree, 0, sizeof(*tree));
    if (inputs < 1 || inputs > PDF_COLOR_COMPONENTS || components < 1 ||
        components > PDF_COLOR_COMPONENTS || !cupidimage_pdf_page_value(parent, key, &pos))
        return 0;
    if (pdf_function_value(st, parent, &pos, inputs, components, tree, 0, 1) == 0)
        return 1;
    cupidimage_pdf_free_function(tree);
    return 0;
}

int cupidimage_pdf_load_function_value(const pdf_render_state *st, const pdf_object_view *view,
                                       size_t *pos, int inputs, int components,
                                       pdf_function_tree *tree) {
    cupidimage_pdf_free_function(tree);
    memset(tree, 0, sizeof(*tree));
    if (inputs >= 1 && inputs <= PDF_COLOR_COMPONENTS && components >= 1 &&
        components <= PDF_COLOR_COMPONENTS &&
        pdf_function_value(st, view, pos, inputs, components, tree, 0, 0) == 0)
        return 1;
    cupidimage_pdf_free_function(tree);
    return 0;
}

size_t cupidimage_pdf_function_bytes(const pdf_function_tree *tree) {
    size_t bytes = sizeof(*tree) + tree->sample_bytes;
    for (int i = 0; i < tree->count; i++)
        bytes += cupidimage_pdf_calculator_bytes(tree->nodes[i].calculator);
    return bytes;
}

void cupidimage_pdf_free_function(pdf_function_tree *tree) {
    for (int i = 0; i < tree->count; i++) {
        free(tree->nodes[i].samples);
        tree->nodes[i].samples = NULL;
        free(tree->nodes[i].calculator);
        tree->nodes[i].calculator = NULL;
    }
    tree->count = 0;
    tree->sample_bytes = 0;
}

static double pdf_function_sample(const pdf_function_node *node, size_t index) {
    size_t bit = index * (unsigned)node->bits, start = bit / 8;
    unsigned leading = (unsigned)(bit % 8), bytes = (leading + (unsigned)node->bits + 7) / 8;
    uint64_t value = 0;
    for (unsigned i = 0; i < bytes; i++)
        value = (value << 8) | node->samples[start + i];
    value >>= bytes * 8 - leading - (unsigned)node->bits;
    return (double)(value & ((UINT64_C(1) << node->bits) - 1));
}

static int pdf_sampled_evaluate(const pdf_function_node *node, const double *input, double *out,
                                size_t *budget) {
    size_t base = 0, stride = 1, steps[PDF_COLOR_COMPONENTS];
    double fractions[PDF_COLOR_COMPONENTS];
    unsigned active = 0;
    for (int axis = 0; axis < node->inputs; axis++) {
        double span = node->domain[axis * 2 + 1] - node->domain[axis * 2];
        double value = span == 0 ? node->encode[axis * 2]
                                 : node->encode[axis * 2] +
                                       (input[axis] - node->domain[axis * 2]) / span *
                                           (node->encode[axis * 2 + 1] - node->encode[axis * 2]);
        if (!isfinite(value))
            return 0;
        value = fmin(node->size[axis] - 1, fmax(0, value));
        size_t low = (size_t)floor(value);
        double fraction = value - low;
        base += low * stride;
        if (fraction > 0 && low + 1 < (size_t)node->size[axis]) {
            steps[active] = stride;
            fractions[active++] = fraction;
        }
        stride *= node->size[axis];
    }
    uint64_t corners = UINT64_C(1) << active;
    uint64_t cost = node->inputs + corners * (active + node->components);
    if (cost > *budget)
        return 0;
    *budget -= (size_t)cost;
    for (int c = 0; c < node->components; c++)
        out[c] = 0;
    for (uint64_t corner = 0; corner < corners; corner++) {
        size_t index = base;
        double weight = 1;
        for (unsigned axis = 0; axis < active; axis++) {
            int upper = (int)((corner >> axis) & 1);
            if (upper)
                index += steps[axis];
            weight *= upper ? fractions[axis] : 1 - fractions[axis];
        }
        for (int c = 0; c < node->components; c++)
            out[c] += weight * pdf_function_sample(node, index * node->components + c);
    }
    double maximum = (double)((UINT64_C(1) << node->bits) - 1);
    for (int c = 0; c < node->components; c++)
        out[c] = node->decode[c * 2] +
                 out[c] / maximum * (node->decode[c * 2 + 1] - node->decode[c * 2]);
    return 1;
}

static int pdf_function_evaluate_node(const pdf_function_tree *tree, int index, const double *input,
                                      double *out, size_t *budget) {
    if (!*budget || index < 0 || index >= tree->count)
        return 0;
    (*budget)--;
    const pdf_function_node *node = &tree->nodes[index];
    double x[PDF_COLOR_COMPONENTS] = {0};
    for (int axis = 0; axis < node->inputs; axis++) {
        if (!isfinite(input[axis]))
            return 0;
        x[axis] = fmin(node->domain[axis * 2 + 1], fmax(node->domain[axis * 2], input[axis]));
    }
    if (node->type == -1) {
        for (int i = 0; i < node->count; i++) {
            double component[PDF_COLOR_COMPONENTS];
            if (!pdf_function_evaluate_node(tree, node->children[i], x, component, budget))
                return 0;
            out[i] = component[0];
        }
    } else if (node->type == 4) {
        if (!cupidimage_pdf_calculator_evaluate(node->calculator, x, node->inputs, out,
                                                node->components, budget))
            return 0;
    } else if (node->type == 0) {
        if (!pdf_sampled_evaluate(node, x, out, budget))
            return 0;
    } else if (node->type == 2) {
        double power = pow(x[0], node->exponent);
        if (!isfinite(power))
            return 0;
        for (int i = 0; i < node->components; i++)
            out[i] = node->c0[i] + power * (node->c1[i] - node->c0[i]);
    } else {
        int part = 0;
        while (part < node->count - 1 && x[0] >= node->bounds[part])
            part++;
        double low = part ? node->bounds[part - 1] : node->domain[0];
        double high = part < node->count - 1 ? node->bounds[part] : node->domain[1];
        double mapped = high == low ? node->encode[part * 2]
                                    : node->encode[part * 2] +
                                          (x[0] - low) / (high - low) *
                                              (node->encode[part * 2 + 1] - node->encode[part * 2]);
        double mapped_input[2] = {mapped, 0};
        if (!pdf_function_evaluate_node(tree, node->children[part], mapped_input, out, budget))
            return 0;
    }
    for (int i = 0; i < node->components; i++) {
        if (!isfinite(out[i]))
            return 0;
        if (node->has_range)
            out[i] = fmin(node->range[i * 2 + 1], fmax(node->range[i * 2], out[i]));
    }
    return 1;
}

int cupidimage_pdf_evaluate_function(const pdf_function_tree *tree, const double *input,
                                     double *output, size_t *budget) {
    return tree->count && pdf_function_evaluate_node(tree, 0, input, output, budget);
}
