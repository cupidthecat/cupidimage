#ifndef CUPIDIMAGE_PDF_MONOCHROME_ENGINE_H
#define CUPIDIMAGE_PDF_MONOCHROME_ENGINE_H
#include <stddef.h>
struct FT_Outline_;

/* No allocation or retained state; the caller owns the outline and packed bitmap. */
int cupidimage_pdf_monochrome_engine(const struct FT_Outline_ *outline, unsigned char *bitmap,
                                     unsigned width, unsigned height, long *pool, size_t pool_words,
                                     size_t *work);
#endif
