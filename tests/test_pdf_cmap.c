#include "formats/pdf/fonts/cmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 3)
        return 1;
    FILE *file = fopen(argv[1], "rb");
    if (!file)
        return 1;
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return 1;
    }
    long length = ftell(file);
    if (length < 0 || length > 20 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return 1;
    }
    unsigned char *data = malloc((size_t)length + 1);
    if (!data) {
        fclose(file);
        return 1;
    }
    int ok = fread(data, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    pdf_object_view view = {0};
    pdf_cmap *map = NULL;
    ok = ok && cupidimage_pdf_get_object_content(data, (size_t)length, 10, &view) &&
         cupidimage_pdf_cmap_load(data, (size_t)length, &view, &map);
    cupidimage_pdf_object_view_release(&view);
    if (!ok) {
        free(data);
        return 2;
    }
    printf("mode %d\n", cupidimage_pdf_cmap_vertical(map));
    size_t n = strlen(argv[2]);
    if (n % 2 || n > 16384) {
        cupidimage_pdf_cmap_free(map);
        free(data);
        return 1;
    }
    unsigned char bytes[8192];
    for (size_t i = 0; i < n / 2; i++) {
        char pair[3] = {argv[2][i * 2], argv[2][i * 2 + 1], 0};
        bytes[i] = (unsigned char)strtoul(pair, NULL, 16);
    }
    size_t pos = 0, work = argc > 3 ? (size_t)strtoull(argv[3], NULL, 10) : 256u * 1024u * 1024u;
    while (pos < n / 2) {
        size_t used;
        uint32_t source;
        unsigned cid, notdef;
        int valid;
        if (!cupidimage_pdf_cmap_next(map, bytes + pos, n / 2 - pos, &used, &source, &cid, &notdef,
                                      &valid, &work)) {
            ok = 0;
            break;
        }
        printf("%zu %u %u %u %d\n", used, source, cid, notdef, valid);
        pos += used;
    }
    cupidimage_pdf_cmap_free(map);
    free(data);
    return ok ? 0 : 3;
}
