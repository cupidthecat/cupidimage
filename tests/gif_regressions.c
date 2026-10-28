#include "cupidimage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compile only the GIF decoder with these allocator names. Always move a
 * successful realloc so stale cleanup pointers are caught under ASan. */
static size_t allocation_count, fail_at;

typedef union allocation_header {
    size_t size;
    long double alignment;
    void *pointer_alignment;
} allocation_header;

void *gif_test_malloc(size_t size) {
    allocation_header *h;
    if (++allocation_count == fail_at)
        return NULL;
    h = malloc(sizeof(*h) + size);
    if (!h)
        return NULL;
    h->size = size;
    memset(h + 1, 0xa5, size);
    return h + 1;
}

void gif_test_free(void *p) {
    if (p)
        free((allocation_header *)p - 1);
}

void *gif_test_realloc(void *p, size_t size) {
    void *next = gif_test_malloc(size);
    if (!next)
        return NULL;
    if (p) {
        size_t old_size = ((allocation_header *)p - 1)->size;
        memcpy(next, p, old_size < size ? old_size : size);
        gif_test_free(p);
    }
    return next;
}

static unsigned char fixture[4096];
static size_t fixture_size;
static int failures, checks;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void byte(unsigned v) {
    fixture[fixture_size++] = (unsigned char)v;
}

static void le16(unsigned v) {
    byte(v);
    byte(v >> 8);
}

static void header(int full_palette) {
    fixture_size = 0;
    memcpy(fixture, "GIF89a", 6);
    fixture_size = 6;
    le16(2);
    le16(1);
    byte(full_palette ? 0x87 : 0x80);
    byte(full_palette ? 255 : 1);
    byte(0);
    for (unsigned i = 0; i < (full_palette ? 256u : 2u); i++) {
        byte(i ? 23 : 201);
        byte(i ? 45 : 3);
        byte(i ? 67 : 5);
    }
}

static void frame(unsigned w, unsigned h, unsigned left, unsigned disposal) {
    byte(0x21);
    byte(0xf9);
    byte(4);
    byte(disposal << 2);
    le16(7);
    byte(0);
    byte(0);
    byte(0x2c);
    le16(left);
    le16(0);
    le16(w);
    le16(h);
    byte(0);
    byte(2);
    byte(2);
    byte(0x44);
    byte(0x01);
    byte(0); /* clear, 0, end */
}

static void test_pixels(void) {
    char err[128];
    for (int full = 0; full < 2; full++) {
        cupidimage_image img = {0};
        cupidimage_animation anim = {0};
        header(full);
        frame(1, 1, 0, 2);
        frame(1, 1, 1, 0);
        byte(0x3b);
        CHECK(cupidimage_load_gif(fixture, fixture_size, &img, err, sizeof(err)));
        if (img.rgba) {
            CHECK(img.width == 2 && img.height == 1);
            CHECK(memcmp(img.rgba, (unsigned char[]){201, 3, 5, 255, 23, 45, 67, 255}, 8) == 0);
        }
        gif_test_free(img.rgba);
        CHECK(cupidimage_load_gif_animation(fixture, fixture_size, &anim, err, sizeof(err)));
        if (anim.frames) {
            CHECK(anim.frame_count == 2 && anim.delays[0] == 70);
            CHECK(memcmp(anim.frames[1].rgba, (unsigned char[]){23, 45, 67, 255, 201, 3, 5, 255},
                         8) == 0);
            CHECK(anim.frames[0].hotspot_x == 0 && anim.frames[0].hotspot_y == 0);
        }
        cupidimage_free_animation(&anim);
    }
}

static void test_invalid_dimensions(void) {
    char err[128];
    for (int which = 0; which < 3; which++) {
        cupidimage_image img = {0};
        cupidimage_animation anim = {0};
        header(0);
        frame(which == 0 ? 0 : 1, which == 1 ? 0 : 1, which == 2 ? 2 : 0, 0);
        byte(0x3b);
        CHECK(!cupidimage_load_gif(fixture, fixture_size, &img, err, sizeof(err)));
        CHECK(!img.rgba && img.width == 0 && img.height == 0);
        CHECK(!cupidimage_load_gif_animation(fixture, fixture_size, &anim, err, sizeof(err)));
        CHECK(!anim.frames && anim.frame_count == 0);
    }
}

static void test_allocation_failures(void) {
    char err[128];
    cupidimage_animation anim = {0};
    header(0);
    for (int i = 0; i < 5; i++)
        frame(1, 1, 0, 0);
    byte(0x3b);
    allocation_count = 0;
    CHECK(cupidimage_load_gif_animation(fixture, fixture_size, &anim, err, sizeof(err)));
    size_t allocations = allocation_count;
    CHECK(anim.frame_count == 5);
    cupidimage_free_animation(&anim);
    for (size_t i = 1; i <= allocations; i++) {
        allocation_count = 0;
        fail_at = i;
        CHECK(!cupidimage_load_gif_animation(fixture, fixture_size, &anim, err, sizeof(err)));
        CHECK(!anim.frames && !anim.delays && !anim.user_input_flags && !anim.frame_count);
        cupidimage_free_animation(&anim);
    }
    fail_at = 0;
}

static void test_truncation(void) {
    char err[128];
    header(0);
    frame(1, 1, 0, 0);
    byte(0x3b);
    for (size_t size = 0; size < fixture_size - 1; size++) {
        cupidimage_image img = {0};
        cupidimage_animation anim = {0};
        CHECK(!cupidimage_load_gif(fixture, size, &img, err, sizeof(err)));
        CHECK(!img.rgba);
        CHECK(!cupidimage_load_gif_animation(fixture, size, &anim, err, sizeof(err)));
        CHECK(!anim.frames && !anim.frame_count);
    }
}

int main(int argc, char **argv) {
    /* Individual modes also allow baseline crash reproduction. */
    if (argc == 1 || strcmp(argv[1], "pixels") == 0)
        test_pixels();
    if (argc == 1 || strcmp(argv[1], "dimensions") == 0)
        test_invalid_dimensions();
    if (argc == 1 || strcmp(argv[1], "allocations") == 0)
        test_allocation_failures();
    if (argc == 1 || strcmp(argv[1], "truncation") == 0)
        test_truncation();
    printf("GIF regressions: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
