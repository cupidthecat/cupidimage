#include "formats/pdf/security/authenticate.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t unhex(const char *text, unsigned char *bytes, size_t capacity) {
    if (!strcmp(text, "-"))
        return 0;
    size_t length = strlen(text);
    assert(!(length % 2) && length / 2 <= capacity);
    for (size_t i = 0; i < length / 2; i++) {
        unsigned value;
        assert(sscanf(text + i * 2, "%2x", &value) == 1);
        bytes[i] = (unsigned char)value;
    }
    return length / 2;
}

int main(void) {
    unsigned revision, key_length, permissions;
    int metadata;
    char password[1025], identifier[1025], owner[97], user[97], oe[65], ue[65], perms[33];
    while (scanf("%u %u %u %d %1024s %1024s %96s %96s %64s %64s %32s", &revision, &key_length,
                 &permissions, &metadata, password, identifier, owner, user, oe, ue, perms) == 11) {
        pdf_security_parameters p = {0};
        unsigned char input[512], id[512], key[32];
        p.revision = revision;
        p.key_length = key_length;
        p.permissions = permissions;
        p.encrypt_metadata = metadata;
        size_t length = unhex(password, input, sizeof(input));
        p.identifier = (pdf_crypto_slice){id, unhex(identifier, id, sizeof(id))};
        assert(unhex(owner, p.owner, sizeof(p.owner)) == (revision <= 4 ? 32 : 48));
        assert(unhex(user, p.user, sizeof(p.user)) == (revision <= 4 ? 32 : 48));
        unhex(oe, p.owner_key, sizeof(p.owner_key));
        unhex(ue, p.user_key, sizeof(p.user_key));
        unhex(perms, p.perms, sizeof(p.perms));
        int is_owner = -1;
        int valid = cupidimage_pdf_security_authenticate(&p, input, length, key, &is_owner);
        if (!valid) {
            assert(is_owner == 0);
            for (unsigned i = 0; i < sizeof(key); i++)
                assert(key[i] == 0);
            puts("invalid");
        } else {
            printf("%d ", is_owner);
            for (unsigned i = 0; i < key_length; i++)
                printf("%02x", key[i]);
            putchar('\n');
        }
    }
    return ferror(stdin) ? 1 : 0;
}
