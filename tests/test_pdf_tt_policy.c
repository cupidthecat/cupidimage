#include "../src/formats/pdf/fonts/truetype_hint/policy.c"
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(x)) {                                                                                \
            fprintf(stderr, "TT profile metadata line %d\n", __LINE__);                            \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static void policy_write16(unsigned char *p, unsigned n) {
    p[0] = (unsigned char)(n >> 8);
    p[1] = (unsigned char)n;
}

static size_t policy_name(unsigned char *out, const char *name, unsigned platform,
                          unsigned encoding, unsigned language, unsigned id) {
    size_t length = strlen(name), wide = platform != 1, bytes = length * (wide ? 2 : 1);
    memset(out, 0, 18 + bytes);
    policy_write16(out + 2, 1);
    policy_write16(out + 4, 18);
    policy_write16(out + 6, platform);
    policy_write16(out + 8, encoding);
    policy_write16(out + 10, language);
    policy_write16(out + 12, id);
    policy_write16(out + 14, (unsigned)bytes);
    for (size_t i = 0; i < length; i++)
        out[18 + i * (wide ? 2 : 1) + (wide ? 1 : 0)] = (unsigned char)name[i];
    return 18 + bytes;
}

int main(int argc, char **argv) {
    if (argc == 2) {
        FILE *file = fopen(argv[1], "rb");
        if (!file)
            return 2;
        if (fseek(file, 0, SEEK_END)) {
            fclose(file);
            return 2;
        }
        long length = ftell(file);
        if (length < 0 || length > 32 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
            fclose(file);
            return 2;
        }
        unsigned char *data = malloc((size_t)length + 1);
        if (!data) {
            fclose(file);
            return 2;
        }
        int ok = fread(data, 1, (size_t)length, file) == (size_t)length;
        fclose(file);
        pdf_tt_font font;
        int required = 0;
        size_t work = 1048576;
        ok = ok && cupidimage_pdf_tt_init(&font, data, (size_t)length) &&
             cupidimage_pdf_tt_full_hinting(&font, &work, &required);
        if (ok)
            printf("%d %zu\n", required, 1048576 - work);
        free(data);
        return ok ? 0 : 3;
    }
    unsigned char data[512];
    pdf_tt_font font = {0};
    size_t work;
    int required;
    for (unsigned i = 0; i < sizeof(tt_full_hinting_names) / sizeof(*tt_full_hinting_names); i++) {
        for (unsigned wide = 0; wide < 2; wide++) {
            char name[100];
            snprintf(name, sizeof(name), "ABCDEF+Owned %s fixture", tt_full_hinting_names[i]);
            font.name = (pdf_tt_table){
                data, policy_name(data, name, wide ? 3 : 1, wide ? 1 : 0, wide ? 0x409 : 0, 1)};
            work = 1048576;
            CHECK(cupidimage_pdf_tt_full_hinting(&font, &work, &required) && required);
            size_t used = 1048576 - work;
            CHECK(used > 0);
            work = used - 1;
            CHECK(!cupidimage_pdf_tt_full_hinting(&font, &work, &required));
            work = used;
            CHECK(cupidimage_pdf_tt_full_hinting(&font, &work, &required) && required && work == 0);
        }
    }
    const char *plain[] = {"Ordinary", "DLCDEF+Ordinary", "DFKai", "dfkai-sb", ""};
    for (unsigned i = 0; i < sizeof(plain) / sizeof(*plain); i++) {
        font.name = (pdf_tt_table){data, policy_name(data, plain[i], 3, 1, 0x409, 1)};
        work = 1048576;
        CHECK(cupidimage_pdf_tt_full_hinting(&font, &work, &required) && !required);
    }
    font.name = (pdf_tt_table){data, policy_name(data, "HuaTianKaiTi\xff", 1, 0, 0, 1)};
    work = 1048576;
    CHECK(cupidimage_pdf_tt_full_hinting(&font, &work, &required) && required);
    for (unsigned size = 0; size < 18; size++) {
        font.name = (pdf_tt_table){data, size};
        work = 1048576;
        CHECK(cupidimage_pdf_tt_full_hinting(&font, &work, &required) && !required);
    }
    font.name = (pdf_tt_table){0};
    for (unsigned row = 0; row < sizeof(tt_full_hinting_ids) / sizeof(*tt_full_hinting_ids);
         row++) {
        unsigned char *buffers[3] = {0};
        pdf_tt_table *tables[] = {&font.cvt, &font.fpgm, &font.prep};
        for (unsigned j = 0; j < 3; j++) {
            const tt_program_id *id = &tt_full_hinting_ids[row][j];
            buffers[j] = calloc((size_t)id->length + 4, 1);
            CHECK(buffers[j]);
            if (id->length) {
                CHECK(id->length >= 4);
                for (unsigned k = 0; k < 4; k++)
                    buffers[j][k] = (unsigned char)(id->checksum >> (24 - k * 8));
            }
            buffers[j][id->length] = 255;
            *tables[j] = (pdf_tt_table){buffers[j], id->length};
        }
        work = 1048576;
        CHECK(cupidimage_pdf_tt_full_hinting(&font, &work, &required) && required);
        buffers[1][0] ^= 1;
        work = 1048576;
        CHECK(cupidimage_pdf_tt_full_hinting(&font, &work, &required) && !required);
        for (unsigned j = 0; j < 3; j++) {
            free(buffers[j]);
            *tables[j] = (pdf_tt_table){0};
        }
    }
    work = 0;
    CHECK(!cupidimage_pdf_tt_full_hinting(&font, &work, &required));
    CHECK(!cupidimage_pdf_tt_full_hinting(NULL, &work, &required));
    CHECK(!cupidimage_pdf_tt_full_hinting(&font, NULL, &required));
    CHECK(!cupidimage_pdf_tt_full_hinting(&font, &work, NULL));
    printf("TT profile metadata: %u checks\n", checks);
    return 0;
}
