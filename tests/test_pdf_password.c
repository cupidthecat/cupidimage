#include "formats/pdf/security/password.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    unsigned revision, raw;
    char text[8195];
    unsigned char input[4097], output[127];
    while (scanf("%u %u %8194s", &revision, &raw, text) == 3) {
        size_t length = !strcmp(text, "-") ? 0 : strlen(text) / 2;
        assert(length <= sizeof(input));
        for (size_t i = 0; i < length; i++) {
            unsigned byte;
            assert(sscanf(text + i * 2, "%2x", &byte) == 1);
            input[i] = (unsigned char)byte;
        }
        size_t used = 999;
        if (!cupidimage_pdf_security_password(revision, input, length, (int)raw, output, &used)) {
            assert(used == 0);
            puts("invalid");
        } else {
            assert(used <= sizeof(output));
            for (size_t i = 0; i < used; i++)
                printf("%02x", output[i]);
            putchar('\n');
        }
    }
    return ferror(stdin) ? 1 : 0;
}
