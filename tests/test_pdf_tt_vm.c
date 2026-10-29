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
#include "../src/formats/pdf/fonts/truetype_vm/graphics.c"
#include "../src/formats/pdf/fonts/truetype_vm/numeric.c"
#include "../src/formats/pdf/fonts/truetype_vm/stack.c"
#include "../src/formats/pdf/fonts/truetype_vm/vm.c"
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
    CHECK(cupidimage_pdf_tt_vm_init(vm, 512, 16, cvt, count, 16));
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

static int font_program(const char *path) {
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
    pdf_tt_vm vm;
    int ok =
        cupidimage_pdf_tt_vm_init(&vm, read16(font.maxp.data + 24), read16(font.maxp.data + 18),
                                  NULL, 0, read16(font.maxp.data + 20));
    if (ok)
        ok = cupidimage_pdf_tt_vm_run(&vm, font.fpgm.data, font.fpgm.length, 1, 1000000);
    unsigned defined = 0;
    for (unsigned i = 0; i < vm.function_count; i++)
        defined += vm.functions && vm.functions[i].defined;
    printf("%d %u %u %zu %u %zu %zu %zu\n", ok, defined, vm.error, vm.error_offset, vm.error_opcode,
           font.cvt.length, font.fpgm.length, font.prep.length);
    cupidimage_pdf_tt_vm_free(&vm);
    free(data);
    return ok ? 0 : 3;
}

static void graphics_tests(void) {
    pdf_tt_vm vm;
    init(&vm, NULL, 0);
    pdf_tt_vm_graphics *g = &vm.graphics;
    CHECK(g->rounding.period == 64 && g->rounding.phase == 0 && g->rounding.threshold == 32 &&
          !g->rounding.off);
    CHECK(g->projection[0] == 16384 && !g->projection[1]);
    CHECK(g->freedom[0] == 16384 && !g->freedom[1]);
    CHECK(g->dual_projection[0] == 16384 && !g->dual_projection[1]);
    CHECK(g->zone[0] == 1 && g->zone[1] == 1 && g->zone[2] == 1);
    CHECK(!g->reference[0] && !g->reference[1] && !g->reference[2]);
    CHECK(g->loop == 1 && g->minimum_distance == 64 && g->cvt_cut_in == 68);
    CHECK(g->delta_base == 9 && g->delta_shift == 3 && !g->single_width_cut_in && g->auto_flip);
    static const unsigned char axis[] = {0x00, 0x0c, 0x0d, 0x03, 0x0c, 0x0d, 0x0e, 0x0d,
                                         0x04, 0x0d, 0x01, 0x02, 0x05, 0x0c, 0x0d};
    static const int32_t expected[] = {0,     16384, 0, 16384, 16384, 0,     0,     16384,
                                       16384, 0,     0, 16384, 0,     16384, 16384, 0};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, axis, sizeof(axis), 0, 100));
    CHECK(vm.stack_size == sizeof(expected) / sizeof(*expected));
    CHECK(!memcmp(vm.stack, expected, sizeof(expected)));
    CHECK(g->dual_projection[0] == 0 && g->dual_projection[1] == 16384);
    static const unsigned char setters[] = {
        0xb0, 7,    0x10, 0xb0, 8,    0x11, 0xb0, 9,    0x12, 0xb0, 0,    0x16, 0xb0,
        1,    0x14, 0xb0, 1,    0x15, 0xb0, 100,  0x17, 0xb0, 25,   0x1a, 0xb0, 16,
        0x1d, 0xb0, 32,   0x1e, 0xb0, 12,   0x5e, 0xb0, 5,    0x5f, 0x4e};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, setters, sizeof(setters), 0, 100));
    CHECK(!vm.stack_size);
    CHECK(g->reference[0] == 7 && g->reference[1] == 8 && g->reference[2] == 9);
    CHECK(g->zone[0] == 0 && g->zone[1] == 1 && g->zone[2] == 1);
    CHECK(g->loop == 100 && g->minimum_distance == 25 && g->cvt_cut_in == 16);
    CHECK(g->delta_base == 12 && g->delta_shift == 5 && g->single_width_cut_in == 32 &&
          !g->auto_flip);
    static const unsigned char finish_set[] = {0xb0, 1, 0x13, 0xb0, 0, 0x17, 0x4d};
    CHECK(cupidimage_pdf_tt_vm_run(&vm, finish_set, sizeof(finish_set), 0, 100));
    CHECK(g->zone[0] == 1 && g->loop == 0 && g->auto_flip);
    finish(&vm);
    const unsigned char pops[] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
                                  0x17, 0x1a, 0x1d, 0x1e, 0x5e, 0x5f};
    for (unsigned i = 0; i < sizeof(pops); i++) {
        error(pops + i, 1, PDF_TT_VM_STACK, 100);
        unsigned char negative[] = {0xb8, 0xff, 0xff, pops[i]};
        if (pops[i] == 0x1a || pops[i] == 0x1d || pops[i] == 0x1e)
            result(negative, sizeof(negative), NULL, 0);
        else
            error(negative, sizeof(negative), PDF_TT_VM_PROGRAM, 100);
    }
    for (unsigned op = 0x13; op <= 0x16; op++) {
        unsigned char bad_zone[] = {0xb0, 2, (unsigned char)op};
        error(bad_zone, sizeof(bad_zone), PDF_TT_VM_PROGRAM, 100);
    }
    for (unsigned op = 0x0c; op <= 0x0d; op++) {
        CHECK(cupidimage_pdf_tt_vm_init(&vm, 1, 0, NULL, 0, 0));
        unsigned char code = (unsigned char)op;
        CHECK(!cupidimage_pdf_tt_vm_run(&vm, &code, 1, 0, 100));
        CHECK(vm.error == PDF_TT_VM_STACK);
        finish(&vm);
    }
}

static void numeric_tests(void) {
    pdf_tt_vm vm;
    init(&vm, NULL, 0);
    const unsigned modes[] = {0x18, 0x19, 0x3d, 0x7a, 0x7c, 0x7d};
    for (unsigned m = 0; m < sizeof(modes) / sizeof(*modes); m++) {
        CHECK(cupidimage_pdf_tt_vm_numeric_op(&vm, modes[m]));
        for (int n = -4096; n <= 4096; n++) {
            double magnitude = fabs((double)n);
            double rounded;
            switch (m) {
                case 0:
                    rounded = floor(magnitude / 64 + 0.5) * 64;
                    break;
                case 1:
                    rounded = floor(magnitude / 64) * 64 + 32;
                    break;
                case 2:
                    rounded = floor(magnitude / 32 + 0.5) * 32;
                    break;
                case 3:
                    rounded = magnitude;
                    break;
                case 4:
                    rounded = ceil(magnitude / 64) * 64;
                    break;
                default:
                    rounded = floor(magnitude / 64) * 64;
                    break;
            }
            int32_t expected = (int32_t)(n < 0 ? -rounded : rounded);
            for (unsigned color = 0; color < 3; color++) {
                vm.stack_size = 0;
                CHECK(cupidimage_pdf_tt_vm_push(&vm, n));
                CHECK(cupidimage_pdf_tt_vm_numeric_op(&vm, 0x68 + color));
                CHECK(vm.stack_size == 1 && vm.stack[0] == expected);
                CHECK(cupidimage_pdf_tt_vm_numeric_op(&vm, 0x6c + color));
                CHECK(vm.stack[0] == expected);
            }
            for (unsigned parity = 0; parity < 2; parity++) {
                vm.stack_size = 0;
                CHECK(cupidimage_pdf_tt_vm_push(&vm, n));
                CHECK(cupidimage_pdf_tt_vm_numeric_op(&vm, 0x56 + parity));
                int32_t remainder = expected % 128;
                int match = parity ? remainder == 0 : remainder == 64 || remainder == -64;
                CHECK(vm.stack[0] == match);
            }
        }
    }
    for (unsigned selector = 0; selector < 192; selector++) {
        vm.stack_size = 0;
        CHECK(cupidimage_pdf_tt_vm_push(&vm, selector | 0x100));
        CHECK(cupidimage_pdf_tt_vm_numeric_op(&vm, 0x76));
        int period = 32 << (selector >> 6);
        int phase = ((selector >> 4) & 3) * period / 4;
        int threshold = selector & 15;
        threshold = threshold ? (threshold - 4) * period / 8 : period - 1;
        for (int n = -1024; n <= 1024; n++) {
            /* Locate the containing threshold interval by enumeration, without
             * sharing the implementation's signed division/floor calculation. */
            int magnitude = n < 0 ? -n : n;
            int expected = phase;
            while (expected + period - threshold <= magnitude)
                expected += period;
            if (n < 0)
                expected = -expected;
            vm.stack_size = 0;
            CHECK(cupidimage_pdf_tt_vm_push(&vm, n));
            CHECK(cupidimage_pdf_tt_vm_numeric_op(&vm, 0x68));
            CHECK(vm.stack[0] == expected);
        }
    }
    finish(&vm);
    for (unsigned i = 0; i < 4000; i++) {
        int32_t a = random_value(), b = random_value();
        int32_t values[] = {a, b};
        unsigned char code[] = {0xb0, 0, 0x45, 0xb0, 1, 0x45, 0};
        for (unsigned op = 0x62; op <= 0x63; op++) {
            if (op == 0x62 && !b)
                continue;
            code[6] = (unsigned char)op;
            int64_t expected = op == 0x62 ? (int64_t)trunc((double)a * 64 / b)
                                          : (int64_t)round((double)a * b / 64);
            init(&vm, values, 2);
            CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
            CHECK(vm.stack_size == 1 && vm.stack[0] == expected);
            finish(&vm);
        }
    }
    const int32_t boundaries[][4] = {{1, 32, 0x63, 1},
                                     {-1, 32, 0x63, -1},
                                     {1, -32, 0x63, -1},
                                     {-1, -32, 0x63, 1},
                                     {1, 31, 0x63, 0},
                                     {-1, 31, 0x63, 0},
                                     {INT32_MIN, 64, 0x63, INT32_MIN},
                                     {INT32_MAX, 64, 0x63, INT32_MAX},
                                     {INT32_MIN, 64, 0x62, INT32_MIN},
                                     {INT32_MAX, 64, 0x62, INT32_MAX},
                                     {-1, 100, 0x62, 0},
                                     {1, -100, 0x62, 0},
                                     {100, -3, 0x62, -2133}};
    for (unsigned i = 0; i < sizeof(boundaries) / sizeof(*boundaries); i++) {
        init(&vm, boundaries[i], 2);
        unsigned char code[] = {0xb0, 0, 0x45, 0xb0, 1, 0x45, (unsigned char)boundaries[i][2]};
        CHECK(cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
        CHECK(vm.stack_size == 1 && vm.stack[0] == boundaries[i][3]);
        finish(&vm);
    }
    const int32_t failures[][3] = {
        {0, 0, 0x62},           {INT32_MIN, -64, 0x62},       {INT32_MAX, 1, 0x62},
        {INT32_MIN, -64, 0x63}, {INT32_MAX, INT32_MAX, 0x63}, {INT32_MIN, INT32_MIN, 0x63}};
    for (unsigned i = 0; i < sizeof(failures) / sizeof(*failures); i++) {
        init(&vm, failures[i], 2);
        unsigned char code[] = {0xb0, 0, 0x45, 0xb0, 1, 0x45, (unsigned char)failures[i][2]};
        CHECK(!cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
        CHECK(vm.error == PDF_TT_VM_ARITHMETIC);
        finish(&vm);
    }
    for (unsigned selector = 192; selector < 256; selector++) {
        unsigned char code[] = {0xb0, (unsigned char)selector, 0x76};
        error(code, sizeof(code), PDF_TT_VM_PROGRAM, 100);
    }
    const unsigned char underflows[] = {0x56, 0x57, 0x62, 0x63, 0x68, 0x69,
                                        0x6a, 0x6c, 0x6d, 0x6e, 0x76};
    for (unsigned i = 0; i < sizeof(underflows); i++)
        error(underflows + i, 1, PDF_TT_VM_STACK, 100);
    const unsigned char reserved[] = {0x6b, 0x6f};
    for (unsigned i = 0; i < sizeof(reserved); i++)
        error(reserved + i, 1, PDF_TT_VM_UNSUPPORTED, 100);

    for (unsigned op = 0x18; op <= 0x18; op++) {
        int32_t extremes[] = {INT32_MAX};
        init(&vm, extremes, 1);
        unsigned char code[] = {(unsigned char)op, 0xb0, 0, 0x45, 0x68};
        CHECK(!cupidimage_pdf_tt_vm_run(&vm, code, sizeof(code), 0, 100));
        CHECK(vm.error == PDF_TT_VM_ARITHMETIC);
        finish(&vm);
    }
    static const unsigned char set_half[] = {0x19};
    static const unsigned char round_zero[] = {0xb0, 0, 0x68};
    init(&vm, NULL, 0);
    CHECK(cupidimage_pdf_tt_vm_run(&vm, set_half, sizeof(set_half), 0, 100));
    CHECK(cupidimage_pdf_tt_vm_run(&vm, round_zero, sizeof(round_zero), 0, 100));
    CHECK(vm.stack_size == 1 && vm.stack[0] == 32);
    finish(&vm);
}

static int expression_programs(void) {
    char line[1024];
    while (fgets(line, sizeof(line), stdin)) {
        size_t length = strcspn(line, "\r\n");
        if (!length || length % 2 || length > 1000)
            return 2;
        unsigned char code[500];
        for (size_t i = 0; i < length; i++) {
            unsigned value;
            if (line[i] >= '0' && line[i] <= '9')
                value = (unsigned)(line[i] - '0');
            else if (line[i] >= 'a' && line[i] <= 'f')
                value = (unsigned)(line[i] - 'a') + 10;
            else
                return 2;
            if (i & 1)
                code[i / 2] |= (unsigned char)value;
            else
                code[i / 2] = (unsigned char)(value * 16);
        }
        pdf_tt_vm vm;
        if (!cupidimage_pdf_tt_vm_init(&vm, 64, 0, NULL, 0, 0))
            return 2;
        int ok = cupidimage_pdf_tt_vm_run(&vm, code, length / 2, 0, 10000);
        if (!ok || vm.stack_size != 1) {
            cupidimage_pdf_tt_vm_free(&vm);
            return 3;
        }
        printf("%ld\n", (long)vm.stack[0]);
        cupidimage_pdf_tt_vm_free(&vm);
    }
    return ferror(stdin) ? 2 : 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--expressions"))
        return expression_programs();
    if (argc == 3 && !strcmp(argv[1], "--font"))
        return font_program(argv[2]);
    if (argc != 1)
        return 2;
    pdf_tt_vm vm;
    for (unsigned failure = 1; failure <= 4; failure++) {
        int32_t cvt[] = {64, 128};
        allocations = 0;
        fail_at = failure;
        CHECK(!cupidimage_pdf_tt_vm_init(&vm, 8, 8, cvt, 2, 8));
        CHECK(vm.error == PDF_TT_VM_MEMORY);
        CHECK(live_bytes == 0);
        cupidimage_pdf_tt_vm_free(&vm);
    }
    fail_at = 0;
    CHECK(cupidimage_pdf_tt_vm_init(&vm, 0, 0, NULL, 0, 0));
    CHECK(cupidimage_pdf_tt_vm_run(&vm, NULL, 0, 1, 0));
    finish(&vm);
    CHECK(!cupidimage_pdf_tt_vm_init(&vm, 65536, 0, NULL, 0, 0));
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
    static const unsigned char unsupported[] = {0x77};
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
    CHECK(cupidimage_pdf_tt_vm_init(&vm, 1, 0, NULL, 0, 0));
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
    printf("TrueType VM foundations: %u checks, 0 failures\n", checks);
    return 0;
}
