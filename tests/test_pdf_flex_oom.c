#include "fault_alloc.h"
#include <assert.h>
#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/formats/pdf/fonts/postscript/private.c"
#include "../src/formats/pdf/fonts/type2.c"
#undef malloc
#undef calloc
#undef realloc
#undef free

int main(void) {
    unsigned checks = 0;
    for (unsigned repeats = 1; repeats <= 64; repeats *= 2) {
        unsigned char program[1024] = {139, 139, 21};
        unsigned length = 3;
        for (unsigned i = 0; i < repeats; i++) {
            const unsigned char flex[] = {159, 159, 151, 159, 159, 159, 159, 12, 34};
            memcpy(program + length, flex, sizeof(flex));
            length += sizeof(flex);
        }
        program[length++] = 14;
        unsigned char offsets[] = {0, 1, (unsigned char)((length + 1) >> 8),
                                   (unsigned char)(length + 1)};
        pdf_cff_private private_dict = {0};
        pdf_cff_font font = {0};
        font.chars = (pdf_cff_index){{offsets, sizeof(offsets)}, {program, length}, 1, 2};
        font.private_dicts = &private_dict;
        font.private_count = 1;
        size_t total = 0;
        for (size_t failure = 0; failure <= total + 1; failure++) {
            fail_at = failure;
            allocation_count = 0;
            pdf_cff_outline outline = {0};
            pdf_ps_hint_program hints = {0};
            size_t work = 100000;
            int ok = cupidimage_pdf_type2_outline_budget(&font, 0, &outline, &hints, &work);
            if (!failure)
                total = allocation_count;
            assert(ok == (!failure || failure > total));
            if (ok) {
                assert(hints.flex_count == repeats);
                for (unsigned i = 0; i < repeats; i++)
                    assert(hints.flexes[i].segment == 1 + i * 2 && hints.flexes[i].depth == 50);
            } else
                assert(!outline.segments && !hints.flexes && !hints.flex_count && !hints.events);
            cupidimage_pdf_type2_free(&outline);
            cupidimage_pdf_ps_hint_program_free(&hints);
            assert(!live_count);
            checks++;
        }
        fail_at = 0;
        for (size_t budget = 0; budget < length; budget++) {
            pdf_cff_outline outline = {0};
            pdf_ps_hint_program hints = {0};
            size_t work = budget;
            int ok = cupidimage_pdf_type2_outline_budget(&font, 0, &outline, &hints, &work);
            assert(work <= budget);
            if (!ok)
                assert(!outline.segments && !hints.flexes && !hints.flex_count);
            cupidimage_pdf_type2_free(&outline);
            cupidimage_pdf_ps_hint_program_free(&hints);
            assert(!live_count);
            checks++;
        }
    }
    (void)fault_calloc;
    printf("Type 2 flex capture: %u allocation and budget checks\n", checks);
    return 0;
}
