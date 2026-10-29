#include <limits.h>
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

int main(int argc, char **argv) {
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
    static const unsigned char unsupported[] = {0x62};
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
    printf("TrueType VM foundations: %u checks, 0 failures\n", checks);
    return 0;
}
