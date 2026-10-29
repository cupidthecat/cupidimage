#include "cupidimage.h"
#include "pdf_probe_options.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int read_input(const char *path, unsigned char **data, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file)
        return 0;
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return 0;
    }
    long length = ftell(file);
    if (length <= 0 || length > 64 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return 0;
    }
    *size = (size_t)length;
    *data = malloc(*size);
    int ok = *data && fread(*data, 1, *size, file) == *size;
    fclose(file);
    return ok;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--capabilities"))
        return pdf_probe_capabilities() ? 0 : 3;
    if (argc < 5)
        return 2;
    cupidimage_pdf_options options = {0};
    if ((argc > 5 && strcmp(argv[2], "pdf")) || !pdf_probe_options(argc, argv, 5, &options))
        return 2;
    const char *path = argv[1], *format = argv[2], *mode = argv[4];
    char *end = NULL;
    long page_value = strtol(argv[3], &end, 10);
    if (!end || *end || page_value < 0 || page_value > 100000)
        return 2;
    int page = (int)page_value, count = 1, ok = 0;
    unsigned char *data = NULL;
    size_t size = 0;
    cupidimage_image image = {0};
    cupidimage_animation animation = {0};
    uint32_t delay = 0, loops = 1;
    char error[256] = {0};
    if (!read_input(path, &data, &size))
        goto done;
    if (!strcmp(mode, "count") && strcmp(format, "gif") && strcmp(format, "pdf") &&
        strcmp(format, "tiff") && strcmp(format, "ico") && strcmp(format, "tga")) {
        ok = printf("1\n") > 0; /* Single-image decoders are checked by the pixel request. */
        goto done;
    }
    if (!strcmp(format, "gif")) {
        if (!cupidimage_load_gif_animation(data, size, &animation, error, sizeof(error)))
            goto done;
        count = (int)animation.frame_count;
        loops = animation.loop_count;
        if (page >= count)
            goto done;
        delay = animation.delays ? animation.delays[page] : 0;
        image = animation.frames[page];
        animation.frames[page].rgba = NULL;
        ok = 1;
    } else if (!strcmp(format, "pdf")) {
        if (!cupidimage_get_pdf_page_count(data, size, &count, error, sizeof(error)))
            goto done;
        ok = !strcmp(mode, "count") ||
             cupidimage_load_pdf_page_with_options(data, size, &image, page, &options, error,
                                                   sizeof(error));
    } else if (!strcmp(format, "tiff")) {
        if (!cupidimage_get_tiff_page_count(data, size, &count, error, sizeof(error)))
            goto done;
        ok = !strcmp(mode, "count") ||
             cupidimage_load_tiff_page(data, size, &image, page, error, sizeof(error));
    } else if (!strcmp(format, "ico")) {
        cupidimage_ico_entry *entries = NULL;
        int cursor = 0;
        if (!cupidimage_ico_get_directory(path, &entries, &count, &cursor, error, sizeof(error)))
            goto done;
        free(entries);
        ok = !strcmp(mode, "count") ||
             cupidimage_load_ico_page(path, page, &image, error, sizeof(error));
    } else if (page != 0)
        goto done;
    else if (!strcmp(format, "png"))
        ok = cupidimage_load_png(data, size, &image, error, sizeof(error));
    else if (!strcmp(format, "jpeg"))
        ok = cupidimage_load_jpeg(data, size, &image, error, sizeof(error));
    else if (!strcmp(format, "webp"))
        ok = cupidimage_load_webp(data, size, &image, error, sizeof(error));
    else if (!strcmp(format, "bmp"))
        ok = cupidimage_load_bmp(data, size, &image, error, sizeof(error));
    else if (!strcmp(format, "svg"))
        ok = cupidimage_load_svg(data, size, &image, error, sizeof(error));
    else if (!strcmp(format, "tga")) {
        ok = cupidimage_load_tga(data, size, &image, error, sizeof(error));
        if (ok && !image.rgba && !image.width && !image.height)
            count = 0;
    }
    if (!ok)
        goto done;
    if (!strcmp(mode, "count")) {
        ok = printf("%d\n", count) > 0;
        goto done;
    }
    if (!image.rgba || !image.width || !image.height) {
        ok = 0;
        goto done;
    }
    if (!strcmp(mode, "bundle")) {
        uint32_t header[] = {
            image.width, image.height, (uint32_t)count, image.hotspot_x, image.hotspot_y,
            delay,       loops};
        size_t pixels = (size_t)image.width * image.height;
        if (fwrite(header, sizeof(header), 1, stdout) != 1 ||
            fwrite(image.rgba, 4, pixels, stdout) != pixels) {
            ok = 0;
            goto done;
        }
    }
    if (strcmp(mode, "pixels")) {
        uint32_t width = image.width < 16 ? image.width : 16;
        uint32_t height = image.height < 16 ? image.height : 16;
        unsigned char *sample = malloc((size_t)width * height * 4);
        if (!sample) {
            ok = 0;
            goto done;
        }
        for (uint32_t y = 0; y < height; y++)
            for (uint32_t x = 0; x < width; x++) {
                uint32_t sx = (uint32_t)((uint64_t)x * image.width / width);
                uint32_t sy = (uint32_t)((uint64_t)y * image.height / height);
                memcpy(sample + ((size_t)y * width + x) * 4,
                       image.rgba + ((size_t)sy * image.width + sx) * 4, 4);
            }
        free(image.rgba);
        image.rgba = sample;
        image.width = width;
        image.height = height;
    }
    if (!strcmp(mode, "pixels") || !strcmp(mode, "sample") || !strcmp(mode, "bundle")) {
        uint32_t header[] = {
            image.width, image.height, (uint32_t)count, image.hotspot_x, image.hotspot_y,
            delay,       loops};
        size_t pixels = (size_t)image.width * image.height;
        ok = fwrite(header, sizeof(header), 1, stdout) == 1 &&
             fwrite(image.rgba, 4, pixels, stdout) == pixels;
        if (ok && !strcmp(mode, "bundle")) {
            for (int backend = 0; backend < 3 && ok; backend++) {
                FILE *stream = tmpfile();
                if (!stream) {
                    ok = 0;
                    break;
                }
                if (backend == 0)
                    ok = cupidimage_render_ansi(&image, stream, 0, 0);
                else if (backend == 1) {
                    cupidimage_kitty_options options = {42, 7, 1, 0};
                    ok = cupidimage_render_kitty_with_options(&image, stream, 0, 0, &options, error,
                                                              sizeof(error));
                } else {
                    cupidimage_sixel_options options = {0};
                    options.max_colors = 256;
                    options.background_color = 0xFFFFFF;
                    options.pixel_aspect_ratio = 1.0f;
                    ok = cupidimage_render_sixel_with_options(&image, stream, 0, 0, &options, error,
                                                              sizeof(error));
                }
                long length = ftell(stream);
                if (length < 0 || length > 65536 || fseek(stream, 0, SEEK_SET))
                    ok = 0;
                uint32_t count_bytes = length >= 0 ? (uint32_t)length : 0;
                if (ok && fwrite(&count_bytes, sizeof(count_bytes), 1, stdout) != 1)
                    ok = 0;
                unsigned char buffer[4096];
                while (ok && count_bytes) {
                    size_t part = count_bytes > sizeof(buffer) ? sizeof(buffer) : count_bytes;
                    if (fread(buffer, 1, part, stream) != part ||
                        fwrite(buffer, 1, part, stdout) != part)
                        ok = 0;
                    count_bytes -= (uint32_t)part;
                }
                fclose(stream);
            }
        }
    } else if (!strcmp(mode, "ansi")) {
        ok = cupidimage_render_ansi(&image, stdout, 0, 0);
    } else if (!strcmp(mode, "kitty")) {
        cupidimage_kitty_options options = {42, 7, 1, 0};
        ok = cupidimage_render_kitty_with_options(&image, stdout, 0, 0, &options, error,
                                                  sizeof(error));
    } else if (!strcmp(mode, "sixel")) {
        cupidimage_sixel_options options = {0};
        options.max_colors = 256;
        options.background_color = 0xFFFFFF;
        options.pixel_aspect_ratio = 1.0f;
        ok = cupidimage_render_sixel_with_options(&image, stdout, 0, 0, &options, error,
                                                  sizeof(error));
    } else
        ok = 0;
done:
    free(data);
    cupidimage_free(&image);
    cupidimage_free_animation(&animation);
    if (!ok)
        fprintf(stderr, "%s\n", error[0] ? error : "asset probe could not load/render input");
    return ok ? 0 : 3;
}
