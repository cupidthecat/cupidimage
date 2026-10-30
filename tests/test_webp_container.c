#include "cupidimage.h"
#include <stdint.h>
#include <string.h>

/* Owned 4x3 constant image, encoded with Pillow/libwebp. No external test
   dependency: expected RGBA was independently decoded by libwebp. */
static const uint8_t vp8[] = {
    0xd0, 0x1,  0x0,  0x9d, 0x1,  0x2a, 0x4,  0x0,  0x3,  0x0,  0x0,  0xc0, 0x12, 0x25, 0x90, 0x2,
    0x74, 0xba, 0x1,  0xf8, 0x0,  0x3,  0xb0, 0x0,  0xfe, 0xfa, 0xda, 0x3f, 0xff, 0x3d, 0x33, 0xe0,
    0x93, 0xb0, 0x7d, 0xb2, 0xff, 0xe7, 0x60, 0xd8, 0x97, 0x6b, 0xc0, 0x0,  0x0,  0x0};
static const uint8_t rgb[] = {39, 80, 119};
static unsigned checks, failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL WebP line %d: %s\n", __LINE__, #c);                              \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

typedef struct {
    uint8_t bytes[2048];
    size_t size;
} buffer;

static void le32(uint8_t *p, size_t n) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(n >> (8 * i));
}

static void chunk(buffer *b, const char *tag, const uint8_t *p, size_t n) {
    memcpy(b->bytes + b->size, tag, 4);
    le32(b->bytes + b->size + 4, n);
    b->size += 8;
    if (n)
        memcpy(b->bytes + b->size, p, n);
    b->size += n;
    if (n & 1)
        b->bytes[b->size++] = 0;
}

static buffer image(const uint8_t *alpha, size_t size, int extended) {
    buffer b = {{0}, 12};
    memcpy(b.bytes, "RIFF", 4);
    memcpy(b.bytes + 8, "WEBP", 4);
    if (extended) {
        uint8_t x[10] = {0};
        x[0] = alpha ? 0x10 : 0;
        x[4] = 3;
        x[7] = 2;
        chunk(&b, "VP8X", x, 10);
    }
    if (alpha)
        chunk(&b, "ALPH", alpha, size);
    chunk(&b, "VP8 ", vp8, sizeof(vp8));
    le32(b.bytes + 4, b.size - 8);
    return b;
}

static void decode(const buffer *b, const uint8_t *alpha, int accepted) {
    cupidimage_image out = {0};
    char error[128] = {0};
    int ok = cupidimage_load_webp(b->bytes, b->size, &out, error, sizeof(error));
    CHECK(ok == accepted);
    if (ok) {
        CHECK(out.width == 4 && out.height == 3);
        for (unsigned i = 0; i < 12; i++) {
            CHECK(out.rgba[i * 4 + 3] == (alpha ? alpha[i] : 255));
            CHECK(memcmp(out.rgba + i * 4, rgb, 3) == 0);
        }
    } else {
        CHECK(out.rgba == NULL);
        CHECK(error[0] != 0);
    }
    cupidimage_free(&out);
}

static unsigned bitpos;

static void bits(uint8_t *p, unsigned value, unsigned n) {
    for (unsigned i = 0; i < n; i++, bitpos++)
        p[bitpos / 8] |= ((value >> i) & 1u) << (bitpos % 8);
}

static void single(uint8_t *p, unsigned v) {
    bits(p, 1, 1);
    bits(p, 0, 1);
    bits(p, v > 1, 1);
    bits(p, v, v > 1 ? 8 : 1);
}

int main(void) {
    buffer b = image(NULL, 0, 0);
    decode(&b, NULL, 1);
    b = image(NULL, 0, 1);
    decode(&b, NULL, 1);
    const uint8_t expected[12] = {0, 1, 250, 128, 255, 10, 100, 200, 40, 220, 0, 255};
    for (unsigned filter = 0; filter < 4; filter++) {
        uint8_t alpha[13] = {0};
        alpha[0] = (uint8_t)(filter << 2);
        for (unsigned y = 0; y < 3; y++)
            for (unsigned x = 0; x < 4; x++) {
                unsigned i = y * 4 + x;
                int pred = 0;
                if (filter) {
                    if (!y) {
                        if (x)
                            pred = expected[i - 1];
                    } else if (!x)
                        pred = expected[i - 4];
                    else if (filter == 1)
                        pred = expected[i - 1];
                    else if (filter == 2)
                        pred = expected[i - 4];
                    else {
                        pred = expected[i - 1] + expected[i - 4] - expected[i - 5];
                        if (pred < 0)
                            pred = 0;
                        if (pred > 255)
                            pred = 255;
                    }
                }
                alpha[i + 1] = (uint8_t)(expected[i] - pred);
            }
        b = image(alpha, sizeof(alpha), 1);
        decode(&b, expected, 1);
        alpha[0] |= 0xd0; /* Informative preprocessing and ignored reserved bits. */
        b = image(alpha, sizeof(alpha), 1);
        decode(&b, expected, 1);
    }
    uint8_t alpha[32] = {1}, expected_constant[12];
    memset(expected_constant, 137, 12);
    bitpos = 8;
    bits(alpha, 0, 1);
    bits(alpha, 0, 1);
    bits(alpha, 0, 1);
    single(alpha, 137);
    single(alpha, 0);
    single(alpha, 0);
    single(alpha, 255);
    single(alpha, 0);
    size_t compressed = (bitpos + 7) / 8;
    b = image(alpha, compressed, 1);
    decode(&b, expected_constant, 1);
    b = image(alpha, compressed - 1, 1);
    decode(&b, NULL, 0);
    alpha[0] = 2;
    b = image(alpha, compressed, 1);
    decode(&b, NULL, 0);
    b = image(NULL, 0, 0);
    b.bytes[4]--;
    decode(&b, NULL, 0);
    b = image(NULL, 0, 0);
    b.size--;
    decode(&b, NULL, 0);
    b = image(NULL, 0, 1);
    b.bytes[24] = 4;
    decode(&b, NULL, 0);
    b = image(NULL, 0, 0);
    chunk(&b, "VP8 ", vp8, sizeof(vp8));
    le32(b.bytes + 4, b.size - 8);
    decode(&b, NULL, 0);
    b = image(NULL, 0, 1);
    uint8_t x[10] = {0};
    chunk(&b, "VP8X", x, 10);
    le32(b.bytes + 4, b.size - 8);
    decode(&b, NULL, 0);
    b = image(NULL, 0, 0);
    chunk(&b, "abcd", (uint8_t[]){1}, 1);
    le32(b.bytes + 4, b.size - 8);
    decode(&b, NULL, 1);
    b.bytes[b.size - 1] = 1;
    decode(&b, NULL, 0);
    b = image(NULL, 0, 0);
    b.bytes[b.size++] = 0;
    le32(b.bytes + 4, b.size - 8);
    decode(&b, NULL, 0);
    printf("WebP container/alpha: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
