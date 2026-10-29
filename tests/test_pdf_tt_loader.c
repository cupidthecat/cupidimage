#include "fault_alloc.h"
#include "fixtures/pdf-truetype.inc"
#include <limits.h>
#include <math.h>
#include <stdint.h>
#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/formats/pdf/fonts/truetype.c"
#include "pdf_tt_loader_sources.inc"
#undef malloc
#undef calloc
#undef realloc
#undef free

static unsigned checks;
#define CHECK(value)                                                                               \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(value)) {                                                                            \
            fprintf(stderr, "TT loader check failed at %d: %s\n", __LINE__, #value);               \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

static void finish(pdf_tt_hint *hint) {
    cupidimage_pdf_tt_hint_free(hint);
    CHECK(live_count == 0);
}

static pdf_tt_font fixture_font(void) {
    pdf_tt_font font;
    CHECK(cupidimage_pdf_tt_init(&font, test_truetype_font, sizeof(test_truetype_font)));
    return font;
}

static void write16(unsigned char *p, unsigned value) {
    p[0] = (unsigned char)(value >> 8);
    p[1] = (unsigned char)value;
}

#include "pdf_tt_loader/composites.c"
#include "pdf_tt_loader/simple.c"
#include "pdf_tt_loader/size_xy.c"
#include "pdf_tt_loader/transform.c"

int main(void) {
    simple_loader_tests();
    composite_loader_tests();
    nonuniform_loader_tests();
    device_shape_tests();
    printf("TrueType loader aggregate: %u checks, 0 failures\n", checks);
    return 0;
}
