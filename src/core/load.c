#include "core/internal.h"
#include "cupidimage.h"
#include <ctype.h>
#include <string.h>

static int is_svg_data(const unsigned char *data, size_t size) {
    if (!data || size < 4) {
        return 0;
    }
    size_t n = size < 4096 ? size : 4096;
    size_t i = 0;
    if (n >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        i = 3;
    }
    while (i < n && isspace((unsigned char)data[i])) {
        i++;
    }
    for (size_t j = i; j + 3 < n; j++) {
        if (data[j] == 0) {
            return 0;
        }
        if (data[j] == '<') {
            unsigned char c1 = (unsigned char)tolower(data[j + 1]);
            unsigned char c2 = (unsigned char)tolower(data[j + 2]);
            unsigned char c3 = (unsigned char)tolower(data[j + 3]);
            if (c1 == 's' && c2 == 'v' && c3 == 'g') {
                return 1;
            }
        }
    }
    return 0;
}

int cupidimage_load_image(const unsigned char *data, size_t size, cupidimage_image *out, char *err,
                          size_t errcap) {
    if (!data || !out) {
        cupidimage_set_err(err, errcap, "invalid arguments");
        return 0;
    }
    if (size >= 6 && data[0] == 0 && data[1] == 0 && (data[2] == 1 || data[2] == 2) &&
        data[3] == 0) {
        uint16_t count = (uint16_t)(data[4] | (data[5] << 8));
        if (count > 0 && 6u + (size_t)count * 16u <= size) {
            return cupidimage_load_ico(data, size, out, err, errcap);
        }
    }
    if (is_svg_data(data, size)) {
        return cupidimage_load_svg(data, size, out, err, errcap);
    }
    if (size >= 8 && data[0] == 137 && data[1] == 80 && data[2] == 78 && data[3] == 71) {
        return cupidimage_load_png(data, size, out, err, errcap);
    }
    if (size >= 6 && (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0)) {
        return cupidimage_load_gif(data, size, out, err, errcap);
    }
    if (size >= 2 && data[0] == 'B' && data[1] == 'M') {
        return cupidimage_load_bmp(data, size, out, err, errcap);
    }
    if (size >= 4 &&
        ((data[0] == 'I' && data[1] == 'I' && data[2] == 42 && data[3] == 0) ||
         (data[0] == 'I' && data[1] == 'I' && data[2] == 43 && data[3] == 0) ||
         (data[0] == 'M' && data[1] == 'M' && data[2] == 0 && (data[3] == 42 || data[3] == 43)))) {
        return cupidimage_load_tiff(data, size, out, err, errcap);
    }
    if (size >= 26) {
        const unsigned char *footer = data + size - 26;
        if (memcmp(footer + 8, "TRUEVISION-XFILE.\0", 18) == 0) {
            return cupidimage_load_tga(data, size, out, err, errcap);
        }
    }
    if (size >= 18) {
        uint8_t color_map_type = data[1];
        uint8_t type = data[2];
        uint16_t width = (uint16_t)(data[12] | (data[13] << 8));
        uint16_t height = (uint16_t)(data[14] | (data[15] << 8));
        uint8_t depth = data[16];
        int depth_ok =
            (depth == 0 || depth == 8 || depth == 15 || depth == 16 || depth == 24 || depth == 32);
        if (color_map_type <= 1 && type <= 11 && depth_ok) {
            if (type == 0 || (width > 0 && height > 0)) {
                return cupidimage_load_tga(data, size, out, err, errcap);
            }
        }
    }
    if (size >= 5 && memcmp(data, "%PDF-", 5) == 0) {
        return cupidimage_load_pdf(data, size, out, err, errcap);
    }
    if (size >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') {
        return cupidimage_load_webp(data, size, out, err, errcap);
    }
    if (size >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
        return cupidimage_load_jpeg(data, size, out, err, errcap);
    }
    cupidimage_set_err(err, errcap, "unknown image format");
    return 0;
}

int cupidimage_load_image_file(const char *path, cupidimage_image *out, char *err, size_t errcap) {
    return cupidimage_load_image_file_via_memory(path, out, err, errcap, cupidimage_load_image);
}
