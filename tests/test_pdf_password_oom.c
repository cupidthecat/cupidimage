#include "fault_alloc.h"
#include <assert.h>

#define malloc fault_malloc
#define free fault_free
#include "../src/formats/pdf/security/password.c"
#undef malloc
#undef free

int main(void) {
    const char *inputs[] = {"", "password", "\xc2\xad", "A\xcc\x8a", "\xd8\xa7\x61", "\xff"};
    unsigned checked = 0;
    for (unsigned revision = 2; revision <= 6; revision++) {
        for (unsigned i = 0; i < sizeof(inputs) / sizeof(*inputs); i++) {
            unsigned char reference[127], result[127];
            size_t reference_length = 0, length = 0;
            fail_at = allocation_count = 0;
            int expected = cupidimage_pdf_security_password(
                revision, (const unsigned char *)inputs[i], strlen(inputs[i]), 0, reference,
                &reference_length);
            size_t allocations = allocation_count;
            assert(!live_count);
            for (size_t failure = 1; failure <= allocations + 1; failure++) {
                fail_at = failure;
                allocation_count = 0;
                int ok =
                    cupidimage_pdf_security_password(revision, (const unsigned char *)inputs[i],
                                                     strlen(inputs[i]), 0, result, &length);
                assert(!live_count);
                if (ok) {
                    assert(expected && length == reference_length);
                    assert(!memcmp(result, reference, length));
                } else
                    assert(!length);
                if (failure > allocations)
                    assert(ok == expected);
                checked++;
            }
        }
    }
    (void)fault_calloc;
    (void)fault_realloc;
    printf("PDF password preparation: %u allocation checks\n", checked);
    return 0;
}
