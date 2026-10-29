#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, allocations, fail_at;
static size_t live_bytes;

typedef union {
    long double align;

    struct {
        size_t bytes;
    } value;
} allocation_header;

static void *vm_test_malloc(size_t bytes) {
    if (++allocations == fail_at || bytes > SIZE_MAX - sizeof(allocation_header))
        return NULL;
    allocation_header *p = malloc(sizeof(*p) + bytes);
    if (!p)
        return NULL;
    p->value.bytes = bytes;
    live_bytes += bytes;
    return p + 1;
}

static void *vm_test_calloc(size_t count, size_t size) {
    if (size && count > SIZE_MAX / size)
        return NULL;
    void *p = vm_test_malloc(count * size);
    if (p)
        memset(p, 0, count * size);
    return p;
}

static void vm_test_free(void *value) {
    if (value) {
        allocation_header *p = (allocation_header *)value - 1;
        live_bytes -= p->value.bytes;
        free(p);
    }
}

#define malloc vm_test_malloc
#define calloc vm_test_calloc
#define free vm_test_free
#include "pdf_tt_vm_sources.inc"
#undef malloc
#undef calloc
#undef free
#include "formats/pdf/fonts/truetype.h"

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "TT VM check failed at line %d: %s\n", __LINE__, #x);                  \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void init(pdf_tt_vm *vm, const int32_t *cvt, unsigned count) {
    CHECK(cupidimage_pdf_tt_vm_init(vm, 512, 16, cvt, count, 16, 0));
}

static void finish(pdf_tt_vm *vm) {
    cupidimage_pdf_tt_vm_free(vm);
    CHECK(live_bytes == 0);
}

static void result(const unsigned char *code, size_t length, const int32_t *expected,
                   unsigned count) {
    pdf_tt_vm vm;
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_run(&vm, code, length, 1, 100000));
    CHECK(vm.stack_size == count);
    CHECK(!count || !memcmp(vm.stack, expected, count * sizeof(*expected)));
    finish(&vm);
}

static void error(const unsigned char *code, size_t length, pdf_tt_vm_error expected, size_t work) {
    pdf_tt_vm vm;
    init(&vm, NULL, 0);
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, code, length, 1, work));
    CHECK(vm.error == expected);
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, NULL, 0, 1, 100));
    finish(&vm);
}

static uint32_t random_state = 97;

static int32_t random_value(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return (int32_t)(random_state % 200001u) - 100000;
}

static unsigned read16(const unsigned char *p) {
    return (unsigned)p[0] * 256 + p[1];
}

static int font_program(const char *path, unsigned ppem, int show_cvt, int grayscale) {
    FILE *file = fopen(path, "rb");
    if (!file)
        return 2;
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return 2;
    }
    long length = ftell(file);
    if (length < 0 || length > 16 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return 2;
    }
    unsigned char *data = malloc((size_t)length + 1);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(data);
        return 2;
    }
    fclose(file);
    pdf_tt_font font;
    if (!cupidimage_pdf_tt_init(&font, data, (size_t)length) || font.maxp.length < 32 ||
        read16(font.maxp.data) != 1 || read16(font.maxp.data + 2)) {
        free(data);
        return 2;
    }
    unsigned cvt_count = ppem ? (unsigned)(font.cvt.length / 2) : 0;
    if (ppem && (font.cvt.length % 2 || cvt_count > 65535)) {
        free(data);
        return 2;
    }
    int32_t *font_cvt = cvt_count ? malloc(cvt_count * sizeof(*font_cvt)) : NULL;
    if (cvt_count && !font_cvt) {
        free(data);
        return 2;
    }
    for (unsigned i = 0; i < cvt_count; i++) {
        unsigned value = read16(font.cvt.data + i * 2);
        font_cvt[i] = value >= 32768 ? (int32_t)value - 65536 : (int32_t)value;
    }
    pdf_tt_vm vm;
    int ok = cupidimage_pdf_tt_vm_init(&vm, read16(font.maxp.data + 24),
                                       read16(font.maxp.data + 18), font_cvt, cvt_count,
                                       read16(font.maxp.data + 20), read16(font.maxp.data + 22));
    if (ok)
        ok = cupidimage_pdf_tt_vm_run(&vm, font.fpgm.data, font.fpgm.length, 1, 1000000);
    if (ok && ppem)
        ok = cupidimage_pdf_tt_vm_set_size(&vm, font.units, (int32_t)ppem * 64, (int32_t)ppem * 64,
                                           grayscale, font_cvt) &&
             cupidimage_pdf_tt_vm_set_zones(&vm, read16(font.maxp.data + 16), NULL, 0, 0) &&
             cupidimage_pdf_tt_vm_run(&vm, font.prep.data, font.prep.length, PDF_TT_VM_PREP,
                                      1000000);
    if (ok && ppem) {
        CHECK(vm.size.grayscale == grayscale);
        ok = cupidimage_pdf_tt_vm_save_preparation(&vm);
    }
    unsigned defined = 0;
    for (unsigned i = 0; i < vm.function_count; i++)
        defined += vm.functions && vm.functions[i].defined;
    printf("%d %u %u %zu %u %zu %zu %zu\n", ok, defined, vm.error, vm.error_offset, vm.error_opcode,
           font.cvt.length, font.fpgm.length, font.prep.length);
    if (ok && show_cvt)
        for (unsigned i = 0; i < vm.cvt_count; i++)
            printf("%ld\n", (long)vm.cvt[i]);
    cupidimage_pdf_tt_vm_free(&vm);
    free(font_cvt);
    free(data);
    return ok ? 0 : 3;
}

#include "pdf_tt_vm/contours.c"
#include "pdf_tt_vm/direct.c"
#include "pdf_tt_vm/interpolate.c"
#include "pdf_tt_vm/lifecycle.c"
#include "pdf_tt_vm/movement.c"
#include "pdf_tt_vm/points.c"
#include "pdf_tt_vm/relative.c"
#include "pdf_tt_vm/size.c"
#include "pdf_tt_vm/size_xy.c"

#include "pdf_tt_vm/flags.c"
#include "pdf_tt_vm/graphics.c"
#include "pdf_tt_vm/instructions.c"
#include "pdf_tt_vm/intersection.c"

#include "pdf_tt_vm/numeric.c"

static int decode_hex_program(const char *text, unsigned char code[500], size_t *bytes) {
    size_t length = strlen(text);
    if (length % 2 || length > 1000)
        return 0;
    for (size_t i = 0; i < length; i++) {
        unsigned value;
        if (text[i] >= '0' && text[i] <= '9')
            value = (unsigned)(text[i] - '0');
        else if (text[i] >= 'a' && text[i] <= 'f')
            value = (unsigned)(text[i] - 'a') + 10;
        else
            return 0;
        if (i & 1)
            code[i / 2] |= (unsigned char)value;
        else
            code[i / 2] = (unsigned char)(value * 16);
    }
    *bytes = length / 2;
    return 1;
}

static int state_programs(void) {
    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        char *parts[3] = {line, NULL, NULL};
        for (unsigned i = 1; i < 3; i++) {
            parts[i] = strchr(parts[i - 1], ':');
            if (!parts[i])
                return 2;
            *parts[i]++ = 0;
        }
        unsigned char code[3][500];
        size_t length[3];
        for (unsigned i = 0; i < 3; i++)
            if (!decode_hex_program(parts[i], code[i], &length[i]))
                return 2;
        const int32_t raw[] = {64};
        const pdf_tt_vm_point points[] = {{{31, 35}, {31, 35}, 0, 1, 0},
                                          {{100, 0}, {100, 0}, 0, 1, 0},
                                          {{0, 100}, {0, 100}, 0, 1, 1}};
        pdf_tt_vm vm;
        int ok = cupidimage_pdf_tt_vm_init(&vm, 64, 2, raw, 1, 16, 0) &&
                 cupidimage_pdf_tt_vm_set_size(&vm, 64, 64, 64, 0, raw) &&
                 cupidimage_pdf_tt_vm_set_zones(&vm, 2, NULL, 0, 0) &&
                 cupidimage_pdf_tt_vm_run(&vm, code[0], length[0], PDF_TT_VM_PREP, 10000) &&
                 cupidimage_pdf_tt_vm_save_preparation(&vm);
        for (unsigned i = 1; ok && i < 3; i++) {
            ok = cupidimage_pdf_tt_vm_begin_glyph(&vm, points, 3, 3) &&
                 cupidimage_pdf_tt_vm_run(&vm, code[i], length[i], PDF_TT_VM_GLYPH, 10000);
            if (ok)
                for (unsigned point = 0; point < 3; point++)
                    printf("%ld %ld%s", (long)vm.zones[1].points[point].current[0],
                           (long)vm.zones[1].points[point].current[1],
                           i == 2 && point == 2 ? "\n" : " ");
        }
        cupidimage_pdf_tt_vm_free(&vm);
        if (!ok)
            return 3;
    }
    return ferror(stdin) ? 2 : 0;
}

static int expression_programs(int geometry, int flags) {
    char line[1024];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        unsigned char code[500];
        size_t bytes;
        if (!decode_hex_program(line, code, &bytes))
            return 2;
        pdf_tt_vm vm;
        const int32_t raw[] = {64, 128, -64, 0};
        if (!cupidimage_pdf_tt_vm_init(&vm, 64, 0, geometry ? raw : NULL, geometry ? 4 : 0, 0, 0))
            return 2;
        if (geometry) {
            if (!cupidimage_pdf_tt_vm_set_size(&vm, 4096, geometry * 64, geometry * 64, 0, raw)) {
                cupidimage_pdf_tt_vm_free(&vm);
                return 2;
            }
            pdf_tt_vm_point points[] = {{{31, 35}, {31, 35}, 0, 1, 0},
                                        {{100, 0}, {100, 0}, 0, 1, 0},
                                        {{0, 100}, {0, 100}, 0, 1, 1}};
            const int32_t design[][2] = {{31, 35}, {100, 0}, {0, 100}};
            for (unsigned i = 0; i < 3; i++)
                for (unsigned axis = 0; axis < 2; axis++)
                    points[i].current[axis] = points[i].original[axis] =
                        (int32_t)cupidimage_pdf_tt_vm_divide(
                            (int64_t)design[i][axis] * vm.size.scale, 65536);
            if (!cupidimage_pdf_tt_vm_set_zones(&vm, 2, points, 3, 3) ||
                !cupidimage_pdf_tt_vm_set_design(&vm, design, 3, vm.size.scale)) {
                cupidimage_pdf_tt_vm_free(&vm);
                return 2;
            }
        }
        int ok = cupidimage_pdf_tt_vm_run(&vm, code, bytes, 0, 10000);
        if (!ok || (!geometry && vm.stack_size != 1)) {
            cupidimage_pdf_tt_vm_free(&vm);
            return 3;
        }
        if (geometry) {
            for (unsigned i = 0; i < 3; i++) {
                printf("%ld %ld", (long)vm.zones[1].points[i].current[0],
                       (long)vm.zones[1].points[i].current[1]);
                if (flags)
                    printf(" %u %u", vm.zones[1].points[i].on, vm.zones[1].points[i].end);
                printf("%s", i == 2 ? "\n" : " ");
            }
        } else
            printf("%ld\n", (long)vm.stack[0]);
        cupidimage_pdf_tt_vm_free(&vm);
    }
    return ferror(stdin) ? 2 : 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--state-programs"))
        return state_programs();
    if (argc == 2 && !strcmp(argv[1], "--expressions"))
        return expression_programs(0, 0);
    if (argc == 2 && !strcmp(argv[1], "--point-programs"))
        return expression_programs(64, 0);
    if (argc == 3 && (!strcmp(argv[1], "--point-programs") || !strcmp(argv[1], "--point-state"))) {
        char *end;
        long pixels = strtol(argv[2], &end, 10);
        if (!*argv[2] || *end || pixels < 1 || pixels > 32767)
            return 2;
        return expression_programs((int)pixels, !strcmp(argv[1], "--point-state"));
    }
    if (argc == 3 && !strcmp(argv[1], "--font"))
        return font_program(argv[2], 0, 0, 0);
    if (argc == 4 && (!strcmp(argv[1], "--prep") || !strcmp(argv[1], "--prep-cvt") ||
                      !strcmp(argv[1], "--prep-gray") || !strcmp(argv[1], "--prep-cvt-gray"))) {
        char *end;
        long ppem = strtol(argv[3], &end, 10);
        if (!*argv[3] || *end || ppem < 1 || ppem > 32767)
            return 2;
        return font_program(argv[2], (unsigned)ppem, strstr(argv[1], "cvt") != NULL,
                            strstr(argv[1], "gray") != NULL);
    }
    if (argc != 1)
        return 2;
    pdf_tt_vm vm;
    for (unsigned failure = 1; failure <= 4; failure++) {
        int32_t cvt[] = {64, 128};
        allocations = 0;
        fail_at = failure;
        CHECK(!cupidimage_pdf_tt_vm_init(&vm, 8, 8, cvt, 2, 8, 0));
        CHECK(vm.error == PDF_TT_VM_MEMORY);
        CHECK(live_bytes == 0);
        cupidimage_pdf_tt_vm_free(&vm);
    }
    fail_at = 0;
    CHECK(cupidimage_pdf_tt_vm_init(&vm, 0, 0, NULL, 0, 0, 0));
    CHECK(cupidimage_pdf_tt_vm_run(&vm, NULL, 0, 1, 0));
    finish(&vm);
    CHECK(!cupidimage_pdf_tt_vm_init(&vm, 65536, 0, NULL, 0, 0, 0));
    CHECK(live_bytes == 0);
    for (unsigned words = 0; words < 2; words++)
        for (unsigned count = 1; count <= 8; count++) {
            unsigned char code[17];
            int32_t want[8];
            code[0] = (unsigned char)((words ? 0xb8 : 0xb0) + count - 1);
            for (unsigned i = 0; i < count; i++) {
                if (words) {
                    code[1 + i * 2] = (unsigned char)(i & 1 ? 0x7f : 0x80);
                    code[2 + i * 2] = (unsigned char)i;
                    want[i] = (i & 1 ? 32512 : -32768) + (int)i;
                } else {
                    code[1 + i] = (unsigned char)(248 + i);
                    want[i] = 248 + (int)i;
                }
            }
            result(code, 1 + count * (words ? 2 : 1), want, count);
        }
    unsigned char many[512] = {0x41, 255};
    int32_t want[255];
    for (unsigned i = 0; i < 255; i++) {
        many[2 + i * 2] = (unsigned char)(i);
        many[3 + i * 2] = (unsigned char)(255 - i);
        unsigned v = i * 256 + 255 - i;
        want[i] = v < 32768 ? (int32_t)v : (int32_t)v - 65536;
    }
    result(many, sizeof(many), want, 255);
    for (size_t cut = 1; cut < sizeof(many); cut++)
        error(many, cut, PDF_TT_VM_PROGRAM, 1000);
    static const unsigned char no_push[] = {0x40, 0, 0x41, 0};
    result(no_push, sizeof(no_push), NULL, 0);
    static const unsigned char stack[] = {0xb2, 10, 20,   30,   0x8a, 0xb0, 2,   0x25,
                                          0xb0, 3,  0x26, 0x20, 0x21, 0x23, 0x24};
    static const int32_t stacked[] = {20, 10, 30, 30, 4};
    result(stack, sizeof(stack), stacked, 5);
    static const unsigned char clear[] = {0xb0, 1, 0x22};
    result(clear, sizeof(clear), NULL, 0);
    static const unsigned char underflow[] = {0x60};
    error(underflow, sizeof(underflow), PDF_TT_VM_STACK, 100);
    static const unsigned char bad_index[] = {0xb0, 0, 0x25};
    error(bad_index, sizeof(bad_index), PDF_TT_VM_STACK, 100);
    static const unsigned char bad_store[] = {0xb1, 16, 1, 0x42};
    error(bad_store, sizeof(bad_store), PDF_TT_VM_STORAGE, 100);
    static const unsigned char store[] = {0xb1, 2, 99, 0x42};
    static const unsigned char load[] = {0xb0, 2, 0x43};
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_run(&vm, store, sizeof(store), 1, 100));
    CHECK(cupidimage_pdf_tt_vm_run(&vm, load, sizeof(load), 0, 100));
    CHECK(vm.stack_size == 1 && vm.stack[0] == 99);
    finish(&vm);
    static const unsigned char branches[] = {0xb0, 0,    0x58, 0xb0, 0x59, 0x58, 0xb0,
                                             7,    0x59, 0x1b, 0xb0, 42,   0x59};
    static const int32_t answer[] = {42};
    result(branches, sizeof(branches), answer, 1);
    static const unsigned char true_branch[] = {0xb0, 1, 0x58, 0xb0, 42, 0x1b, 0xb0, 0x59, 0x59};
    result(true_branch, sizeof(true_branch), answer, 1);
    static const unsigned char unmatched[] = {0xb0, 0, 0x58, 0xb0, 1};
    error(unmatched, sizeof(unmatched), PDF_TT_VM_PROGRAM, 100);
    static const unsigned char definitions[] = {0xb0, 0, 0x2c, 0xb0, 0x2d, 0x2d, 0xb0, 0, 0x2b};
    static const int32_t fortyfive[] = {45};
    result(definitions, sizeof(definitions), fortyfive, 1);
    static const unsigned char function[] = {0xb0, 0, 0x2c, 0xb0, 1, 0x60, 0x2d};
    static const unsigned char loop[] = {0xb2, 0, 10, 0, 0x2a};
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_run(&vm, function, sizeof(function), 1, 100));
    CHECK(cupidimage_pdf_tt_vm_run(&vm, loop, sizeof(loop), 0, 100));
    CHECK(vm.stack_size == 1 && vm.stack[0] == 10);
    finish(&vm);
    static const unsigned char recursive[] = {0xb0, 0, 0x2c, 0xb0, 0, 0x2b, 0x2d, 0xb0, 0, 0x2b};
    error(recursive, sizeof(recursive), PDF_TT_VM_FUNCTION, 10000);
    static const unsigned char nested[] = {0xb0, 0, 0x2c, 0xb0, 1, 0x2c, 0x2d, 0x2d};
    error(nested, sizeof(nested), PDF_TT_VM_FUNCTION, 100);
    static const unsigned char undefined[] = {0xb0, 1, 0x2b};
    error(undefined, sizeof(undefined), PDF_TT_VM_FUNCTION, 100);
    static const unsigned char missing_end[] = {0xb0, 0, 0x2c, 0xb0, 1};
    error(missing_end, sizeof(missing_end), PDF_TT_VM_PROGRAM, 100);
    static const unsigned char jump[] = {0xb0, 3, 0x1c, 0x20, 0x20, 0xb0, 42};
    result(jump, sizeof(jump), answer, 1);
    static const unsigned char backward[] = {0xb8, 0xff, 0xfd, 0x1c};
    error(backward, sizeof(backward), PDF_TT_VM_BUDGET, 50);
    static const unsigned char outside[] = {0xb8, 0xff, 0xfc, 0x1c};
    error(outside, sizeof(outside), PDF_TT_VM_PROGRAM, 100);
    static const unsigned char unsupported[] = {0x7b};
    error(unsupported, sizeof(unsupported), PDF_TT_VM_UNSUPPORTED, 100);
    static const unsigned char conditional_true[] = {0xb1, 3, 1, 0x78, 0x20, 0x20, 0xb0, 42};
    static const unsigned char conditional_false[] = {0xb1, 3, 0, 0x79, 0x20, 0x20, 0xb0, 42};
    result(conditional_true, sizeof(conditional_true), answer, 1);
    result(conditional_false, sizeof(conditional_false), answer, 1);
    static const unsigned char not_taken[] = {0xb9, 0x80, 0, 0, 0, 0x78, 0xb0, 42};
    result(not_taken, sizeof(not_taken), answer, 1);
    static const unsigned char redefined[] = {0xb0, 0,    0x2c, 0xb0, 1,    0x2d, 0xb0, 0,
                                              0x2c, 0xb0, 42,   0x2d, 0xb0, 0,    0x2b};
    result(redefined, sizeof(redefined), answer, 1);
    init(&vm, NULL, 0);
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, function, sizeof(function), 0, 100));
    CHECK(vm.error == PDF_TT_VM_FUNCTION);
    finish(&vm);
    static const unsigned char end_without_definition[] = {0x2d};
    error(end_without_definition, sizeof(end_without_definition), PDF_TT_VM_FUNCTION, 100);
    static const unsigned char read_zero[] = {0xb0, 0, 0x43};
    static const int32_t zero[] = {0};
    result(read_zero, sizeof(read_zero), zero, 1);
    int32_t cvt_value[] = {-64};
    static const unsigned char write_cvt[] = {0xb1, 0, 42, 0x44, 0xb0, 0, 0x45};
    init(&vm, cvt_value, 1);
    CHECK(cupidimage_pdf_tt_vm_run(&vm, write_cvt, sizeof(write_cvt), 0, 100));
    CHECK(vm.stack_size == 1 && vm.stack[0] == 42 && vm.cvt[0] == 42);
    finish(&vm);
    CHECK(cupidimage_pdf_tt_vm_init(&vm, 1, 0, NULL, 0, 0, 0));
    static const unsigned char full_stack[] = {0xb1, 1, 2};
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, full_stack, sizeof(full_stack), 0, 100));
    CHECK(vm.error == PDF_TT_VM_STACK);
    finish(&vm);
    error(many, sizeof(many), PDF_TT_VM_BUDGET, 50);
    unsigned char deep_if[520];
    size_t deep_length = 0;
    deep_if[deep_length++] = 0xb0;
    deep_if[deep_length++] = 0;
    deep_if[deep_length++] = 0x58;
    for (unsigned i = 0; i < 200; i++)
        deep_if[deep_length++] = 0x58;
    for (unsigned i = 0; i < 201; i++)
        deep_if[deep_length++] = 0x59;
    deep_if[deep_length++] = 0xb0;
    deep_if[deep_length++] = 42;
    result(deep_if, deep_length, answer, 1);
    for (unsigned i = 0; i < 500; i++) {
        int32_t values[] = {random_value(), random_value()};
        unsigned ops[] = {0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x5a, 0x5b, 0x60, 0x61, 0x8b, 0x8c};
        int32_t a = values[0], b = values[1];
        int32_t results[] = {a<b, a <= b, a> b, a >= b, a == b, a != b,        a != 0 && b != 0,
                             a != 0 || b != 0,  a + b,  a - b,  a > b ? a : b, a < b ? a : b};
        for (unsigned j = 0; j < sizeof(ops) / sizeof(*ops); j++) {
            unsigned char code[] = {0xb0, 0, 0x45, 0xb0, 1, 0x45, 0};
            code[6] = (unsigned char)ops[j];
            init(&vm, values, 2);
            CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
            CHECK(vm.stack_size == 1 && vm.stack[0] == results[j]);
            finish(&vm);
        }
    }
    int32_t extreme[] = {INT32_MIN, INT32_MAX, 65, -65};
    static const unsigned char bad_abs[] = {0xb0, 0, 0x45, 0x64};
    init(&vm, extreme, 4);
    CHECK(!cupidimage_pdf_tt_vm_run(&vm, bad_abs, sizeof(bad_abs), 0, 100));
    CHECK(vm.error == PDF_TT_VM_ARITHMETIC);
    finish(&vm);
    static const unsigned char floor_ceil[] = {0xb0, 2, 0x45, 0x66, 0xb0, 2, 0x45, 0x67,
                                               0xb0, 3, 0x45, 0x66, 0xb0, 3, 0x45, 0x67};
    init(&vm, extreme, 4);
    CHECK(cupidimage_pdf_tt_vm_run(&vm, floor_ceil, sizeof(floor_ceil), 0, 100));
    int32_t rounded[] = {64, 128, -128, -64};
    CHECK(vm.stack_size == 4 && !memcmp(vm.stack, rounded, sizeof(rounded)));
    finish(&vm);
    static const unsigned char overflow_cases[][7] = {{0xb0, 1, 0x45, 0xb0, 1, 0x60, 0x59},
                                                      {0xb0, 0, 0x45, 0xb0, 1, 0x61, 0x59},
                                                      {0xb0, 0, 0x45, 0x65, 0x59, 0x59, 0x59},
                                                      {0xb0, 1, 0x45, 0x67, 0x59, 0x59, 0x59}};
    for (unsigned i = 0; i < 4; i++) {
        init(&vm, extreme, 4);
        CHECK(!cupidimage_pdf_tt_vm_run(&vm, overflow_cases[i], 7, 0, 100));
        CHECK(vm.error == PDF_TT_VM_ARITHMETIC);
        finish(&vm);
    }
    static const unsigned char absent_cvt[] = {0xb0, 0, 0x45};
    error(absent_cvt, sizeof(absent_cvt), PDF_TT_VM_STORAGE, 100);
    numeric_tests();
    graphics_tests();
    size_tests();
    size_xy_tests();
    point_tests();
    projection_precision_tests();
    lifecycle_tests();
    movement_tests();
    align_points_tests();
    relative_tests();
    direct_tests();
    contour_tests();
    point_flag_tests();
    instruction_definition_tests();
    intersection_tests();
    interpolation_tests();
    printf("TrueType VM foundations: %u checks, 0 failures\n", checks);
    return 0;
}
