#include "type1_gridfit.h"
#include <math.h>
#include <string.h>

static int ps_type1_coordinate(double value) {
    return isfinite(value) && fabs(value) <= 4096;
}

int cupidimage_pdf_ps_type1_inputs(const pdf_ps_private_hints *private_hints,
                                   const pdf_type1_hint_trace *trace, const pdf_cff_outline *source,
                                   const pdf_ps_gridfit_size *size, pdf_ps_private_hints *selected,
                                   unsigned *stem_count) {
    if (!private_hints || !trace || !source || !size || !selected || !stem_count ||
        source->composite || source->count > 65536 || (source->count && !source->segments) ||
        trace->count > 65536 || (trace->count && !trace->events) ||
        (trace->diagnostics & ~(PDF_T1_HINT_BAD_WIDTH | PDF_T1_HINT_COUNTER_DATA)))
        return 0;
    /* This profile uses blue zones, shift/fuzz/scale and LanguageGroup. It does
     * not infer values for unused or unresolved stem-snap/darkening fields. */
    const unsigned fields = (1u << PDF_PS_BLUE_VALUES) | (1u << PDF_PS_OTHER_BLUES) |
                            (1u << PDF_PS_FAMILY_BLUES) | (1u << PDF_PS_FAMILY_OTHER_BLUES) |
                            (1u << PDF_PS_BLUE_SCALE) | (1u << PDF_PS_BLUE_SHIFT) |
                            (1u << PDF_PS_BLUE_FUZZ) | (1u << PDF_PS_LANGUAGE_GROUP);
    if (private_hints->invalid & fields)
        return 0;
    *selected = *private_hints;
    selected->invalid = 0;
    selected->counts[4] = selected->counts[5] = 0;
    memset(selected->arrays[4], 0, sizeof(selected->arrays[4]));
    memset(selected->arrays[5], 0, sizeof(selected->arrays[5]));
    selected->scalars[3] = selected->scalars[4] = selected->scalars[5] = selected->scalars[7] = 0;
    double origin[2] = {size->origin_x / 65536.0, size->origin_y / 65536.0};
    *stem_count = 0;
    for (unsigned i = 0; i < trace->count; i++) {
        const pdf_type1_hint_event *event = trace->events + i;
        if (event->kind > PDF_T1_HINT_COUNTER || event->vertical > 1 ||
            (event->kind > PDF_T1_HINT_STEM3 && event->kind != PDF_T1_HINT_COUNTER &&
             event->vertical) ||
            event->segment > source->count || (i && event->segment < trace->events[i - 1].segment))
            return 0;
        for (unsigned j = 0; j < 6; j++)
            if (!isfinite(event->values[j]) || fabs(event->values[j]) > 1e30)
                return 0;
        if (event->kind == PDF_T1_HINT_COUNTER &&
            (!(trace->diagnostics & PDF_T1_HINT_COUNTER_DATA) || event->segment != 0 ||
             event->values[2] < 0 || event->values[2] > 65535 ||
             floor(event->values[2]) != event->values[2] ||
             event->values[3] != (event->values[1] < 0) || event->values[4] != 0 ||
             event->values[5] != 0))
            return 0;
        if (event->kind > PDF_T1_HINT_STEM3)
            continue;
        unsigned count = event->kind == PDF_T1_HINT_STEM3 ? 3 : 1;
        if (*stem_count > 96 - count)
            return 0;
        *stem_count += count;
        for (unsigned j = 0; j < count; j++) {
            double start = event->values[2 * j], width = event->values[2 * j + 1];
            if (!ps_type1_coordinate(start) || !ps_type1_coordinate(start + width) ||
                !ps_type1_coordinate(start + origin[event->vertical ? 0 : 1]) ||
                !ps_type1_coordinate(start + width + origin[event->vertical ? 0 : 1]) ||
                (width < 0 && width != -20 && width != -21))
                return 0;
        }
    }
    int open = 0;
    for (unsigned i = 0; i < source->count; i++) {
        const pdf_cff_segment *segment = source->segments + i;
        if (segment->kind > PDF_CFF_SETPOINT || (segment->kind == PDF_CFF_MOVE && open) ||
            (segment->kind != PDF_CFF_MOVE && segment->kind != PDF_CFF_SETPOINT && !open))
            return 0;
        for (unsigned j = 0; j < (segment->kind == PDF_CFF_CURVE ? 6u : 2u); j++)
            if (!ps_type1_coordinate(segment->points[j] + origin[j % 2]))
                return 0;
        if (segment->kind == PDF_CFF_MOVE)
            open = 1;
        else if (segment->kind == PDF_CFF_CLOSE || segment->kind == PDF_CFF_END)
            open = 0;
    }
    return !open;
}
