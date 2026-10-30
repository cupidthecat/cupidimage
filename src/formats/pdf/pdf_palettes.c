#include "pdf_render_internal.h"
#include <stdlib.h>
#include <string.h>

struct pdf_palette {
    struct pdf_palette *next;
    size_t length;
    unsigned char data[];
};

const unsigned char *cupidimage_pdf_palette_intern(pdf_palette_store *store,
                                                   const unsigned char *data, size_t length) {
    if (!store || !length || length > 256 * PDF_COLOR_COMPONENTS)
        return NULL;
    for (pdf_palette *item = store->head; item; item = item->next)
        if (item->length == length && !memcmp(item->data, data, length))
            return item->data;
    size_t cost = sizeof(pdf_palette) + length;
    if (store->count == 256 || cost > 16u * 1024u * 1024u - store->bytes)
        return NULL;
    pdf_palette *item = malloc(cost);
    if (!item)
        return NULL;
    item->next = store->head;
    item->length = length;
    memcpy(item->data, data, length);
    store->head = item;
    store->bytes += cost;
    store->count++;
    return item->data;
}

void cupidimage_pdf_palettes_free(pdf_palette_store *store) {
    while (store->head) {
        pdf_palette *item = store->head;
        store->head = item->next;
        free(item);
    }
    store->bytes = store->count = 0;
}
