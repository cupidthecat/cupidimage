#include "internal.h"
#include <stdlib.h>
#include <string.h>

struct pdf_jb2_allocation {
    struct pdf_jb2_allocation *next;
    void *data;
    size_t size;
};

int cupidimage_pdf_jb2_context_init(pdf_jb2_context *context, size_t *work, size_t limit) {
    if (!context)
        return 0;
    memset(context, 0, sizeof(*context));
    context->work = work;
    context->limit = limit < PDF_JB2_MEMORY_LIMIT ? limit : PDF_JB2_MEMORY_LIMIT;
    return work ? 1 : cupidimage_pdf_jb2_fail(context);
}

int cupidimage_pdf_jb2_fail(pdf_jb2_context *context) {
    if (context)
        context->failed = 1;
    return 0;
}

int cupidimage_pdf_jb2_take(pdf_jb2_context *context, uint64_t amount) {
    if (!context || context->failed || !context->work)
        return 0;
    if (amount > *context->work) {
        *context->work = 0;
        return cupidimage_pdf_jb2_fail(context);
    }
    *context->work -= (size_t)amount;
    return 1;
}

void *cupidimage_pdf_jb2_alloc(pdf_jb2_context *context, size_t count, size_t size) {
    if (!context || context->failed || (count && size > SIZE_MAX / count)) {
        cupidimage_pdf_jb2_fail(context);
        return NULL;
    }
    size *= count;
    if (!size)
        size = 1;
    size_t overhead = sizeof(pdf_jb2_allocation);
    if (size > context->limit || overhead > context->limit - size ||
        context->live > context->limit - size - overhead ||
        !cupidimage_pdf_jb2_take(context, size / 8 + 1)) {
        cupidimage_pdf_jb2_fail(context);
        return NULL;
    }
    void *data = calloc(1, size);
    pdf_jb2_allocation *entry = data ? malloc(sizeof(*entry)) : NULL;
    if (!entry) {
        free(data);
        cupidimage_pdf_jb2_fail(context);
        return NULL;
    }
    *entry = (pdf_jb2_allocation){context->allocations, data, size};
    context->allocations = entry;
    context->live += size + overhead;
    if (context->live > context->peak)
        context->peak = context->live;
    return data;
}

void *cupidimage_pdf_jb2_detach(pdf_jb2_context *context, void *data) {
    if (!context || !data)
        return NULL;
    pdf_jb2_allocation **link = &context->allocations;
    while (*link && (*link)->data != data) {
        if (!cupidimage_pdf_jb2_take(context, 1))
            return NULL;
        link = &(*link)->next;
    }
    if (!*link) {
        cupidimage_pdf_jb2_fail(context);
        return NULL;
    }
    pdf_jb2_allocation *entry = *link;
    *link = entry->next;
    context->live -= entry->size + sizeof(*entry);
    free(entry);
    return data;
}

int cupidimage_pdf_jb2_release(pdf_jb2_context *context, void *data) {
    if (!data)
        return 1;
    void *detached = cupidimage_pdf_jb2_detach(context, data);
    if (!detached)
        return 0;
    free(detached);
    return 1;
}

void cupidimage_pdf_jb2_clear(pdf_jb2_context *context) {
    if (!context)
        return;
    pdf_jb2_allocation *entry = context->allocations;
    while (entry) {
        pdf_jb2_allocation *next = entry->next;
        free(entry->data);
        free(entry);
        entry = next;
    }
    memset(context, 0, sizeof(*context));
}
