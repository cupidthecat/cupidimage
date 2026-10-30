#ifndef CUPIDIMAGE_PDF_JPX_BUDGET_H
#define CUPIDIMAGE_PDF_JPX_BUDGET_H

#include <stddef.h>
#include <stdint.h>

typedef struct pdf_jpx_allocation pdf_jpx_allocation;

typedef struct pdf_jpx_budget {
    struct pdf_jpx_budget *previous;
    pdf_jpx_allocation *allocations;
    size_t memory_limit, live, peak, calls_left;
    size_t *work_left;
    int failed;
} pdf_jpx_budget;

void cupidimage_jpx_budget_begin(pdf_jpx_budget *, size_t, size_t *);
int cupidimage_jpx_budget_end(pdf_jpx_budget *);
int cupidimage_jpx_codec_work(uint64_t);
int cupidimage_jpx_codec_block_work(int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t);

#endif
