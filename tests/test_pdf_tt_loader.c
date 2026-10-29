#include "fault_alloc.h"
#include "fixtures/pdf-truetype.inc"
#include <limits.h>
#include <stdint.h>
#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/formats/pdf/fonts/truetype.c"
#include "../src/formats/pdf/fonts/truetype_components.c"
#include "../src/formats/pdf/fonts/truetype_hint/composite.c"
#include "../src/formats/pdf/fonts/truetype_hint/load.c"
#include "../src/formats/pdf/fonts/truetype_hint/metrics.c"
#include "../src/formats/pdf/fonts/truetype_hint/prepare.c"
#include "../src/formats/pdf/fonts/truetype_hint/simple.c"
#include "../src/formats/pdf/fonts/truetype_vm/contours.c"
#include "../src/formats/pdf/fonts/truetype_vm/design.c"
#include "../src/formats/pdf/fonts/truetype_vm/direct.c"
#include "../src/formats/pdf/fonts/truetype_vm/graphics.c"
#include "../src/formats/pdf/fonts/truetype_vm/interpolate.c"
#include "../src/formats/pdf/fonts/truetype_vm/lifecycle.c"
#include "../src/formats/pdf/fonts/truetype_vm/movement.c"
#include "../src/formats/pdf/fonts/truetype_vm/numeric.c"
#include "../src/formats/pdf/fonts/truetype_vm/points.c"
#include "../src/formats/pdf/fonts/truetype_vm/relative.c"
#include "../src/formats/pdf/fonts/truetype_vm/size.c"
#include "../src/formats/pdf/fonts/truetype_vm/stack.c"
#include "../src/formats/pdf/fonts/truetype_vm/vector.c"
#include "../src/formats/pdf/fonts/truetype_vm/vm.c"
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

int main(void) {
    simple_loader_tests();
    composite_loader_tests();
    printf("TrueType loader aggregate: %u checks, 0 failures\n", checks);
    return 0;
}
