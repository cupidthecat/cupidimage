#include "cupidimage.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
static unsigned expected_height = 1;
static unsigned char data[512];

static void put64(size_t offset, uint64_t value) {
    for (unsigned i = 0; i < 8; i++)
        data[offset + i] = (unsigned char)(value >> (i * 8));
}

static void tag(unsigned index, unsigned number, unsigned type, uint64_t count, uint64_t value) {
    size_t offset = 24 + index * 20;
    data[offset] = (unsigned char)number;
    data[offset + 1] = (unsigned char)(number >> 8);
    data[offset + 2] = (unsigned char)type;
    put64(offset + 4, count);
    put64(offset + 12, value);
}

static void fixture(unsigned compression, int tiled) {
    expected_height = 1;
    memset(data, 0, sizeof(data));
    data[0] = data[1] = 'I';
    data[2] = 43;
    data[4] = 8;
    put64(8, 16);
    put64(16, tiled ? 11 : 9);
    tag(0, 256, 4, 1, 1);
    tag(1, 257, 4, 1, 1);
    tag(2, 258, 3, 1, 8);
    tag(3, 259, 3, 1, compression);
    tag(4, 262, 3, 1, 1);
    tag(5, 277, 3, 1, 1);
    tag(6, tiled ? 324 : 273, 16, 1, 400);
    tag(7, tiled ? 325 : 279, 16, 1, 1);
    tag(8, 278, 4, 1, 1);
    if (tiled) {
        tag(9, 322, 4, 1, 1);
        tag(10, 323, 4, 1, 1);
    }
    data[400] = 73;
}

static void check(const char *name, size_t size, int expected) {
    cupidimage_image image = {0};
    char error[128] = {0};
    int ok = cupidimage_load_tiff(data, size, &image, error, sizeof(error));
    checks++;
    if (ok != expected || (!ok && (image.rgba || !error[0])) ||
        (ok && (image.width != 1 || image.height != expected_height))) {
        fprintf(stderr, "FAIL %s: returned %d: %s\n", name, ok, error);
        failures++;
    }
    if (ok) {
        for (unsigned y = 0; y < expected_height; y++) {
            if (memcmp(image.rgba + y * 4, (unsigned char[]){73, 73, 73, 255}, 4)) {
                fprintf(stderr, "FAIL %s: incorrect pixel on row %u\n", name, y);
                failures++;
            }
        }
    }
    cupidimage_free(&image);
}

int main(void) {
    fixture(1, 0);
    check("valid BigTIFF strip", sizeof(data), 1);
    fixture(1, 1);
    check("valid BigTIFF tile", sizeof(data), 1);
    fixture(1, 0);
    tag(1, 257, 4, 1, 2);
    tag(7, 279, 16, 1, 2);
    tag(8, 278, 4, 1, UINT32_MAX);
    data[401] = 73;
    expected_height = 2;
    check("large RowsPerStrip", sizeof(data), 1);
    fixture(1, 0);
    put64(16, UINT64_MAX / 20);
    check("IFD range wraps", sizeof(data), 0);
    fixture(1, 0);
    tag(2, 258, 3, 5, UINT64_MAX - 3);
    check("tag range wraps", sizeof(data), 0);
    for (int tiled = 0; tiled <= 1; tiled++) {
        fixture(32773, tiled);
        tag(7, tiled ? 325 : 279, 16, 1, 2);
        data[400] = 0;
        data[401] = 73;
        check("valid PackBits block", sizeof(data), 1);
        fixture(32773, tiled);
        tag(7, tiled ? 325 : 279, 16, 1, UINT64_MAX);
        memset(data + 400, 0x80, sizeof(data) - 400);
        check("compressed byte count wraps", sizeof(data), 0);
        fixture(1, tiled);
        tag(6, tiled ? 324 : 273, 16, 1, UINT64_MAX);
        check("pixel offset out of range", sizeof(data), 0);
    }
    fixture(7, 0);
    tag(6, 273, 16, 1, UINT64_MAX);
    tag(7, 279, 16, 1, 4);
    check("JPEG strip range wraps", sizeof(data), 0);
    fixture(7, 0);
    tag(6, 513, 16, 1, UINT64_MAX);
    tag(7, 514, 16, 1, 4);
    check("JPEG interchange range wraps", sizeof(data), 0);
    printf("TIFF bounds: %u checks, %u failures\n", checks, failures);
    return failures != 0;
}
