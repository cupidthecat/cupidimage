#include "internal.h"
#include <string.h>

pdf_jb2_bitmap *cupidimage_pdf_jb2_bitmap_new(pdf_jb2_context *context, uint32_t width,
                                              uint32_t height, unsigned fill) {
    if (width > 65535 || height > 65535 || (uint64_t)width * height > PDF_JB2_PIXEL_LIMIT ||
        fill > 1) {
        cupidimage_pdf_jb2_fail(context);
        return NULL;
    }
    pdf_jb2_bitmap *bitmap = cupidimage_pdf_jb2_alloc(context, 1, sizeof(*bitmap));
    if (!bitmap)
        return NULL;
    bitmap->width = width;
    bitmap->height = height;
    bitmap->stride = ((size_t)width + 7) / 8;
    size_t bytes = bitmap->stride * height;
    if (!bytes)
        return bitmap;
    bitmap->data = cupidimage_pdf_jb2_alloc(context, bytes, 1);
    if (!bitmap->data)
        return NULL;
    if (fill) {
        memset(bitmap->data, 255, bytes);
        if (width % 8)
            for (unsigned y = 0; y < height; y++)
                bitmap->data[y * bitmap->stride + bitmap->stride - 1] &=
                    (unsigned char)(255u << (8 - width % 8));
    }
    return bitmap;
}

unsigned cupidimage_pdf_jb2_pixel(const pdf_jb2_bitmap *bitmap, int64_t x, int64_t y) {
    if (!bitmap || !bitmap->data || x < 0 || y < 0 || (uint64_t)x >= bitmap->width ||
        (uint64_t)y >= bitmap->height)
        return 0;
    return (bitmap->data[(size_t)y * bitmap->stride + (size_t)x / 8] >> (7 - x % 8)) & 1u;
}

void cupidimage_pdf_jb2_pixel_set(pdf_jb2_bitmap *bitmap, uint32_t x, uint32_t y, unsigned value) {
    if (!bitmap || !bitmap->data || x >= bitmap->width || y >= bitmap->height)
        return;
    unsigned char mask = (unsigned char)(128u >> (x % 8));
    unsigned char *pixel = bitmap->data + (size_t)y * bitmap->stride + x / 8;
    *pixel = value ? (unsigned char)(*pixel | mask) : (unsigned char)(*pixel & ~mask);
}

int cupidimage_pdf_jb2_blit(pdf_jb2_context *context, pdf_jb2_bitmap *destination,
                            const pdf_jb2_bitmap *source, int64_t x, int64_t y,
                            unsigned operation) {
    if (!destination || !source || operation > 4 || !cupidimage_pdf_jb2_take(context, 1))
        return cupidimage_pdf_jb2_fail(context);
    if (x >= destination->width || y >= destination->height || x <= -(int64_t)source->width ||
        y <= -(int64_t)source->height)
        return 1;
    unsigned sx = x < 0 ? (unsigned)-x : 0, sy = y < 0 ? (unsigned)-y : 0;
    unsigned dx = x < 0 ? 0 : (unsigned)x, dy = y < 0 ? 0 : (unsigned)y;
    unsigned width = source->width - sx, height = source->height - sy;
    if (width > destination->width - dx)
        width = destination->width - dx;
    if (height > destination->height - dy)
        height = destination->height - dy;
    if (!cupidimage_pdf_jb2_take(context, (uint64_t)width * height * 3))
        return 0;
    for (unsigned row = 0; row < height; row++)
        for (unsigned col = 0; col < width; col++) {
            unsigned r = source == destination && y > 0 ? height - row - 1 : row;
            unsigned c = source == destination && x > 0 ? width - col - 1 : col;
            unsigned a = cupidimage_pdf_jb2_pixel(destination, dx + c, dy + r);
            unsigned b = cupidimage_pdf_jb2_pixel(source, sx + c, sy + r);
            unsigned value = operation == 0   ? a | b
                             : operation == 1 ? a & b
                             : operation == 2 ? a ^ b
                             : operation == 3 ? !(a ^ b)
                                              : b;
            cupidimage_pdf_jb2_pixel_set(destination, dx + c, dy + r, value);
        }
    return 1;
}
