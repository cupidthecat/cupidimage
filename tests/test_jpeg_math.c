#include "../src/formats/jpeg/cupidimage_jpeg.c"
#include "../src/formats/jpeg/jpeg_arithmetic.c"
#include "../src/formats/jpeg/jpeg_entropy.c"
#include "../src/formats/jpeg/jpeg_output.c"
#include <assert.h>

static void arithmetic_standard_sequence(void) {
    /* ISO/IEC 10918-1, K.4.1 and Table K.8. */
    static const uint8_t compressed[] = {0x65, 0x5b, 0x51, 0x44, 0xf7, 0x96, 0x9d, 0x51,
                                         0x78, 0x55, 0xbf, 0xff, 0x00, 0xfc, 0x51, 0x84,
                                         0xc7, 0xce, 0xf9, 0x39, 0x00, 0x28, 0x7d, 0x46,
                                         0x70, 0x8e, 0xcb, 0xc0, 0xf6, 0xff, 0xd9, 0x00};
    static const uint8_t expected[] = {0x00, 0x02, 0x00, 0x51, 0x00, 0x00, 0x00, 0xc0,
                                       0x03, 0x52, 0x87, 0x2a, 0xaa, 0xaa, 0xaa, 0xaa,
                                       0x82, 0xc0, 0x20, 0x00, 0xfc, 0xd7, 0x9e, 0xf6,
                                       0x74, 0xea, 0xab, 0xf7, 0x69, 0x7e, 0xe7, 0x4c};
    jpeg_arithmetic reader = {0};
    reader.data = compressed;
    reader.size = sizeof(compressed);
    arithmetic_start(&reader);
    uint8_t state = 0;
    for (unsigned bit = 0; bit < 256; bit++)
        assert(arithmetic_decision(&reader, &state) == ((expected[bit / 8] >> (7 - bit % 8)) & 1));
    assert(!reader.failed);
}

int main(void) {
    arithmetic_standard_sequence();
    int32_t value = 123;
    assert(cupidimage_jpeg_dequantize(1, 8, &value) && value == 8);
    assert(cupidimage_jpeg_dequantize((int32_t)JPEG_DEQUANT_LIMIT, 1, &value));
    assert(value == JPEG_DEQUANT_LIMIT);
    assert(cupidimage_jpeg_dequantize(-(int32_t)JPEG_DEQUANT_LIMIT, 1, &value));
    assert(value == -JPEG_DEQUANT_LIMIT);
    value = 123;
    assert(!cupidimage_jpeg_dequantize((int32_t)JPEG_DEQUANT_LIMIT + 1, 1, &value) && value == 123);
    assert(!cupidimage_jpeg_dequantize(INT32_MAX, INT_MAX, &value) && value == 123);
    assert(!cupidimage_jpeg_dequantize(INT32_MIN, INT_MAX, &value) && value == 123);

    const uint8_t bit = 0x80;
    for (int negative = 0; negative < 2; negative++) {
        jpeg_bitstream stream = {&bit, 1, 0, 0, 0, 0};
        value = negative ? INT32_MIN : INT32_MAX;
        assert(!refine_coeff(&stream, &value, 1));
        assert(value == (negative ? INT32_MIN : INT32_MAX));
    }
    jpeg_bitstream stream = {&bit, 1, 0, 0, 0, 0};
    value = INT32_MAX - 1;
    assert(refine_coeff(&stream, &value, 1) && value == INT32_MAX);

    int32_t block[64];
    uint8_t pixels[64];
    for (int negative = 0; negative < 2; negative++) {
        for (unsigned i = 0; i < 64; i++)
            block[i] = negative ? -(int32_t)JPEG_DEQUANT_LIMIT : (int32_t)JPEG_DEQUANT_LIMIT;
        idct_8x8(block, pixels, 8);
        assert(pixels[0] == (negative ? 0 : 255));
    }
    uint8_t dc_counts[16] = {1}, dc_symbols[1] = {0};
    uint8_t ac_counts[16] = {1, 1}, ac_symbols[2] = {0xf0, 0};
    jpeg_huff dc = {0}, ac = {0};
    assert(cupidimage_jpeg_huff_build(&dc, dc_counts, dc_symbols, 1));
    assert(cupidimage_jpeg_huff_build(&ac, ac_counts, ac_symbols, 2));
    int quantizers[64];
    for (unsigned i = 0; i < 64; i++)
        quantizers[i] = 1;
    const uint8_t valid_run = 0x0b, excess_run = 0x07;
    int predictor = 0;
    stream = (jpeg_bitstream){&valid_run, 1, 0, 0, 0, 0};
    assert(cupidimage_jpeg_decode_block(&stream, &dc, &ac, quantizers, &predictor, block));
    stream = (jpeg_bitstream){&excess_run, 1, 0, 0, 0, 0};
    assert(!cupidimage_jpeg_decode_block(&stream, &dc, &ac, quantizers, &predictor, block));
    puts("JPEG arithmetic: 13 coefficient checks and 256 standard decisions passed");
    return 0;
}
