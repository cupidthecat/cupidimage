#ifndef CUPIDIMAGE_PDF_TYPE1_COUNTER_H
#define CUPIDIMAGE_PDF_TYPE1_COUNTER_H
#include "trace.h"
#include <stddef.h>

typedef struct {
    double *values;
    unsigned count, capacity;
    int active, finished;
} pdf_type1_counter;

int cupidimage_pdf_type1_counter_push(pdf_type1_counter *counter, const double *values,
                                      unsigned count, int last, pdf_type1_hint_trace *trace,
                                      unsigned segment, size_t *work);
void cupidimage_pdf_type1_counter_free(pdf_type1_counter *counter);
#endif
