#include "../src/formats/jpeg/cupidimage_jpeg.c"
#include "../src/formats/jpeg/jpeg_entropy.c"
#include "../src/formats/jpeg/jpeg_output.c"
#include <assert.h>

int main(void) {
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
    puts("JPEG coefficient arithmetic: 13 checks passed");
    return 0;
}
