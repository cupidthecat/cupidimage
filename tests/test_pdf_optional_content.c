#include "fault_alloc.h"
#include "formats/pdf/pdf_internal.h"
#define malloc fault_malloc
#define calloc fault_calloc
#define realloc fault_realloc
#define free fault_free
#include "../src/formats/pdf/optional_content/evaluate.c"
#include "../src/formats/pdf/optional_content/state.c"
#include "../src/formats/pdf/optional_content/values.c"
#undef malloc
#undef calloc
#undef realloc
#undef free

static int evaluate(const unsigned char *data, size_t size, const pdf_xref *xref, int count,
                    char **objects, int output) {
    pdf_optional_content store;
    if (!cupidimage_pdf_optional_init(&store, data, size, xref))
        return 0;
    int ok = 1;
    for (int i = 0; ok && i < count; i++) {
        char *end;
        unsigned long number = strtoul(objects[i], &end, 10);
        pdf_object_view value = {0};
        pdf_object_id object = PDF_OBJECT_ID(number, 0);
        int visible = -1;
        ok = *objects[i] && !*end && number <= 0xffffffffu &&
             cupidimage_pdf_lookup_object(xref, data, size, object, &value) == 1 &&
             cupidimage_pdf_optional_visible(&store, &value, object, &visible);
        if (ok && output)
            printf("%lu %d\n", number, visible);
        cupidimage_pdf_object_view_release(&value);
    }
    store.work_left = 0;
    pdf_object_view empty = {(const unsigned char *)"<< >>", 5, NULL, xref};
    int ignored;
    if (cupidimage_pdf_optional_visible(&store, &empty, 0, &ignored))
        ok = 0;
    cupidimage_pdf_optional_free(&store);
    return ok;
}

int main(int argc, char **argv) {
    (void)fault_calloc;
    int faults = argc > 1 && !strcmp(argv[1], "--faults");
    if (argc < 3 + faults)
        return 2;
    unsigned char *data = NULL;
    size_t size = 0, eof = 0, offset = 0;
    char error[256] = {0};
    pdf_xref xref = {0};
    int ok = cupidimage_read_file_bytes(argv[1 + faults], &data, &size, error, sizeof(error)) &&
             cupidimage_pdf_find_eof_marker(data, size, &eof) &&
             cupidimage_pdf_find_startxref(data, eof, &offset) &&
             cupidimage_pdf_parse_xref_table(data, size, offset, &xref, error, sizeof(error)) &&
             cupidimage_pdf_parse_trailer(data, size, offset, &xref, error, sizeof(error));
    size_t checks = 0, total = 0;
    if (ok) {
        for (size_t fail = 0; fail <= total; fail++) {
            fail_at = fail;
            allocation_count = 0;
            int result =
                evaluate(data, size, &xref, argc - 2 - faults, argv + 2 + faults, !faults && !fail);
            if (!fail) {
                ok = result;
                if (faults)
                    total = allocation_count;
            }
            if (live_count || (fail && result)) {
                fprintf(stderr, "optional-content ownership failure at allocation %zu\n", fail);
                ok = 0;
            }
            checks++;
        }
    }
    if (faults && ok)
        printf("%zu allocation checks\n", checks);
    free(xref.entries);
    free(data);
    return ok ? 0 : 3;
}
