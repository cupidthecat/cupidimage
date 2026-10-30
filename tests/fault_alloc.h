#ifndef TEST_FAULT_ALLOC_H
#define TEST_FAULT_ALLOC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_count, fail_at, live_count;
static void *live[4096];
static size_t sizes[4096];

static void *fault_malloc(size_t size) {
    if (++allocation_count == fail_at)
        return NULL;
    void *p = malloc(size);
    if (p) {
        if (live_count == 4096)
            abort();
        live[live_count] = p;
        sizes[live_count++] = size;
    }
    return p;
}

static void fault_free(void *p) {
    if (!p)
        return;
    for (size_t i = 0; i < live_count; i++) {
        if (live[i] == p) {
            live[i] = live[--live_count];
            sizes[i] = sizes[live_count];
            free(p);
            return;
        }
    }
    fprintf(stderr, "freed an unowned allocation\n");
    abort();
}

static void *fault_realloc(void *p, size_t size) {
    void *next = fault_malloc(size);
    if (!next)
        return NULL;
    if (p) {
        size_t i = 0;
        while (i < live_count && live[i] != p)
            i++;
        if (i == live_count)
            abort();
        memcpy(next, p, sizes[i] < size ? sizes[i] : size);
        fault_free(p);
    }
    return next;
}

static void *fault_calloc(size_t count, size_t size) {
    if (size && count > (size_t)-1 / size)
        return NULL;
    void *p = fault_malloc(count * size);
    if (p)
        memset(p, 0, count * size);
    return p;
}

#endif
