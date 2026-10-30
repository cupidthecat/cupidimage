#include "cupidimage.h"
#include "cupidimage_kitty.h"
#include <limits.h>
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

static void validation(void) {
    uint8_t pixel[4] = {19, 92, 165, 238};
    cupidimage_image image = {1, 1, pixel, 0, 0};
    cupidimage_kitty_options opts = {42, 7, 0, 0};
    cupidimage_image frames[2] = {image, image};
    cupidimage_animation anim = {0};
    char error[128], encoded[32];
    FILE *out = tmpfile();
    CHECK(out != NULL);
    if (!out)
        return;
    anim.frames = frames;
    anim.frame_count = 2;
    CHECK(cupidimage_render_kitty_with_options(&image, out, 0, 0, NULL, error, sizeof(error)));
    CHECK(cupidimage_render_kitty_animation_with_options(&anim, out, 0, 0, NULL, error,
                                                         sizeof(error)));
    for (int mode = 0; mode < 4; mode++) {
        cupidimage_image bad = image;
        if (mode == 0)
            bad.width = 0;
        if (mode == 1)
            bad.height = 0;
        if (mode == 2) {
            bad.width = UINT32_MAX;
            bad.height = UINT32_MAX;
        }
        if (mode == 3)
            bad.rgba = NULL;
        CHECK(!cupidimage_render_kitty_with_options(&bad, out, 0, 0, &opts, error, sizeof(error)));
        CHECK(error[0]);
        CHECK(!cupidimage_render_ansi(&bad, out, 1, 1));
        frames[1] = bad;
        long before = ftell(out);
        CHECK(!cupidimage_render_kitty_animation_with_options(&anim, out, 0, 0, &opts, error,
                                                              sizeof(error)));
        CHECK(ftell(out) == before);
    }
    frames[1] = image;
    anim.loop_count = UINT32_MAX;
    CHECK(!cupidimage_render_kitty_animation_with_options(&anim, out, 0, 0, &opts, error,
                                                          sizeof(error)));
    anim.loop_count = 1;
    uint32_t delays[2] = {20, UINT32_MAX};
    anim.delays = delays;
    CHECK(!cupidimage_render_kitty_animation_with_options(&anim, out, 0, 0, &opts, error,
                                                          sizeof(error)));
    CHECK(kitty_base64_encode(pixel, 4, encoded, sizeof(encoded)) == 8);
    CHECK(strcmp(encoded, "E1yl7g==") == 0);
    CHECK(kitty_base64_encode(pixel, 4, encoded, 8) == -1);
    CHECK(kitty_base64_encode(pixel, SIZE_MAX, encoded, sizeof(encoded)) == -1);
    CHECK(kitty_base64_encode(pixel, 4, NULL, 32) == -1);
    CHECK(kitty_base64_encode(NULL, 1, encoded, sizeof(encoded)) == -1);
    CHECK(kitty_base64_encode(NULL, 0, encoded, sizeof(encoded)) == 0 && encoded[0] == 0);
    fclose(out);

    /* /dev/full catches buffered output failures as well as short writes. */
    for (int mode = 0; mode < 6; mode++) {
        out = fopen("/dev/full", "w");
        CHECK(out != NULL);
        if (!out)
            continue;
        int result;
        error[0] = 0;
        anim.delays = NULL;
        if (mode == 0)
            result = cupidimage_render_ansi(&image, out, 1, 1);
        else if (mode == 1)
            result = cupidimage_render_kitty(&image, out, 0, 0, error, sizeof(error));
        else if (mode == 2)
            result = cupidimage_kitty_delete_all(out, error, sizeof(error));
        else if (mode == 3)
            result = cupidimage_kitty_delete_image(out, 42, error, sizeof(error));
        else if (mode == 4)
            result = cupidimage_kitty_delete_placement(out, 42, 7, error, sizeof(error));
        else
            result = cupidimage_render_kitty_animation_with_options(&anim, out, 0, 0, &opts, error,
                                                                    sizeof(error));
        CHECK(result == 0);
        if (mode)
            CHECK(error[0]);
        fclose(out);
    }
}

int main(int argc, char **argv) {
    if (argc == 1) {
        validation();
        printf("Terminal API: %d checks, %d failures\n", checks, failures);
        return failures != 0;
    }
    uint8_t pixels[64 * 32 * 4];
    for (size_t i = 0; i < sizeof(pixels); i++)
        pixels[i] = (uint8_t)(i * 73 + 19);
    cupidimage_image image = {64, 32, pixels, 0, 0};
    cupidimage_kitty_options opts = {42, 7, 1, 0};
    char error[128];
    if (strcmp(argv[1], "image") == 0)
        return !cupidimage_render_kitty_with_options(&image, stdout, 80, 24, &opts, error,
                                                     sizeof(error));
    if (strcmp(argv[1], "delete") == 0)
        return !cupidimage_kitty_delete_placement(stdout, 42, 7, error, sizeof(error));
    if (strcmp(argv[1], "ansi") == 0) {
        image.width = image.height = 1;
        pixels[3] = 255;
        return !cupidimage_render_ansi(&image, stdout, 1, 1);
    }
    if (strcmp(argv[1], "animation") == 0) {
        cupidimage_image frames[2] = {image, image};
        uint32_t delays[2] = {20, 0};
        cupidimage_animation anim = {0};
        anim.frames = frames;
        anim.frame_count = 2;
        anim.delays = delays;
        anim.loop_count = argc > 2 ? (uint32_t)strtoul(argv[2], NULL, 10) : 3;
        return !cupidimage_render_kitty_animation_with_options(&anim, stdout, 80, 24, &opts, error,
                                                               sizeof(error));
    }
    return 2;
}
