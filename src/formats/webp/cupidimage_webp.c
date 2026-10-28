#include "core/internal.h"
#include "cupidimage.h"
#include "formats/webp/cupidimage_webp_lossless.h"
#include "formats/webp/cupidimage_webp_tables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "webp_internal.h"

/* RFC 9649 section 2.7.1.2: alpha uses the lossless green channel and
   then reverses the optional byte-wise spatial filter. */
static uint8_t *decode_alpha(const uint8_t *data, size_t size, unsigned width, unsigned height,
                             char *err, size_t errcap) {
    if (!size || !width || !height || (size_t)width > SIZE_MAX / height / 4u) {
        set_err(err, errcap, "invalid ALPH dimensions or header");
        return NULL;
    }
    unsigned method = data[0] & 3u, filter = (data[0] >> 2) & 3u;
    /* Reserved bits are ignored; preprocessing is informative. */
    size_t count = (size_t)width * height;
    if (method > 1 || (method == 0 && size - 1 != count) || size > SIZE_MAX - 4) {
        set_err(err, errcap, "invalid ALPH data");
        return NULL;
    }
    uint8_t *alpha = malloc(count);
    if (!alpha) {
        set_err(err, errcap, "out of memory");
        return NULL;
    }
    if (!method)
        memcpy(alpha, data + 1, count);
    else {
        uint8_t *wrapped = malloc(size + 4);
        if (!wrapped) {
            free(alpha);
            set_err(err, errcap, "out of memory");
            return NULL;
        }
        uint32_t dimensions = (width - 1) | ((height - 1) << 14);
        wrapped[0] = 0x2f;
        for (unsigned i = 0; i < 4; i++)
            wrapped[i + 1] = (uint8_t)(dimensions >> (8 * i));
        memcpy(wrapped + 5, data + 1, size - 1);
        cupidimage_image decoded = {0};
        int ok = cupidimage_decode_vp8l(wrapped, size + 4, &decoded, err, errcap);
        free(wrapped);
        if (!ok) {
            free(alpha);
            return NULL;
        }
        for (size_t i = 0; i < count; i++)
            alpha[i] = decoded.rgba[i * 4 + 1];
        cupidimage_free(&decoded);
    }
    if (filter)
        for (unsigned y = 0; y < height; y++)
            for (unsigned x = 0; x < width; x++) {
                size_t i = (size_t)y * width + x;
                int prediction = 0;
                if (!y) {
                    if (x)
                        prediction = alpha[i - 1];
                } else if (!x)
                    prediction = alpha[i - width];
                else if (filter == 1)
                    prediction = alpha[i - 1];
                else if (filter == 2)
                    prediction = alpha[i - width];
                else
                    prediction = clamp_u8(alpha[i - 1] + alpha[i - width] - alpha[i - width - 1]);
                alpha[i] = (uint8_t)(alpha[i] + prediction);
            }
    return alpha;
}

int cupidimage_load_webp(const unsigned char *data, size_t size, cupidimage_image *out, char *err,
                         size_t errcap) {
    if (!data || !out) {
        set_err(err, errcap, "invalid arguments");
        return 0;
    }
    memset(out, 0, sizeof(*out));

    if (size < 12 || memcmp(data, "RIFF", 4) != 0 || memcmp(data + 8, "WEBP", 4) != 0) {
        set_err(err, errcap, "not a WebP");
        return 0;
    }

    const unsigned char *vp8 = NULL;
    size_t vp8_size = 0;
    const unsigned char *vp8l = NULL;
    size_t vp8l_size = 0;
    const unsigned char *alph = NULL;
    size_t alph_size = 0;
    int has_vp8x = 0;
    uint8_t vp8x_flags = 0;
    uint32_t vp8x_width = 0;
    uint32_t vp8x_height = 0;

    uint32_t declared = read_le32(data + 4);
    if (declared < 4 || (uint64_t)declared + 8 != size || (declared & 1u)) {
        set_err(err, errcap, "invalid WebP RIFF size");
        return 0;
    }
    size_t off = 12;
    while (size - off >= 8) {
        const unsigned char *chunk = data + off;
        uint32_t csize = read_le32(chunk + 4);
        off += 8;
        size_t padded = (size_t)csize + (csize & 1u);
        if (padded < csize || padded > size - off || ((csize & 1u) && data[off + csize] != 0)) {
            set_err(err, errcap, "truncated WebP");
            return 0;
        }
        if (memcmp(chunk, "VP8X", 4) == 0) {
            if (csize != 10 || has_vp8x || vp8 || vp8l || alph || off != 20) {
                set_err(err, errcap, "invalid VP8X chunk");
                return 0;
            }
            has_vp8x = 1;
            vp8x_flags = data[off];
            vp8x_width = read_le24(data + off + 4) + 1;
            vp8x_height = read_le24(data + off + 7) + 1;
            if ((uint64_t)vp8x_width * vp8x_height > UINT32_MAX) {
                set_err(err, errcap, "WebP canvas too large");
                return 0;
            }
        } else if (memcmp(chunk, "VP8 ", 4) == 0) {
            if (vp8 || vp8l) {
                set_err(err, errcap, "duplicate WebP bitstream");
                return 0;
            }
            vp8 = data + off;
            vp8_size = csize;
        } else if (memcmp(chunk, "VP8L", 4) == 0) {
            if (vp8 || vp8l || alph) {
                set_err(err, errcap, "invalid VP8L chunk ordering");
                return 0;
            }
            vp8l = data + off;
            vp8l_size = csize;
        } else if (memcmp(chunk, "ALPH", 4) == 0) {
            if (alph || vp8 || vp8l || !has_vp8x || !(vp8x_flags & 0x10)) {
                set_err(err, errcap, "invalid ALPH chunk ordering");
                return 0;
            }
            alph = data + off;
            alph_size = csize;
        }
        off += padded;
    }
    if (off != size) {
        set_err(err, errcap, "truncated WebP chunk header");
        return 0;
    }

    if (has_vp8x && (vp8x_flags & 0x02)) {
        set_err(err, errcap, "animated WebP not supported");
        return 0;
    }

    if (vp8l && vp8l_size > 0) {
        int ok = cupidimage_decode_vp8l(vp8l, vp8l_size, out, err, errcap);
        if (ok && has_vp8x && (out->width != vp8x_width || out->height != vp8x_height)) {
            cupidimage_free(out);
            set_err(err, errcap, "VP8L dimensions differ from canvas");
            return 0;
        }
        return ok;
    }

    if (!vp8 || vp8_size < 10) {
        set_err(err, errcap, "missing VP8 chunk");
        return 0;
    }

    uint16_t width = 0;
    uint16_t height = 0;
    size_t payload_off = 0;
    if (!parse_vp8_keyframe(vp8, vp8_size, &width, &height, &payload_off, NULL)) {
        set_err(err, errcap, "unsupported VP8 frame");
        return 0;
    }
    (void)payload_off;

    if (!width || !height || (has_vp8x && (width != vp8x_width || height != vp8x_height))) {
        set_err(err, errcap, "VP8 dimensions differ from canvas");
        return 0;
    }
    uint8_t *alpha = NULL;
    if (alph) {
        alpha = decode_alpha(alph, alph_size, width, height, err, errcap);
        if (!alpha)
            return 0;
    }
    int ok = cupidimage_decode_vp8(vp8, vp8_size, alpha, alpha ? (size_t)width * height : 0, out,
                                   err, errcap);
    free(alpha);
    if (!ok)
        cupidimage_free(out);
    return ok;
}

int cupidimage_load_webp_file(const char *path, cupidimage_image *out, char *err, size_t errcap) {
    return cupidimage_load_image_file_via_memory(path, out, err, errcap, cupidimage_load_webp);
}
