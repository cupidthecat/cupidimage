#include "cupidimage.h"
#include "pdf_fixture.h"

typedef struct {
    const char *name, *content;
    unsigned x, y;
    unsigned char rgb[3];
} probe;

int main(void) {
    static const probe probes[] = {
        {"c stroke", "3 w 12 12 m 12 60 60 60 60 12 c S", 36, 48, {0, 0, 0}},
        {"c leaves chord open", "3 w 12 12 m 12 60 60 60 60 12 c S", 36, 12, {255, 255, 255}},
        {"v stroke", "3 w 12 12 m 12 60 60 12 v S", 18, 30, {0, 0, 0}},
        {"y stroke", "3 w 12 12 m 12 60 60 12 y S", 36, 30, {0, 0, 0}},
        {"curve fill", "12 12 m 12 60 60 60 60 12 c f", 36, 30, {0, 0, 0}},
        {"curve fill exterior", "12 12 m 12 60 60 60 60 12 c f", 36, 55, {255, 255, 255}},
        {"transformed curve",
         "q .5 0 0 .5 20 20 cm 3 w 12 12 m 12 60 60 60 60 12 c S Q",
         38,
         44,
         {0, 0, 0}},
        {"degenerate curve", "3 w 36 36 m 36 36 36 36 36 36 c S", 36, 36, {0, 0, 0}},
        {"curve without current point", "12 60 60 60 60 12 c S", 36, 48, {255, 255, 255}},
        {"default nonzero fill", "12 12 48 48 re 24 24 24 24 re f", 36, 36, {0, 0, 0}},
        {"F alias", "12 12 48 48 re 24 24 24 24 re F", 36, 36, {0, 0, 0}},
        {"even-odd fill", "12 12 48 48 re 24 24 24 24 re f*", 36, 36, {255, 255, 255}},
        {"B nonzero", "12 12 48 48 re 24 24 24 24 re B", 36, 36, {0, 0, 0}},
        {"B even-odd", "12 12 48 48 re 24 24 24 24 re B*", 36, 36, {255, 255, 255}},
        {"b nonzero", "12 12 48 48 re 24 24 24 24 re b", 36, 36, {0, 0, 0}},
        {"b even-odd", "12 12 48 48 re 24 24 24 24 re b*", 36, 36, {255, 255, 255}},
        {"opposite winding hole",
         "12 12 48 48 re 24 24 m 24 48 l 48 48 l 48 24 l h f",
         36,
         36,
         {255, 255, 255}},
        {"open triangle fill", "12 12 m 60 12 l 36 60 l f", 36, 30, {0, 0, 0}},
        {"open triangle exterior", "12 12 m 60 12 l 36 60 l f", 10, 30, {255, 255, 255}},
        {"multiple open subpaths",
         "12 12 m 60 12 l 36 60 l 60 12 m 68 12 l 64 24 l f",
         10,
         30,
         {255, 255, 255}},
        {"open stroke stays open", "3 w 12 12 m 60 12 l 36 60 l S", 24, 36, {255, 255, 255}},
        {"B does not stroke implicit closure",
         "1 0 0 rg 0 0 1 RG 3 w 12 12 m 60 12 l 36 60 l B",
         25,
         36,
         {255, 0, 0}},
        {"b strokes explicit closure",
         "1 0 0 rg 0 0 1 RG 3 w 12 12 m 60 12 l 36 60 l b",
         24,
         36,
         {0, 0, 255}},
        {"CMYK fill", "1 0 0 0 k 12 12 48 48 re f", 36, 36, {0, 255, 255}},
        {"CMYK stroke", "1 0 0 0 K 3 w 12 12 48 48 re S", 12, 36, {0, 255, 255}},
        {"CMYK black", "0 0 0 1 k 12 12 48 48 re f", 36, 36, {0, 0, 0}},
        {"CMYK mixed inks", ".25 .5 .75 .25 k 12 12 48 48 re f", 36, 36, {128, 64, 0}},
        {"CMYK restore",
         "1 0 0 0 k q 0 1 0 0 k 12 12 20 48 re f Q 40 12 20 48 re f",
         48,
         36,
         {0, 255, 255}},
        {"CMYK saved color",
         "1 0 0 0 k q 0 1 0 0 k 12 12 20 48 re f Q 40 12 20 48 re f",
         24,
         36,
         {255, 0, 255}},
        {"CMYK clamps components", "-1 0 0 1 k 12 12 48 48 re f", 36, 36, {0, 0, 0}}};
    unsigned failures = 0;
    for (unsigned i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        const probe *p = &probes[i];
        test_pdf_buffer pdf = test_make_pdf(p->content, strlen(p->content), 0);
        cupidimage_image image = {0};
        char err[128] = {0};
        int ok = cupidimage_load_pdf(pdf.data, pdf.size, &image, err, sizeof(err));
        if (!ok || image.width != 96 || image.height != 96) {
            fprintf(stderr, "FAIL %s: %s (%ux%u)\n", p->name, err, image.width, image.height);
            failures++;
        } else {
            const unsigned char *pixel = image.rgba + ((72 - p->y) * 4 / 3 * 96 + p->x * 4 / 3) * 4;
            if (memcmp(pixel, p->rgb, 3) || pixel[3] != 255) {
                fprintf(stderr, "FAIL %s: got %u,%u,%u expected %u,%u,%u\n", p->name, pixel[0],
                        pixel[1], pixel[2], p->rgb[0], p->rgb[1], p->rgb[2]);
                failures++;
            }
        }
        cupidimage_free(&image);
    }
    printf("PDF graphics: %zu probes, %u failures\n", sizeof(probes) / sizeof(probes[0]), failures);
    return failures ? 1 : 0;
}
