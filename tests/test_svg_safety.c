#include "cupidimage.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static uint32_t hash(uint32_t x, uint32_t y, uint32_t seed) {
    uint32_t h = (x * 374761393u + y * 668265263u) ^ seed;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void noise(void) {
    const char *svg =
        "<svg width='16' height='16'><defs>"
        "<filter id='n' filterUnits='userSpaceOnUse' x='0' y='0' width='16' height='16'>"
        "<feTurbulence baseFrequency='1' numOctaves='8' seed='2147483520'/></filter>"
        "</defs><rect width='16' height='16' filter='url(#n)'/></svg>";
    cupidimage_image image = {0};
    cupidimage_svg_options opts = {0};
    opts.scale = 1;
    opts.dpi = 96;
    opts.supersampling = 1;
    char error[128];
    CHECK(cupidimage_load_svg_with_options((const unsigned char *)svg, strlen(svg), &image, &opts,
                                           error, sizeof(error)));
    if (!image.rgba)
        return;
    CHECK(image.width == 16 && image.height == 16);
    for (uint32_t y = 0; y < 16; y++) {
        for (uint32_t x = 0; x < 16; x++) {
            float sum = 0, amplitude = 1, total = 0;
            for (uint32_t octave = 0; octave < 8; octave++) {
                float value =
                    (float)(hash(x << octave, y << octave, 2147483520u + octave * 17u) & 65535u) /
                    65535.0f;
                sum += fabsf(value * 2.0f - 1.0f) * amplitude;
                total += amplitude;
                amplitude *= 0.5f;
            }
            unsigned expected = (unsigned)lroundf(sum / total * 255.0f);
            const uint8_t *pixel = image.rgba + ((size_t)y * 16 + x) * 4;
            CHECK(abs((int)pixel[0] - (int)expected) <= 1 && pixel[0] == pixel[1] &&
                  pixel[0] == pixel[2] && pixel[3] == 255);
        }
    }
    cupidimage_free(&image);
}

static void text_paths(void) {
    const char *parts[] = {
        "<svg width='96' height='32'><defs><path id='p' d='M0 24 L96 24'/></defs>"
        "<text font-size='16' fill='green'><textPath href='#p'>A</textPath>"
        "<tspan x='40' y='24' fill='red'>B</tspan></text></svg>",
        "<svg width='96' height='32'><defs><path id='p' d='M0 24 L96 24'/></defs>"
        "<text font-size='16'><textPath href='#p'>A</text></svg>"};
    for (size_t i = 0; i < 2; i++) {
        cupidimage_image image = {0};
        char error[128];
        CHECK(cupidimage_load_svg((const unsigned char *)parts[i], strlen(parts[i]), &image, error,
                                  sizeof(error)));
        if (image.rgba && i == 0) {
            unsigned red = 0, green = 0;
            for (size_t p = 0; p < (size_t)image.width * image.height; p++) {
                const uint8_t *pixel = image.rgba + p * 4;
                red += pixel[0] > 128 && pixel[1] < 32 && pixel[3] > 128;
                green += pixel[1] > 64 && pixel[0] < 32 && pixel[3] > 128;
            }
            CHECK(red > 0 && green > 0);
        }
        cupidimage_free(&image);
    }
}

int main(void) {
    noise();
    text_paths();
    printf("SVG safety: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
