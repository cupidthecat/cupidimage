#include "trace.h"
#include <stdlib.h>
#include <string.h>

int cupidimage_pdf_type1_trace_append(pdf_type1_hint_trace *trace,
                                      const pdf_type1_hint_event *event) {
    if (!trace)
        return 1;
    if (trace->count == 65536)
        return 0;
    if (trace->count == trace->capacity) {
        unsigned capacity = trace->capacity ? trace->capacity * 2 : 16;
        pdf_type1_hint_event *next = realloc(trace->events, (size_t)capacity * sizeof(*next));
        if (!next)
            return 0;
        trace->events = next;
        trace->capacity = capacity;
    }
    trace->events[trace->count++] = *event;
    return 1;
}

void cupidimage_pdf_type1_trace_free(pdf_type1_hint_trace *trace) {
    if (trace) {
        free(trace->events);
        memset(trace, 0, sizeof(*trace));
    }
}
