#ifndef CUPIDIMAGE_TEST_PDF_PROBE_OPTIONS_H
#define CUPIDIMAGE_TEST_PDF_PROBE_OPTIONS_H
#include "cupidimage.h"
#include <stdio.h>
#include <string.h>

static int pdf_probe_capabilities(void) {
    return fputs("{\"schema\":1,\"pdf_options\":[\"--pdf-text-aa\",\"--pdf-tt-hinting\","
                 "\"--pdf-cff-hinting\",\"--pdf-type1-hinting\"]}\n",
                 stdout) >= 0;
}

static int pdf_probe_options(int argc, char **argv, int first, cupidimage_pdf_options *options) {
    for (int i = first; i < argc; i++) {
        if (!strcmp(argv[i], "--pdf-text-aa"))
            options->text_antialias = 1;
        else if (!strcmp(argv[i], "--pdf-tt-hinting"))
            options->truetype_hinting = 1;
        else if (!strcmp(argv[i], "--pdf-cff-hinting"))
            options->cff_hinting = 1;
        else if (!strcmp(argv[i], "--pdf-type1-hinting"))
            options->type1_hinting = 1;
        else
            return 0;
    }
    return 1;
}
#endif
