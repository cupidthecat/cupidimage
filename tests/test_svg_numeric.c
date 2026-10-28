#include "cupidimage.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(test)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(test)) {                                                                             \
            failures++;                                                                            \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test);                                \
        }                                                                                          \
    } while (0)

static void dimensions(void) {
    const char *values[] = {"nan", "inf", "-inf", "1e50", "4294967296", "-1", "0"};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        char svg[128], error[128] = {0};
        int length = snprintf(svg, sizeof(svg), "<svg width='%s' height='16'/>", values[i]);
        uint32_t width = 0, height = 0;
        CHECK(!cupidimage_svg_get_dimensions((const unsigned char *)svg, (size_t)length, &width,
                                             &height, error, sizeof(error)));
        CHECK(error[0]);
    }
    const char *valid[] = {"0.4", "13.3", "13.6"};
    const uint32_t expected[] = {1, 13, 14};
    for (unsigned i = 0; i < 3; i++) {
        char svg[128], error[128] = {0};
        int length = snprintf(svg, sizeof(svg), "<svg width='%s' height='16'/>", valid[i]);
        uint32_t width = 0, height = 0;
        CHECK(cupidimage_svg_get_dimensions((const unsigned char *)svg, (size_t)length, &width,
                                            &height, error, sizeof(error)));
        CHECK(width == expected[i] && height == 16);
    }
}

static void options(void) {
    const char *svg = "<svg width='16' height='16'/>";
    for (int option = 0; option < 7; option++) {
        cupidimage_svg_options opts = {0};
        opts.scale = 1;
        opts.dpi = 96;
        opts.supersampling = 2;
        if (option == 0)
            opts.scale = NAN;
        if (option == 1)
            opts.scale = INFINITY;
        if (option == 2)
            opts.dpi = NAN;
        if (option == 3)
            opts.animation_time = INFINITY;
        if (option == 4)
            opts.width = opts.height = 2147483648u;
        if (option == 5)
            opts.width = opts.height = 65536;
        if (option == 6)
            opts.scale = 1e30f;
        cupidimage_image image = {0};
        char error[128] = {0};
        CHECK(!cupidimage_load_svg_with_options((const unsigned char *)svg, strlen(svg), &image,
                                                &opts, error, sizeof(error)));
        CHECK(!image.rgba && error[0]);
        cupidimage_free(&image);
    }
}

static void clipped_shapes(void) {
    const char *shapes[] = {"<rect x='-1e15' y='-1e15' width='2e15' height='2e15' fill='red'/>",
                            "<ellipse cx='0' cy='0' rx='1e15' ry='1e15' fill='red'/>",
                            "<path d='M-1e15 -1e15 H1e15 V1e15 H-1e15 Z' fill='red'/>",
                            "<text x='-1e15' y='8' font-size='16'>A</text>",
                            "<rect width='16' height='16' fill='red' filter='url(#f)'/>"};
    for (size_t i = 0; i < sizeof(shapes) / sizeof(shapes[0]); i++) {
        char svg[512], error[128] = {0};
        int length = snprintf(svg, sizeof(svg),
                              "<svg width='16' height='16'>"
                              "<defs><filter id='f' x='-1e15' y='-1e15' width='2e15' height='2e15' "
                              "filterUnits='userSpaceOnUse'><feGaussianBlur "
                              "stdDeviation='1e15'/></filter></defs>%s</svg>",
                              shapes[i]);
        cupidimage_image image = {0};
        CHECK(cupidimage_load_svg((const unsigned char *)svg, (size_t)length, &image, error,
                                  sizeof(error)));
        CHECK(image.width == 16 && image.height == 16 && image.rgba);
        if (image.rgba && i < 3) {
            for (size_t p = 0; p < 16 * 16; p++) {
                CHECK(memcmp(image.rgba + p * 4, (unsigned char[]){255, 0, 0, 255}, 4) == 0);
            }
        }
        cupidimage_free(&image);
    }
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "shapes") == 0)
        clipped_shapes();
    else if (argc > 1 && strcmp(argv[1], "dimensions") == 0)
        dimensions();
    else {
        dimensions();
        options();
        clipped_shapes();
    }
    printf("SVG numeric: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
