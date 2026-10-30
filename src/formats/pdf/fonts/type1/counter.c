#include "counter.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int type1_counter_parse(const pdf_type1_counter *counter, pdf_type1_hint_trace *trace,
                               unsigned segment) {
    unsigned left = counter->count;
    for (unsigned axis = 0; axis < 2; axis++) {
        if (!left)
            return 0;
        double value = counter->values[--left];
        if (value < 0 || value > 65536 || floor(value) != value)
            return 0;
        unsigned groups = (unsigned)value;
        for (unsigned group = 0; group < groups; group++) {
            double width;
            do {
                if (left < 2)
                    return 0;
                double delta = counter->values[--left];
                width = counter->values[--left];
                pdf_type1_hint_event event = {0};
                event.kind = PDF_T1_HINT_COUNTER;
                event.segment = segment;
                event.vertical = axis;
                /* Keep the specified delta/width encoding and group priority. */
                event.values[0] = delta;
                event.values[1] = width;
                event.values[2] = group;
                event.values[3] = width < 0;
                if (!cupidimage_pdf_type1_trace_append(trace, &event))
                    return 0;
            } while (width >= 0);
        }
    }
    return !left;
}

int cupidimage_pdf_type1_counter_push(pdf_type1_counter *counter, const double *values,
                                      unsigned count, int last, pdf_type1_hint_trace *trace,
                                      unsigned segment, size_t *work) {
    if (!counter || !work || counter->finished || count > 22 || (count && !values) ||
        counter->count > 65536 - count || (last != 0 && last != 1))
        return 0;
    unsigned total = counter->count + count;
    size_t cost = count + (last ? total : 0);
    if (total > counter->capacity)
        cost += counter->count;
    if (*work < cost) {
        *work = 0;
        return 0;
    }
    *work -= cost;
    for (unsigned i = 0; i < count; i++)
        if (!isfinite(values[i]) || fabs(values[i]) > 1e30)
            return 0;
    if (total > counter->capacity) {
        unsigned capacity = counter->capacity ? counter->capacity * 2 : 32;
        if (capacity < total)
            capacity = total;
        double *next = realloc(counter->values, (size_t)capacity * sizeof(*next));
        if (!next)
            return 0;
        counter->values = next;
        counter->capacity = capacity;
    }
    /* Each call transfers operands top first onto the PostScript pseudo-stack. */
    for (unsigned i = count; i; i--)
        counter->values[counter->count++] = values[i - 1];
    counter->active = 1;
    if (!last)
        return 1;
    if (!type1_counter_parse(counter, trace, segment))
        return 0;
    if (trace)
        trace->diagnostics |= PDF_T1_HINT_COUNTER_DATA;
    counter->active = 0;
    counter->finished = 1;
    return 1;
}

void cupidimage_pdf_type1_counter_free(pdf_type1_counter *counter) {
    if (counter) {
        free(counter->values);
        memset(counter, 0, sizeof(*counter));
    }
}
