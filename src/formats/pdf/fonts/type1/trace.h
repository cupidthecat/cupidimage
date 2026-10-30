#ifndef CUPIDIMAGE_PDF_TYPE1_TRACE_H
#define CUPIDIMAGE_PDF_TYPE1_TRACE_H

enum {
    PDF_T1_HINT_STEM,
    PDF_T1_HINT_STEM3,
    PDF_T1_HINT_REPLACE,
    PDF_T1_HINT_DOTSECTION,
    PDF_T1_HINT_FLEX,
    PDF_T1_HINT_COUNTER
};

enum { PDF_T1_HINT_BAD_WIDTH = 1, PDF_T1_HINT_COUNTER_DATA = 2 };

typedef struct {
    unsigned kind, segment, vertical;
    /* Stem pairs are absolute font coordinates. Flex retains depth and endpoint.
     * Counter events retain encoded delta/width, group priority and final-pair flag. */
    double values[6];
} pdf_type1_hint_event;

typedef struct {
    pdf_type1_hint_event *events;
    unsigned count, capacity, diagnostics;
} pdf_type1_hint_trace;

int cupidimage_pdf_type1_trace_append(pdf_type1_hint_trace *trace,
                                      const pdf_type1_hint_event *event);
void cupidimage_pdf_type1_trace_free(pdf_type1_hint_trace *trace);
#endif
