/* Test-only native-loading and supplied-geometry instruction probes. */
#include "formats/pdf/fonts/truetype.h"
#include "formats/pdf/fonts/truetype_hint/hint.h"
#include "formats/pdf/fonts/truetype_vm/vm.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned u16(const unsigned char *p) {
    return (unsigned)p[0] * 256 + p[1];
}

static uint32_t le32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int32_t i32(const unsigned char *p) {
    uint32_t n = le32(p);
    return n <= INT32_MAX ? (int32_t)n : (int32_t)((int64_t)n - INT64_C(4294967296));
}

static unsigned char *read_file(const char *path, size_t *length) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0 || n > 32 * 1024 * 1024 || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        return NULL;
    }
    unsigned char *p = malloc((size_t)n + 1);
    if (!p || fread(p, 1, (size_t)n, f) != (size_t)n) {
        free(p);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *length = (size_t)n;
    return p;
}

static int native_probe(const char *path, const char *size, const char *y_size, const char *index,
                        int grayscale, int factor, const int32_t *matrix, const int32_t *offset) {
    char *end;
    long ppem = strtol(size, &end, 10);
    if (!*size || *end || ppem < 1 || ppem > 96 * 64 / factor)
        return 2;
    long y_ppem = strtol(y_size, &end, 10);
    if (!*y_size || *end || y_ppem < 1 || y_ppem > 96 * 64 / factor)
        return 2;
    unsigned long glyph = strtoul(index, &end, 10);
    if (!*index || *end || glyph > 65535)
        return 2;
    size_t length;
    unsigned char *data = read_file(path, &length);
    pdf_tt_font font;
    if (!data || !cupidimage_pdf_tt_init(&font, data, length)) {
        free(data);
        return 2;
    }
    pdf_tt_hint hint;
    int ok = cupidimage_pdf_tt_hint_init_xy(&hint, &font, (int32_t)ppem * factor,
                                            (int32_t)y_ppem * factor, (int32_t)y_ppem * factor,
                                            grayscale, 0, 1000000);
    if (ok)
        ok = cupidimage_pdf_tt_hint_glyph(&hint, (unsigned)glyph);
    pdf_tt_hint_shape shape = {0};
    if (ok && matrix)
        ok = cupidimage_pdf_tt_hint_shape_build(&hint, matrix, offset, &shape);
    pdf_tt_vm *vm = &hint.vm;
    printf("%d %u %u %zu %u\n", ok, vm->error, vm->error_opcode, vm->error_offset,
           vm->zones[1].count);
    if (ok && matrix) {
        for (unsigned i = 0; i < shape.count; i++)
            printf("%ld %ld %u %u\n", (long)(shape.points[i].x * 64),
                   (long)(shape.points[i].y * 64), shape.points[i].on, shape.points[i].end);
    } else if (ok) {
        unsigned outline = vm->zones[1].outline_count;
        for (unsigned i = 0; i < outline; i++)
            printf("%lld %ld %u %u\n",
                   (long long)vm->zones[1].points[i].current[0] -
                       vm->zones[1].points[outline].current[0],
                   (long)vm->zones[1].points[i].current[1], vm->zones[1].points[i].on,
                   vm->zones[1].points[i].end);
    }
    cupidimage_pdf_tt_hint_shape_free(&shape);
    cupidimage_pdf_tt_hint_free(&hint);
    free(data);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 12 &&
        (!strcmp(argv[2], "--native-matrix") || !strcmp(argv[2], "--native-matrix-gray"))) {
        int32_t values[6];
        for (unsigned i = 0; i < 6; i++) {
            char *end;
            errno = 0;
            long long value = strtoll(argv[6 + i], &end, 10);
            if (!*argv[6 + i] || *end || errno == ERANGE || value < INT32_MIN || value > INT32_MAX)
                return 2;
            values[i] = (int32_t)value;
        }
        return native_probe(argv[1], argv[3], argv[4], argv[5],
                            !strcmp(argv[2], "--native-matrix-gray"), 64, values, values + 4);
    }
    if (argc == 6 && (!strcmp(argv[2], "--native-xy") || !strcmp(argv[2], "--native-xy-gray") ||
                      !strcmp(argv[2], "--native-f26-xy")))
        return native_probe(argv[1], argv[3], argv[4], argv[5],
                            !strcmp(argv[2], "--native-xy-gray"),
                            !strcmp(argv[2], "--native-f26-xy") ? 1 : 64, NULL, NULL);
    if (argc == 5 && (!strcmp(argv[2], "--native") || !strcmp(argv[2], "--native-gray")))
        return native_probe(argv[1], argv[3], argv[3], argv[4], !strcmp(argv[2], "--native-gray"),
                            64, NULL, NULL);
    if (argc != 4)
        return 2;
    char *end;
    long requested = strtol(argv[3], &end, 10);
    if (!*argv[3] || *end || requested < 1 || requested > 96)
        return 2;
    int ppem = (int)requested;
    size_t flen = 0, len = 0;
    unsigned char *data = read_file(argv[1], &flen), *fixture = read_file(argv[2], &len);
    if (!data || !fixture || len < 12) {
        free(data);
        free(fixture);
        return 2;
    }
    pdf_tt_font f;
    if (!cupidimage_pdf_tt_init(&f, data, flen) || f.maxp.length < 32 || f.cvt.length % 2 ||
        f.cvt.length / 2 > 65535) {
        free(data);
        free(fixture);
        return 2;
    }
    unsigned count = le32(fixture), outline = le32(fixture + 4), codes = le32(fixture + 8);
    if (count > 65540 || outline > 65536 || outline > count || count - outline != 4 ||
        codes > 65535 || len != 12 + (size_t)count * 28 + codes) {
        free(data);
        free(fixture);
        return 2;
    }
    pdf_tt_vm_point *points = calloc(count ? count : 1, sizeof(*points));
    unsigned cvts = (unsigned)(f.cvt.length / 2);
    int32_t *cvt = malloc((cvts ? cvts : 1) * sizeof(*cvt));
    if (!points || !cvt) {
        free(points);
        free(cvt);
        free(data);
        free(fixture);
        return 2;
    }
    int32_t (*design)[2] = calloc(count ? count : 1, sizeof(*design));
    if (!design) {
        free(points);
        free(cvt);
        free(data);
        free(fixture);
        return 2;
    }
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *p = fixture + 12 + i * 28;
        points[i] =
            (pdf_tt_vm_point){{i32(p), i32(p + 4)}, {i32(p + 8), i32(p + 12)}, p[16], p[17], p[18]};
        design[i][0] = i32(p + 20);
        design[i][1] = i32(p + 24);
    }
    for (unsigned i = 0; i < cvts; i++) {
        unsigned n = u16(f.cvt.data + i * 2);
        cvt[i] = n < 32768 ? (int32_t)n : (int32_t)n - 65536;
    }
    pdf_tt_vm vm;
    int ok = cupidimage_pdf_tt_vm_init(&vm, u16(f.maxp.data + 24), u16(f.maxp.data + 18), cvt, cvts,
                                       u16(f.maxp.data + 20), u16(f.maxp.data + 22));
    if (ok)
        ok = cupidimage_pdf_tt_vm_run(&vm, f.fpgm.data, f.fpgm.length, PDF_TT_VM_FONT, 1000000);
    if (ok)
        ok = cupidimage_pdf_tt_vm_set_size(&vm, f.units, ppem * 64, ppem * 64, 0, cvt);
    if (ok)
        ok = cupidimage_pdf_tt_vm_set_zones(&vm, u16(f.maxp.data + 16), NULL, 0, 0);
    if (ok)
        ok = cupidimage_pdf_tt_vm_run(&vm, f.prep.data, f.prep.length, PDF_TT_VM_PREP, 1000000) &&
             cupidimage_pdf_tt_vm_save_preparation(&vm);
    if (ok)
        ok = cupidimage_pdf_tt_vm_begin_glyph(&vm, points, count, outline);
    if (ok)
        ok = cupidimage_pdf_tt_vm_set_design(&vm, design, count, vm.size.scale);
    if (ok)
        ok = cupidimage_pdf_tt_vm_run(&vm, fixture + 12 + count * 28, codes, PDF_TT_VM_GLYPH,
                                      1000000);
    printf("%d %u %u %zu %u\n", ok, vm.error, vm.error_opcode, vm.error_offset, count);
    if (ok)
        for (unsigned i = 0; i < outline; i++)
            printf("%lld %ld %u %u\n",
                   (long long)vm.zones[1].points[i].current[0] -
                       vm.zones[1].points[outline].current[0],
                   (long)vm.zones[1].points[i].current[1], vm.zones[1].points[i].on,
                   vm.zones[1].points[i].end);
    cupidimage_pdf_tt_vm_free(&vm);
    free(design);
    free(points);
    free(cvt);
    free(data);
    free(fixture);
    return 0;
}
