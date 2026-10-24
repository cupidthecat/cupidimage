#include "cupidimage.h"

#include <stdlib.h>
#include <limits.h>

void cupidimage_free(cupidimage_image *img) {
    if (!img) {
        return;
    }
    free(img->rgba);
    img->rgba = NULL;
    img->width = 0;
    img->height = 0;
    img->hotspot_x = 0;
    img->hotspot_y = 0;
}
