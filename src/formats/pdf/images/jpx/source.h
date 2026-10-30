#ifndef CUPIDIMAGE_PDF_JPX_SOURCE_H
#define CUPIDIMAGE_PDF_JPX_SOURCE_H

#include "../../pdf_render_internal.h"
#include "file.h"

typedef struct {
    pdf_jpx_file file;
    unsigned char *encoded;
    int present;
} pdf_jpx_source;

/* A successful absent result leaves a zero source for the packed-image path. */
int cupidimage_pdf_jpx_source_load(const pdf_render_state *, const pdf_object_view *,
                                   pdf_jpx_source *);
int cupidimage_pdf_jpx_color_space(const pdf_render_state *, const pdf_jpx_source *,
                                   pdf_image_colors *);
void cupidimage_pdf_jpx_source_free(pdf_jpx_source *);

#endif
