#include "cupidimage.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 9u
#define HEIGHT 9u

typedef struct {
    uint8_t data[8192];
    size_t size;
} buffer;

typedef struct {
    uint32_t width, height;
    uint8_t type, depth, interlace;
    uint16_t samples[WIDTH * HEIGHT * 4];
    uint8_t palette[256 * 3];
    unsigned palette_entries;
    const uint8_t *trns;
    size_t trns_size;
    const uint8_t *idat;
    size_t idat_size;
    uint8_t expected[WIDTH * HEIGHT * 4];
} png_case;

static unsigned checks, failures;

static void append(buffer *buf, const void *data, size_t size) {
    assert(size <= sizeof(buf->data) - buf->size);
    if (size)
        memcpy(buf->data + buf->size, data, size);
    buf->size += size;
}

static void put_be32(buffer *buf, uint32_t value) {
    uint8_t bytes[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8),
                        (uint8_t)value};
    append(buf, bytes, sizeof(bytes));
}

static void chunk(buffer *png, const char *type, const void *data, size_t size) {
    put_be32(png, (uint32_t)size);
    size_t start = png->size;
    append(png, type, 4);
    append(png, data, size);
    uint32_t crc = UINT32_C(0xffffffff);
    for (size_t i = start; i < png->size; i++) {
        crc ^= png->data[i];
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
        }
    }
    put_be32(png, ~crc);
}

static unsigned channels(uint8_t type) {
    switch (type) {
        case 0:
        case 3:
            return 1;
        case 2:
            return 3;
        case 4:
            return 2;
        case 6:
            return 4;
        default:
            assert(0);
            return 0;
    }
}

static buffer scanlines(const png_case *test) {
    static const unsigned xs[7] = {0, 4, 0, 2, 0, 1, 0};
    static const unsigned ys[7] = {0, 0, 4, 0, 2, 0, 1};
    static const unsigned dx[7] = {8, 8, 4, 4, 2, 2, 1};
    static const unsigned dy[7] = {8, 8, 8, 4, 4, 2, 2};
    buffer raw = {{0}, 0};
    unsigned count = channels(test->type);
    for (unsigned pass = 0; pass < (test->interlace ? 7u : 1u); pass++) {
        unsigned x0 = test->interlace ? xs[pass] : 0;
        unsigned y0 = test->interlace ? ys[pass] : 0;
        unsigned xstep = test->interlace ? dx[pass] : 1;
        unsigned ystep = test->interlace ? dy[pass] : 1;
        if (x0 >= test->width || y0 >= test->height)
            continue;
        for (unsigned y = y0; y < test->height; y += ystep) {
            uint8_t row[WIDTH * 8] = {0};
            unsigned bit = 0;
            for (unsigned x = x0; x < test->width; x += xstep) {
                for (unsigned c = 0; c < count; c++) {
                    uint16_t value = test->samples[(y * test->width + x) * count + c];
                    for (unsigned b = test->depth; b > 0; b--, bit++) {
                        row[bit / 8] |= ((value >> (b - 1)) & 1u) << (7 - bit % 8);
                    }
                }
            }
            const uint8_t filter = 0;
            append(&raw, &filter, 1);
            append(&raw, row, (bit + 7) / 8);
        }
    }
    return raw;
}

static buffer stored_zlib(const buffer *raw) {
    buffer compressed = {{0}, 0};
    unsigned length = (unsigned)raw->size;
    uint8_t header[] = {0x78,
                        0x01,
                        0x01,
                        (uint8_t)length,
                        (uint8_t)(length >> 8),
                        (uint8_t)~length,
                        (uint8_t)(~length >> 8)};
    append(&compressed, header, sizeof(header));
    append(&compressed, raw->data, raw->size);
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw->size; i++) {
        a = (a + raw->data[i]) % 65521;
        b = (b + a) % 65521;
    }
    put_be32(&compressed, (b << 16) | a);
    return compressed;
}

static buffer make_png(const png_case *test) {
    static const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
    buffer png = {{0}, 0}, ihdr = {{0}, 0};
    append(&png, signature, sizeof(signature));
    put_be32(&ihdr, test->width);
    put_be32(&ihdr, test->height);
    uint8_t format[] = {test->depth, test->type, 0, 0, test->interlace};
    append(&ihdr, format, sizeof(format));
    chunk(&png, "IHDR", ihdr.data, ihdr.size);
    if (test->palette_entries) {
        chunk(&png, "PLTE", test->palette, test->palette_entries * 3);
    }
    if (test->trns)
        chunk(&png, "tRNS", test->trns, test->trns_size);
    buffer raw = scanlines(test);
    buffer compressed = stored_zlib(&raw);
    const uint8_t *idat = test->idat ? test->idat : compressed.data;
    size_t size = test->idat ? test->idat_size : compressed.size;
    /* IDAT boundaries need not coincide with DEFLATE blocks. */
    chunk(&png, "IDAT", idat, size / 2);
    chunk(&png, "IDAT", idat + size / 2, size - size / 2);
    chunk(&png, "IEND", NULL, 0);
    return png;
}

static void check(const char *name, const png_case *test, int expected_ok) {
    buffer png = make_png(test);
    cupidimage_image image = {0};
    char err[128] = {0};
    int ok = cupidimage_load_png(png.data, png.size, &image, err, sizeof(err));
    checks++;
    if (!!ok != !!expected_ok) {
        fprintf(stderr, "FAIL %s (type=%u depth=%u Adam7=%u): %s\n", name, test->type, test->depth,
                test->interlace, ok ? "accepted invalid PNG" : err);
        failures++;
    } else if (ok && (image.width != test->width || image.height != test->height ||
                      memcmp(image.rgba, test->expected, test->width * test->height * 4))) {
        fprintf(stderr, "FAIL %s (type=%u depth=%u Adam7=%u): wrong RGBA pixels\n", name,
                test->type, test->depth, test->interlace);
        failures++;
    } else if (!ok && (image.rgba || image.width || image.height || !err[0])) {
        fprintf(stderr, "FAIL %s: invalid output after decode failure\n", name);
        failures++;
    }
    cupidimage_free(&image);
}

static png_case new_case(uint8_t type, uint8_t depth, uint8_t interlace) {
    png_case test = {0};
    test.width = WIDTH;
    test.height = HEIGHT;
    test.type = type;
    test.depth = depth;
    test.interlace = interlace;
    return test;
}

static uint8_t scaled(unsigned sample, uint8_t depth) {
    unsigned max = (1u << depth) - 1;
    return (uint8_t)((sample * 255u + max / 2) / max);
}

static void test_palette(uint8_t interlace) {
    static const unsigned entries[] = {1, 2, 4, 16, 255, 256};
    uint8_t alpha[256];
    for (unsigned i = 0; i < 256; i++)
        alpha[i] = (uint8_t)i;
    for (unsigned e = 0; e < sizeof(entries) / sizeof(entries[0]); e++) {
        for (unsigned transparency = 0; transparency < 2; transparency++) {
            png_case test = new_case(3, 8, interlace);
            test.palette_entries = entries[e];
            for (unsigned i = 0; i < entries[e]; i++) {
                test.palette[i * 3] = (uint8_t)i;
                test.palette[i * 3 + 1] = (uint8_t)(255 - i);
                test.palette[i * 3 + 2] = (uint8_t)(i ^ 0x55);
            }
            if (transparency) {
                test.trns = alpha;
                /* Unlisted palette entries remain opaque. */
                test.trns_size = entries[e] > 1 ? entries[e] - 1 : 1;
            }
            for (unsigned p = 0; p < WIDTH * HEIGHT; p++) {
                unsigned index = p % 2 ? entries[e] - 1 : 0;
                test.samples[p] = (uint16_t)index;
                memcpy(test.expected + p * 4, test.palette + index * 3, 3);
                test.expected[p * 4 + 3] =
                    transparency && index < test.trns_size ? alpha[index] : 255;
            }
            check("palette endpoints", &test, 1);
            if (entries[e] == 2 || entries[e] == 4 || entries[e] == 16) {
                test.depth = entries[e] == 2 ? 1 : entries[e] == 4 ? 2 : 4;
                check("packed palette", &test, 1);
            }
            if (transparency) {
                test.trns_size = entries[e];
                for (unsigned p = 0; p < WIDTH * HEIGHT; p++) {
                    test.expected[p * 4 + 3] = alpha[test.samples[p]];
                }
                check("complete palette alpha table", &test, 1);
            }
            if (entries[e] < 256) {
                test.depth = 8;
                test.samples[0] = (uint16_t)entries[e];
                check("out-of-range palette index", &test, 0);
            }
        }
    }
}

static void test_gray(uint8_t depth, uint8_t interlace) {
    png_case test = new_case(0, depth, interlace);
    unsigned max = (1u << depth) - 1;
    unsigned key = depth == 16 ? 0x1234 : max / 2;
    uint8_t trns[] = {(uint8_t)(key >> 8), (uint8_t)key, 0};
    unsigned values[] = {key, key + 1, 0, max};
    for (unsigned p = 0; p < WIDTH * HEIGHT; p++) {
        unsigned value = values[p % 4];
        test.samples[p] = (uint16_t)value;
        memset(test.expected + p * 4, scaled(value, depth), 3);
        test.expected[p * 4 + 3] = 255;
    }
    check("opaque grayscale", &test, 1);
    test.trns = trns;
    test.trns_size = 2;
    for (unsigned p = 0; p < WIDTH * HEIGHT; p++) {
        test.expected[p * 4 + 3] = values[p % 4] == key ? 0 : 255;
    }
    check("grayscale transparency key", &test, 1);
    if (depth < 16) {
        trns[0] |= 0x80;
        check("grayscale tRNS unused bits", &test, 1);
    }
    test.trns_size = 1;
    check("short grayscale tRNS", &test, 0);
    test.trns_size = 3;
    check("long grayscale tRNS", &test, 0);
    test.trns_size = 0;
    check("empty grayscale tRNS", &test, 0);
}

static void test_rgb(uint8_t depth, uint8_t interlace) {
    png_case test = new_case(2, depth, interlace);
    unsigned key[] = {0x1234, 0x5678, 0x9abc};
    if (depth == 8) {
        key[0] = 17;
        key[1] = 33;
        key[2] = 65;
    }
    uint8_t trns[7] = {0};
    for (unsigned c = 0; c < 3; c++) {
        trns[c * 2] = (uint8_t)(key[c] >> 8);
        trns[c * 2 + 1] = (uint8_t)key[c];
    }
    for (unsigned p = 0; p < WIDTH * HEIGHT; p++) {
        for (unsigned c = 0; c < 3; c++) {
            unsigned value = key[c] + (p % 4 == c + 1);
            test.samples[p * 3 + c] = (uint16_t)value;
            test.expected[p * 4 + c] = scaled(value, depth);
        }
        test.expected[p * 4 + 3] = 255;
    }
    check("opaque RGB", &test, 1);
    test.trns = trns;
    test.trns_size = 6;
    for (unsigned p = 0; p < WIDTH * HEIGHT; p++)
        test.expected[p * 4 + 3] = p % 4 ? 255 : 0;
    check("RGB transparency key", &test, 1);
    if (depth == 8) {
        trns[0] = trns[2] = trns[4] = 0x80;
        check("RGB tRNS unused bits", &test, 1);
    }
    test.trns_size = 5;
    check("short RGB tRNS", &test, 0);
    test.trns_size = 7;
    check("long RGB tRNS", &test, 0);
}

static void test_alpha(uint8_t type, uint8_t depth, uint8_t interlace) {
    png_case test = new_case(type, depth, interlace);
    unsigned count = channels(type);
    unsigned values[] = {0, ((1u << depth) - 1) / 2, (1u << depth) - 1};
    for (unsigned p = 0; p < WIDTH * HEIGHT; p++) {
        for (unsigned c = 0; c < count; c++) {
            unsigned value = values[(p + c) % 3];
            test.samples[p * count + c] = (uint16_t)value;
            if (type == 4 && c == 0)
                memset(test.expected + p * 4, scaled(value, depth), 3);
            else
                test.expected[p * 4 + (c == count - 1 ? 3 : c)] = scaled(value, depth);
        }
    }
    check("explicit alpha", &test, 1);
    const uint8_t trns[] = {0, 0, 0, 0, 0, 0};
    test.trns = trns;
    test.trns_size = type == 4 ? 2 : 6;
    check("tRNS forbidden with alpha", &test, 0);
}

static void test_mixed_blocks(void) {
    /* A complete dynamic tree with a one-bit EOB and 15-bit unused codes,
       followed by a final stored block. Inflates to filter 0, pixels 1..9. */
    static const uint8_t idat[] = {0x78, 0x01, 0x04, 0xe0, 0x81, 0x92, 0x24, 0x49, 0x92, 0x24, 0xc9,
                                   0x12, 0x8b, 0x9a, 0x47, 0x56, 0xcf, 0xde, 0x7b, 0xff, 0xff, 0xb9,
                                   0x88, 0x09, 0x09, 0x00, 0xf6, 0xff, 0x01, 0x02, 0x03, 0x04, 0x05,
                                   0x06, 0x07, 0x08, 0x09, 0x00, 0xaf, 0x00, 0x2e};
    png_case test = new_case(0, 8, 0);
    test.height = 1;
    test.idat = idat;
    test.idat_size = sizeof(idat);
    for (unsigned p = 0; p < WIDTH; p++) {
        memset(test.expected + p * 4, (int)p + 1, 3);
        test.expected[p * 4 + 3] = 255;
    }
    check("dynamic Huffman to stored block", &test, 1);
}

int main(void) {
    static const uint8_t depths[] = {1, 2, 4, 8, 16};
    for (uint8_t interlace = 0; interlace < 2; interlace++) {
        test_palette(interlace);
        for (unsigned d = 0; d < sizeof(depths); d++)
            test_gray(depths[d], interlace);
        for (uint8_t depth = 8; depth <= 16; depth += 8) {
            test_rgb(depth, interlace);
            test_alpha(4, depth, interlace);
            test_alpha(6, depth, interlace);
        }
        png_case test = new_case(3, 8, interlace);
        test.palette_entries = 1;
        const uint8_t alpha[] = {0, 255};
        test.trns = alpha;
        test.trns_size = 2;
        check("palette tRNS exceeds PLTE", &test, 0);
        test.trns_size = 0;
        for (unsigned p = 0; p < WIDTH * HEIGHT; p++)
            test.expected[p * 4 + 3] = 255;
        check("empty palette tRNS", &test, 1);
    }
    test_mixed_blocks();
    printf("PNG regressions: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
