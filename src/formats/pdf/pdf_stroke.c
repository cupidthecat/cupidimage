#include "pdf_render_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ISO 32000-1, 8.4.3: lengths and dash phase are measured in user space. */
int cupidimage_pdf_set_dash(pdf_graphics_state *gs, const unsigned char *data, size_t length,
                            double phase) {
    double values[32], sum = 0;
    int count = 0;
    size_t pos = 0;
    if (!isfinite(phase) || fabs(phase) > 1e30)
        return 0;
    while (1) {
        cupidimage_pdf_skip_ws_and_comments(data, length, &pos);
        if (pos == length)
            break;
        if (count == 32 || !cupidimage_pdf_parse_pdf_number(data, length, &pos, &values[count]) ||
            !isfinite(values[count]) || values[count] < 0 || values[count] > 1e30)
            return 0;
        if (pos < length && !cupidimage_pdf_is_ws(data[pos]) && data[pos] != '%')
            return 0;
        sum += values[count++];
    }
    if (count && sum == 0)
        return 0;
    memcpy(gs->dash, values, (size_t)count * sizeof(double));
    gs->dash_count = count;
    gs->dash_phase = phase;
    return 1;
}

static int stroke_skip_reference(const pdf_object_view *view, size_t *pos) {
    double ignored;
    for (int i = 0; i < 2; i++) {
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
        if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &ignored))
            return 0;
    }
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    return *pos < view->len && view->data[(*pos)++] == 'R';
}

static int stroke_read_array(const pdf_render_state *st, const pdf_object_view *view, size_t *pos,
                             pdf_graphics_state *gs, unsigned depth) {
    pdf_object_id object;
    if (depth == 16)
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    if (cupidimage_pdf_page_reference(view, *pos, &object)) {
        pdf_object_view indirect = {0};
        size_t p = 0;
        int ok = cupidimage_pdf_state_object(st, object, &indirect) &&
                 stroke_read_array(st, &indirect, &p, gs, depth + 1) &&
                 stroke_skip_reference(view, pos);
        cupidimage_pdf_object_view_release(&indirect);
        return ok;
    }
    if (*pos == view->len || view->data[(*pos)++] != '[')
        return 0;
    double sum = 0;
    gs->dash_count = 0;
    while (1) {
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
        if (*pos < view->len && view->data[*pos] == ']') {
            (*pos)++;
            break;
        }
        double value;
        if (gs->dash_count == 32 || !cupidimage_pdf_read_number(st, view, *pos, 0, &value) ||
            !isfinite(value) || value < 0 || value > 1e30)
            return 0;
        if (cupidimage_pdf_page_reference(view, *pos, &object)) {
            if (!stroke_skip_reference(view, pos))
                return 0;
        } else {
            double ignored;
            if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &ignored))
                return 0;
        }
        sum += value;
        gs->dash[gs->dash_count++] = value;
    }
    return !gs->dash_count || sum > 0;
}

static int stroke_read_dash(const pdf_render_state *st, const pdf_object_view *view, size_t pos,
                            pdf_graphics_state *gs, unsigned depth) {
    pdf_object_id object;
    if (depth == 16)
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    if (cupidimage_pdf_page_reference(view, pos, &object)) {
        pdf_object_view indirect = {0};
        int ok = cupidimage_pdf_state_object(st, object, &indirect) &&
                 stroke_read_dash(st, &indirect, 0, gs, depth + 1);
        cupidimage_pdf_object_view_release(&indirect);
        return ok;
    }
    if (pos == view->len || view->data[pos++] != '[' || !stroke_read_array(st, view, &pos, gs, 0))
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    double phase;
    if (!cupidimage_pdf_read_number(st, view, pos, 0, &phase) || !isfinite(phase) ||
        fabs(phase) > 1e30)
        return 0;
    if (cupidimage_pdf_page_reference(view, pos, &object)) {
        if (!stroke_skip_reference(view, &pos))
            return 0;
    } else {
        double ignored;
        if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, &pos, &ignored))
            return 0;
    }
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    if (pos == view->len || view->data[pos] != ']')
        return 0;
    gs->dash_phase = phase;
    return 1;
}

int cupidimage_pdf_set_flatness(pdf_graphics_state *state, double value) {
    if (!isfinite(value) || value < 0 || value > 100)
        return 0;
    state->flatness = value ? value : 1;
    return 1;
}

double cupidimage_pdf_flatness(const pdf_graphics_state *state, double ceiling) {
    return fmin(state->flatness > 0 ? state->flatness : 1, ceiling);
}

int cupidimage_pdf_apply_stroke_view(const pdf_render_state *st, const pdf_object_view *view,
                                     pdf_graphics_state *next) {
    double flatness = next->flatness;
    if (!cupidimage_pdf_graphics_number(st, view, "FL", 0, 100, &flatness) ||
        !cupidimage_pdf_set_flatness(next, flatness))
        return 0;
    const char *keys[] = {"LC", "LJ", "ML"};
    double numbers[3] = {next->line_cap, next->line_join, next->miter_limit};
    for (int i = 0; i < 3; i++) {
        if (!cupidimage_pdf_graphics_number(st, view, keys[i], i == 2 ? 1 : 0, i == 2 ? 1e6 : 2,
                                            &numbers[i]) ||
            (i != 2 && floor(numbers[i]) != numbers[i]))
            return 0;
    }
    next->line_cap = (int)numbers[0];
    next->line_join = (int)numbers[1];
    next->miter_limit = numbers[2];
    pdf_object_view dash = {0};
    int present = cupidimage_pdf_optional_value(st->document, st->document_size, view, "D", &dash);
    int ok = present == 0 || (present > 0 && stroke_read_dash(st, &dash, 0, next, 0));
    cupidimage_pdf_object_view_release(&dash);
    return ok;
}
