#include "cupidimage.h"
#include "pdf_fixture.h"

static unsigned checks, failures;

static size_t zlib_stream(unsigned char *out, const unsigned char *data, size_t size, int mixed) {
    /* Complete dynamic tree: one-bit EOB and 15-bit unused symbols.
       Outputs a zero byte, then starts a final stored block. */
    static const unsigned char prefix[] = {0x78, 0x01, 0x04, 0xe0, 0x81, 0x92, 0x24, 0x49,
                                           0x92, 0x24, 0xc9, 0x12, 0x8b, 0x9a, 0x47, 0x56,
                                           0xcf, 0xde, 0x7b, 0xff, 0xff, 0xb9, 0x88, 0x09};
    size_t pos = 0;
    if (mixed) {
        memcpy(out, prefix, sizeof(prefix));
        pos = sizeof(prefix);
    } else {
        out[pos++] = 0x78;
        out[pos++] = 0x01;
        out[pos++] = 0x01;
    }
    out[pos++] = (unsigned char)size;
    out[pos++] = (unsigned char)(size >> 8);
    out[pos++] = (unsigned char)~size;
    out[pos++] = (unsigned char)(~size >> 8);
    memcpy(out + pos, data, size);
    pos += size;
    uint32_t a = 1, b = mixed ? 1 : 0;
    for (size_t i = 0; i < size; i++) {
        a = (a + data[i]) % 65521;
        b = (b + a) % 65521;
    }
    uint32_t adler = (b << 16) | a;
    for (int shift = 24; shift >= 0; shift -= 8)
        out[pos++] = (unsigned char)(adler >> shift);
    return pos;
}

static void test_pdf(int compressed, int mixed, int corrupt) {
    static const unsigned char content[] = "0 g 10 10 20 20 re f\n";
    unsigned char idat[512];
    size_t size = zlib_stream(idat, content, sizeof(content) - 1, mixed);
    if (corrupt)
        idat[size - 1] ^= 1;
    test_pdf_buffer pdf = test_make_pdf(compressed ? idat : content,
                                        compressed ? size : sizeof(content) - 1, compressed);
    cupidimage_image image = {0};
    char err[128] = {0};
    int ok = cupidimage_load_pdf(pdf.data, pdf.size, &image, err, sizeof(err));
    checks++;
    if (corrupt) {
        if (ok || image.rgba || !err[0]) {
            fprintf(stderr, "FAIL container accepted corrupt Adler-32\n");
            failures++;
        }
        cupidimage_free(&image);
        return;
    }
    if (!ok || image.width != 96 || image.height != 96 ||
        memcmp(image.rgba + (70 * 96 + 20) * 4, (unsigned char[]){0, 0, 0, 255}, 4) ||
        memcmp(image.rgba + (20 * 96 + 80) * 4, (unsigned char[]){255, 255, 255, 255}, 4)) {
        fprintf(stderr, "FAIL PDF compressed=%d mixed=%d: %s\n", compressed, mixed, err);
        failures++;
    }
    cupidimage_free(&image);
}

static void le16(unsigned char *out, unsigned value) {
    out[0] = (unsigned char)value;
    out[1] = (unsigned char)(value >> 8);
}

static void le32(unsigned char *out, uint32_t value) {
    for (unsigned i = 0; i < 4; i++)
        out[i] = (unsigned char)(value >> (i * 8));
}

static void test_tiff(unsigned compression, int mixed, int corrupt) {
    unsigned char data[512] = {'I', 'I', 42, 0, 8};
    unsigned char raw[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    unsigned char idat[128];
    size_t size = zlib_stream(idat, raw + mixed, sizeof(raw) - (size_t)mixed, mixed);
    if (corrupt)
        idat[size - 1] ^= 1;
    const unsigned tags[][3] = {{256, 4, 10},          {257, 4, 1}, {258, 3, 8},
                                {259, 3, compression}, {262, 3, 1}, {273, 4, 122},
                                {277, 3, 1},           {278, 4, 1}, {279, 4, (unsigned)size}};
    le16(data + 8, 9);
    for (unsigned i = 0; i < 9; i++) {
        unsigned char *entry = data + 10 + i * 12;
        le16(entry, tags[i][0]);
        le16(entry + 2, tags[i][1]);
        le32(entry + 4, 1);
        le32(entry + 8, tags[i][2]);
    }
    memcpy(data + 122, idat, size);
    cupidimage_image image = {0};
    char err[128] = {0};
    int ok = cupidimage_load_tiff(data, 122 + size, &image, err, sizeof(err));
    checks++;
    if (corrupt) {
        if (ok || image.rgba || !err[0]) {
            fprintf(stderr, "FAIL container accepted corrupt Adler-32\n");
            failures++;
        }
        cupidimage_free(&image);
        return;
    }
    if (ok && image.width == 10 && image.height == 1) {
        for (unsigned i = 0; i < 10; i++) {
            if (memcmp(image.rgba + i * 4, (unsigned char[]){i, i, i, 255}, 4))
                ok = 0;
        }
    } else
        ok = 0;
    if (!ok) {
        fprintf(stderr, "FAIL TIFF compression=%u mixed=%d: %s\n", compression, mixed, err);
        failures++;
    }
    cupidimage_free(&image);
}

int main(void) {
    test_pdf(0, 0, 0);
    test_pdf(1, 0, 0);
    test_pdf(1, 1, 0);
    test_pdf(1, 0, 1);
    test_pdf(1, 1, 1);
    for (int mixed = 0; mixed < 2; mixed++) {
        test_tiff(8, mixed, 0);
        test_tiff(32946, mixed, 0);
        test_tiff(8, mixed, 1);
        test_tiff(32946, mixed, 1);
    }
    printf("DEFLATE containers: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
