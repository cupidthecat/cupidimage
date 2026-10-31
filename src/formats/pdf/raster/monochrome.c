#include "monochrome.h"
#include "monochrome_engine.h"
#include <stdlib.h>

#ifndef FTIMAGE_H_
#define FT_BEGIN_HEADER
#define FT_END_HEADER
#define FT_STATIC_BYTE_CAST(type, value) ((type)(unsigned char)(value))
#include "third_party/freetype-raster/ftimage.h"
#endif

int cupidimage_pdf_monochrome(const pdf_coverage_point *points, unsigned count, unsigned width,
                              unsigned height, unsigned scan_mode, int high_precision,
                              size_t *work_left, unsigned char **output) {
    if (!output)
        return 0;
    *output = NULL;
    if (!work_left || (count && !points) || count > 65535 || width > 32767 || height > 32767 ||
        scan_mode > 7 || (high_precision != 0 && high_precision != 1))
        return 0;
    size_t pixels = (size_t)width * height;
    if (pixels > 16u * 1024u * 1024u || pixels > *work_left || count > *work_left - pixels)
        return 0;
    unsigned contours = 0;
    for (unsigned i = 0; i < count; i++) {
        unsigned tag = points[i].tag;
        if (points[i].x < -2097152 || points[i].x > 2097152 || points[i].y < -2097152 ||
            points[i].y > 2097152 || (tag & ~0xe7u) || (tag & 3) > 2 ||
            (!(tag & 4) && (tag & 0xe0)) || ((tag & 4) && i && !points[i - 1].end) ||
            points[i].end > 1)
            return 0;
        contours += points[i].end;
    }
    if (count && !points[count - 1].end)
        return 0;
    *work_left -= pixels + count;
    if (!pixels)
        return 1;
    unsigned char *coverage = calloc(pixels, 1), *bitmap = NULL;
    long *pool = NULL;
    FT_Outline outline = {0};
    int ok = coverage != NULL;
    if (!ok || !count)
        goto done;
    size_t stride = (width + 7) / 8;
    bitmap = calloc(stride * height, 1);
    outline.points = malloc(((size_t)count + 1) * sizeof(*outline.points));
    outline.tags = malloc((size_t)count + 1);
    outline.contours = malloc((size_t)contours * sizeof(*outline.contours));
    size_t pool_bytes = (size_t)count * 128 + ((size_t)width + height) * 64 + 16384;
    size_t pool_words = pool_bytes / sizeof(*pool);
    pool = malloc(pool_words * sizeof(*pool));
    ok = bitmap && outline.points && outline.tags && outline.contours && pool;
    if (!ok)
        goto done;
    /* The upstream walker temporarily forms a pointer before its first point. */
    for (unsigned i = 0, contour = 0; i < count; i++) {
        outline.points[i + 1] = (FT_Vector){points[i].x, points[i].y};
        outline.tags[i + 1] = (unsigned char)points[i].tag;
        if (points[i].end)
            outline.contours[contour++] = (unsigned short)i;
    }
    outline.n_points = (unsigned short)count;
    outline.n_contours = (unsigned short)contours;
    outline.flags = high_precision ? FT_OUTLINE_HIGH_PRECISION : 0;
    if (scan_mode & 2)
        outline.flags |= FT_OUTLINE_IGNORE_DROPOUTS;
    if (scan_mode & 4)
        outline.flags |= FT_OUTLINE_SMART_DROPOUTS;
    if (!(scan_mode & 1))
        outline.flags |= FT_OUTLINE_INCLUDE_STUBS;
    outline.points++;
    outline.tags++;
    ok = cupidimage_pdf_monochrome_engine(&outline, bitmap, width, height, pool, pool_words,
                                          work_left);
    outline.points--;
    outline.tags--;
    if (ok) {
        for (unsigned y = 0; y < height; y++)
            for (unsigned x = 0; x < width; x++)
                coverage[(size_t)y * width + x] =
                    bitmap[(size_t)(height - 1 - y) * stride + x / 8] & (0x80u >> (x & 7)) ? 255
                                                                                           : 0;
    }
done:
    free(pool);
    free(outline.contours);
    free(outline.tags);
    free(outline.points);
    free(bitmap);
    if (ok)
        *output = coverage;
    else
        free(coverage);
    return ok;
}
