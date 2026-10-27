#include "cmap_predefined.h"
#include <string.h>
#include "cmap_data.inc"

const pdf_predefined_cmap *cupidimage_pdf_predefined_cmap(const char *name) {
    size_t lo = 0, hi = sizeof(cmap_resources) / sizeof(*cmap_resources);
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int order = strcmp(name, cmap_resources[mid].name);
        if (!order)
            return cmap_resources + mid;
        if (order < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}
