#include "cupidimage.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int test_svg(void) {
    static const char *begins[] = {"bad;1s", "1s;2s", "indefinite;1s", "bad", "0s"};
    for (unsigned i = 0; i < sizeof(begins) / sizeof(begins[0]); i++) {
        char svg[512], err[128] = {0};
        int size = snprintf(
            svg, sizeof(svg),
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8\" height=\"8\">"
            "<rect width=\"4\" height=\"4\"><animate attributeName=\"x\" from=\"0\" to=\"4\" "
            "begin=\"%s\" dur=\"1s\"/></rect></svg>",
            begins[i]);
        cupidimage_svg_options options = {0};
        options.scale = 1;
        options.dpi = 96;
        options.animation_time = 0.5f;
        options.supersampling = 1;
        cupidimage_image image = {0};
        if (!cupidimage_load_svg_with_options((const unsigned char *)svg, (size_t)size, &image,
                                              &options, err, sizeof(err))) {
            fprintf(stderr, "SVG begin=%s failed: %s\n", begins[i], err);
            return 0;
        }
        cupidimage_free(&image);
    }
    return 1;
}

static int test_ico(void) {
    unsigned char ico[70] = {0};
    ico[2] = 1;
    ico[4] = 1; /* One icon. */
    ico[6] = ico[7] = ico[10] = 1;
    ico[12] = 32;
    ico[14] = 48;
    ico[18] = 22;
    ico[22] = 40;
    ico[26] = 1;
    ico[30] = 2;
    ico[34] = 1;
    ico[36] = 32;
    ico[64] = ico[65] = 255; /* One opaque red BGRA pixel. */
    char err[128] = {0};
    cupidimage_image image = {0};
    if (!cupidimage_load_ico(ico, sizeof(ico), &image, err, sizeof(err)) || image.width != 1 ||
        image.height != 1 || memcmp(image.rgba, (unsigned char[]){255, 0, 0, 255}, 4)) {
        fprintf(stderr, "valid ICO failed: %s\n", err);
        cupidimage_free(&image);
        return 0;
    }
    cupidimage_free(&image);
    ico[0] = 1; /* ICONDIR reserved field must be zero. */
    if (cupidimage_load_ico(ico, sizeof(ico), &image, err, sizeof(err)) || image.rgba) {
        fprintf(stderr, "ICO with invalid reserved header was accepted\n");
        cupidimage_free(&image);
        return 0;
    }
    ico[0] = 0;
    ico[30] = 0xfe;
    ico[31] = ico[32] = ico[33] = 0xff;
    if (!cupidimage_load_ico(ico, sizeof(ico), &image, err, sizeof(err)) || image.width != 1 ||
        image.height != 1 || memcmp(image.rgba, (unsigned char[]){255, 0, 0, 255}, 4)) {
        fprintf(stderr, "valid top-down ICO failed: %s\n", err);
        cupidimage_free(&image);
        return 0;
    }
    cupidimage_free(&image);
    ico[31] = ico[32] = 0;
    ico[30] = 0;
    ico[33] = 0x80; /* INT32_MIN cannot be negated. */
    if (cupidimage_load_ico(ico, sizeof(ico), &image, err, sizeof(err)) || image.rgba || !err[0]) {
        fprintf(stderr, "ICO with INT32_MIN height was not rejected\n");
        cupidimage_free(&image);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    int ok = argc > 1 && strcmp(argv[1], "ico") == 0 ? test_ico() : test_svg() && test_ico();
    if (ok)
        puts("Decoder safety regressions passed");
    return ok ? 0 : 1;
}
