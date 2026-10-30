#include "budget.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define JPX_THREAD_LOCAL _Thread_local
#elif defined(_MSC_VER)
#define JPX_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define JPX_THREAD_LOCAL __thread
#else
#error JPEG2000 decoding requires compiler-supported thread-local storage
#endif

struct pdf_jpx_allocation {
    struct pdf_jpx_allocation *previous, *next;
    pdf_jpx_budget *owner;
    void *raw;
    size_t size, cost;
};

static JPX_THREAD_LOCAL pdf_jpx_budget *jpx_current_budget;

void cupidimage_jpx_budget_begin(pdf_jpx_budget *budget, size_t memory, size_t *work) {
    memset(budget, 0, sizeof(*budget));
    budget->previous = jpx_current_budget;
    budget->memory_limit = memory;
    budget->calls_left = 1024u * 1024u;
    budget->work_left = work;
    jpx_current_budget = budget;
}

int cupidimage_jpx_codec_work(uint64_t amount) {
    pdf_jpx_budget *budget = jpx_current_budget;
    if (!budget || budget->failed)
        return 0;
    if (!budget->work_left || amount > *budget->work_left) {
        budget->failed = 1;
        return 0;
    }
    *budget->work_left -= (size_t)amount;
    return 1;
}

int cupidimage_jpx_codec_block_work(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t bits,
                                    uint32_t roi) {
    if (x1 < x0 || y1 < y0 || bits > 31 || roi > 31 || bits + roi > 31)
        return 0;
    uint64_t width = (uint64_t)((int64_t)x1 - x0), height = (uint64_t)((int64_t)y1 - y0);
    if (width > 1024 || height > 1024 || width * height > 4096)
        return 0;
    return cupidimage_jpx_codec_work(1 + width * height * (3u * (bits + roi) + 1u));
}

static void *jpx_allocate(size_t size) {
    pdf_jpx_budget *budget = jpx_current_budget;
    size_t overhead = sizeof(pdf_jpx_allocation) + 31;
    if (!budget || budget->failed || !size)
        return NULL;
    if (!budget->calls_left || size > SIZE_MAX - overhead || budget->live > budget->memory_limit ||
        size + overhead > budget->memory_limit - budget->live ||
        !cupidimage_jpx_codec_work(1 + size / 64)) {
        budget->failed = 1;
        return NULL;
    }
    budget->calls_left--;
    void *raw = malloc(size + overhead);
    if (!raw) {
        budget->failed = 1;
        return NULL;
    }
    uintptr_t address = ((uintptr_t)raw + sizeof(pdf_jpx_allocation) + 31) & ~(uintptr_t)31;
    pdf_jpx_allocation *allocation = (pdf_jpx_allocation *)address - 1;
    *allocation =
        (pdf_jpx_allocation){NULL, budget->allocations, budget, raw, size, size + overhead};
    if (allocation->next)
        allocation->next->previous = allocation;
    budget->allocations = allocation;
    budget->live += allocation->cost;
    if (budget->live > budget->peak)
        budget->peak = budget->live;
    return (void *)address;
}

void *cupidimage_jpx_opj_malloc(size_t size) {
    return jpx_allocate(size);
}

void *cupidimage_jpx_opj_calloc(size_t count, size_t size) {
    if (count && size > SIZE_MAX / count) {
        if (jpx_current_budget)
            jpx_current_budget->failed = 1;
        return NULL;
    }
    void *result = jpx_allocate(count * size);
    if (result)
        memset(result, 0, count * size);
    return result;
}

void cupidimage_jpx_opj_free(void *memory) {
    if (!memory)
        return;
    pdf_jpx_allocation *allocation = (pdf_jpx_allocation *)memory - 1;
    pdf_jpx_budget *owner = allocation->owner;
    if (allocation->previous)
        allocation->previous->next = allocation->next;
    else
        owner->allocations = allocation->next;
    if (allocation->next)
        allocation->next->previous = allocation->previous;
    owner->live -= allocation->cost;
    free(allocation->raw);
}

void *cupidimage_jpx_opj_realloc(void *memory, size_t size) {
    if (!memory)
        return jpx_allocate(size);
    if (!size) {
        cupidimage_jpx_opj_free(memory);
        return NULL;
    }
    pdf_jpx_allocation *old = (pdf_jpx_allocation *)memory - 1;
    if (old->owner != jpx_current_budget)
        return NULL;
    void *result = jpx_allocate(size);
    if (result) {
        memcpy(result, memory, old->size < size ? old->size : size);
        cupidimage_jpx_opj_free(memory);
    }
    return result;
}

void *cupidimage_jpx_opj_aligned_malloc(size_t size) {
    return jpx_allocate(size);
}

void *cupidimage_jpx_opj_aligned_32_malloc(size_t size) {
    return jpx_allocate(size);
}

void *cupidimage_jpx_opj_aligned_realloc(void *memory, size_t size) {
    return cupidimage_jpx_opj_realloc(memory, size);
}

void *cupidimage_jpx_opj_aligned_32_realloc(void *memory, size_t size) {
    return cupidimage_jpx_opj_realloc(memory, size);
}

void cupidimage_jpx_opj_aligned_free(void *memory) {
    cupidimage_jpx_opj_free(memory);
}

double cupidimage_jpx_opj_clock(void) {
    clock_t value = clock();
    return value == (clock_t)-1 ? 0 : (double)value / CLOCKS_PER_SEC;
}

int cupidimage_jpx_budget_end(pdf_jpx_budget *budget) {
    int clean = jpx_current_budget == budget && !budget->allocations && !budget->live;
    while (budget->allocations)
        cupidimage_jpx_opj_free(budget->allocations + 1);
    jpx_current_budget = budget->previous;
    return clean && !budget->failed;
}
