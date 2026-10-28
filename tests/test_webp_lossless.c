#include "cupidimage.h"
#include "formats/webp/cupidimage_webp_lossless.h"
#include <string.h>

static unsigned char stream[2048];
static size_t bitpos;
static int checks, failures;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void bits(unsigned value, unsigned count) {
    for (unsigned i = 0; i < count; i++, bitpos++)
        stream[bitpos / 8] |= ((value >> i) & 1u) << (bitpos % 8);
}

static void single(unsigned symbol) {
    bits(1, 1);
    bits(0, 1);
    bits(symbol > 1, 1);
    bits(symbol, symbol > 1 ? 8 : 1);
}

static size_t fixture(unsigned width, unsigned height, int mixed, int duplicate) {
    memset(stream, 0, sizeof(stream));
    bitpos = 0;
    bits(0x2f, 8);
    bits(width - 1, 14);
    bits(height - 1, 14);
    bits(0, 4);
    if (duplicate) {
        bits(1, 1);
        bits(2, 2);
        bits(1, 1);
        bits(2, 2);
    }
    bits(0, 1); /* No more transforms. */
    bits(0, 1);
    bits(0, 1); /* No color cache or meta-prefix image. */
    if (mixed == 2) {
        /* Green lengths 1,2,2; its length-code tree also has lengths 1,2,2. */
        bits(0, 1);
        bits(1, 4);
        bits(0, 3);
        bits(0, 3);
        bits(1, 3);
        bits(2, 3);
        bits(2, 3);
        bits(1, 1);
        bits(0, 3);
        bits(1, 2); /* Three explicit lengths. */
        bits(1, 2);
        bits(3, 2);
        bits(3, 2);
    } else if (mixed) {
        bits(1, 1);
        bits(1, 1);
        bits(1, 1);
        bits(7, 8);
        bits(9, 8);
    } else
        single(0);
    single(33);
    single(77);
    single(255);
    single(0);
    /* Single-symbol trees consume no pixel bits. */
    if (mixed)
        for (unsigned i = 0; i < width * height; i++) {
            if (mixed == 2)
                bits(i % 3 == 2 ? 3 : i % 3, i % 3 ? 2 : 1);
            else
                bits(i & 1u, 1);
        }
    return (bitpos + 7) / 8;
}

static void le32(unsigned char *out, size_t value) {
    for (unsigned i = 0; i < 4; i++)
        out[i] = (unsigned char)(value >> (i * 8));
}

static void decode(unsigned width, unsigned height, int mixed) {
    char error[128];
    size_t length = fixture(width, height, mixed, 0);
    unsigned char container[sizeof(stream) + 21];
    memcpy(container, "RIFF", 4);
    le32(container + 4, 12 + length + (length & 1));
    memcpy(container + 8, "WEBPVP8L", 8);
    le32(container + 16, length);
    memcpy(container + 20, stream, length);
    container[20 + length] = 0;
    for (int wrapped = 0; wrapped < 2; wrapped++) {
        cupidimage_image image = {0};
        int ok = wrapped ? cupidimage_load_webp(container, 20 + length + (length & 1), &image,
                                                error, sizeof(error))
                         : cupidimage_decode_vp8l(stream, length, &image, error, sizeof(error));
        CHECK(ok);
        if (image.rgba) {
            CHECK(image.width == width && image.height == height);
            for (size_t i = 0; i < (size_t)width * height; i++) {
                const unsigned char *pixel = image.rgba + i * 4;
                CHECK(pixel[0] == 33 &&
                      pixel[1] == (mixed == 2 ? i % 3
                                   : mixed    ? ((i & 1) ? 9 : 7)
                                              : 0) &&
                      pixel[2] == 77 && pixel[3] == 255);
            }
        }
        cupidimage_free(&image);
    }
    for (size_t truncated = 0; truncated < length; truncated++) {
        cupidimage_image image = {0};
        CHECK(!cupidimage_decode_vp8l(stream, truncated, &image, error, sizeof(error)));
        CHECK(image.rgba == NULL);
        cupidimage_free(&image);
    }
}

int main(void) {
    decode(1, 1, 0);
    decode(2, 1, 0);
    decode(3, 2, 0);
    decode(16, 16, 0);
    decode(1, 1, 1);
    decode(3, 2, 1);
    decode(16, 16, 1);
    for (unsigned width = 1; width <= 12; width++)
        decode(width, 1, 2);
    char error[128];
    cupidimage_image image = {0};
    size_t length = fixture(1, 1, 0, 1);
    CHECK(!cupidimage_decode_vp8l(stream, length, &image, error, sizeof(error)));
    cupidimage_free(&image);
    printf("WebP lossless: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
