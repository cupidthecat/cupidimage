#include "storage.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define GROUP_BLOCK_PIXELS 16384u

int cupidimage_pdf_group_storage_init(pdf_group_storage *store, size_t pixels, unsigned channels,
                                      size_t *live_bytes, size_t limit) {
    memset(store, 0, sizeof(*store));
    if (!live_bytes || *live_bytes > limit || (channels != 3 && channels != 4))
        return 0;
    size_t count = pixels / GROUP_BLOCK_PIXELS + (pixels % GROUP_BLOCK_PIXELS != 0);
    if (count > (limit - *live_bytes) / sizeof(*store->blocks))
        return 0;
    double **blocks = count ? calloc(count, sizeof(*blocks)) : NULL;
    if (count && !blocks)
        return 0;
    store->blocks = blocks;
    store->pixels = pixels;
    store->count = count;
    store->stride = channels + 2;
    store->bytes = count * sizeof(*blocks);
    store->live_bytes = live_bytes;
    store->limit = limit;
    *live_bytes += store->bytes;
    return 1;
}

const double *cupidimage_pdf_group_storage_read(const pdf_group_storage *store, size_t pixel) {
    static const double empty[6] = {0};
    if (pixel >= store->pixels)
        return empty;
    const double *block = store->blocks[pixel / GROUP_BLOCK_PIXELS];
    return block ? block + (pixel % GROUP_BLOCK_PIXELS) * store->stride : empty;
}

double *cupidimage_pdf_group_storage_write(pdf_group_storage *store, size_t pixel) {
    if (pixel >= store->pixels || !store->live_bytes)
        return NULL;
    size_t index = pixel / GROUP_BLOCK_PIXELS;
    if (!store->blocks[index]) {
        size_t count = store->pixels - index * GROUP_BLOCK_PIXELS;
        if (count > GROUP_BLOCK_PIXELS)
            count = GROUP_BLOCK_PIXELS;
        size_t bytes = count * store->stride * sizeof(double);
        if (*store->live_bytes > store->limit || bytes > store->limit - *store->live_bytes)
            return NULL;
        double *block = calloc(count * store->stride, sizeof(*block));
        if (!block)
            return NULL;
        store->blocks[index] = block;
        store->bytes += bytes;
        *store->live_bytes += bytes;
    }
    return store->blocks[index] + (pixel % GROUP_BLOCK_PIXELS) * store->stride;
}

void cupidimage_pdf_group_storage_detach(pdf_group_storage *store) {
    if (store->live_bytes)
        *store->live_bytes -= store->bytes;
    store->live_bytes = NULL;
}

void cupidimage_pdf_group_storage_free(pdf_group_storage *store) {
    if (store->blocks)
        for (size_t i = 0; i < store->count; i++)
            free(store->blocks[i]);
    free(store->blocks);
    cupidimage_pdf_group_storage_detach(store);
    memset(store, 0, sizeof(*store));
}
