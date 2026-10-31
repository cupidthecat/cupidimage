#ifndef CUPIDIMAGE_PDF_OPTIONAL_CONTENT_H
#define CUPIDIMAGE_PDF_OPTIONAL_CONTENT_H
#include "../pdf_internal.h"

typedef struct {
    pdf_object_id object;
    unsigned enabled, applicable;
} pdf_optional_group;

typedef struct {
    const unsigned char *document;
    size_t document_size, work_left;
    const pdf_xref *xref;
    pdf_optional_group *groups;
    unsigned count, capacity;
    char (*intents)[128];
    unsigned intent_count;
    int explicit_intents;
} pdf_optional_content;

int cupidimage_pdf_optional_init(pdf_optional_content *, const unsigned char *, size_t,
                                 const pdf_xref *);
void cupidimage_pdf_optional_free(pdf_optional_content *);
/* Evaluate a group or membership value. Other property dictionaries have no effect. */
int cupidimage_pdf_optional_visible(pdf_optional_content *, const pdf_object_view *, pdf_object_id,
                                    int *);
#endif
