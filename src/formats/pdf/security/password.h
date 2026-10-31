#ifndef CUPIDIMAGE_PDF_SECURITY_PASSWORD_H
#define CUPIDIMAGE_PDF_SECURITY_PASSWORD_H

#include <stddef.h>

/* UTF-8 is validated before preparation. Raw legacy bytes apply only to R2-4.
 * The output is already truncated to the revision's byte limit. */
int cupidimage_pdf_security_password(unsigned revision, const unsigned char *input, size_t length,
                                     int raw_legacy, unsigned char output[127],
                                     size_t *output_length);

#endif
