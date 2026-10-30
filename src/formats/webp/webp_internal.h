#ifndef CUPIDIMAGE_WEBP_INTERNAL_H
#define CUPIDIMAGE_WEBP_INTERNAL_H
#include "cupidimage.h"
#include <stdio.h>

static inline void set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg);
    }
}

static inline uint32_t read_le32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint32_t read_le24(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static inline uint8_t clamp_u8(int v) {
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return (uint8_t)v;
}

static inline int parse_vp8_keyframe(const unsigned char *data, size_t size, uint16_t *out_w,
                                     uint16_t *out_h, size_t *out_payload_off,
                                     uint32_t *out_first_part_size) {
    if (size < 10) {
        return 0;
    }
    uint32_t tag = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16);
    int frame_type = tag & 1;
    if (frame_type != 0 || ((tag >> 1) & 7u) > 3 || !(tag & 16u)) {
        return 0;
    }
    uint32_t first_part_size = (tag >> 5) & 0x7FFFFu;
    if (data[3] != 0x9D || data[4] != 0x01 || data[5] != 0x2A) {
        return 0;
    }
    uint16_t width = (uint16_t)(data[6] | ((data[7] & 0x3F) << 8));
    uint16_t height = (uint16_t)(data[8] | ((data[9] & 0x3F) << 8));
    *out_w = width;
    *out_h = height;
    *out_payload_off = 3;
    if (out_first_part_size) {
        *out_first_part_size = first_part_size;
    }
    return 1;
}

int cupidimage_decode_vp8(const unsigned char *data, size_t size, const uint8_t *alpha,
                          size_t alpha_size, cupidimage_image *out, char *err, size_t errcap);

#endif
