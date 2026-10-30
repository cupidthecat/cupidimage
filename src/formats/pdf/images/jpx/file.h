#ifndef CUPIDIMAGE_PDF_JPX_FILE_H
#define CUPIDIMAGE_PDF_JPX_FILE_H

#include "jpx.h"

#define PDF_JPX_COLORS 16
#define PDF_JPX_DEFINITIONS 256
#define PDF_JPX_SHARED 32

typedef struct {
    const unsigned char *data;
    size_t length;
} pdf_jpx_slice;

typedef struct {
    unsigned method, approximation;
    int precedence;
    uint32_t enumeration;
    pdf_jpx_slice parameters;
} pdf_jpx_color;

typedef struct {
    uint16_t component;
    unsigned char type, column;
} pdf_jpx_channel;

typedef struct {
    uint16_t channel, type, association;
} pdf_jpx_definition;

typedef struct {
    pdf_jpx_slice data;
    unsigned entries, columns, row_bytes;
    unsigned offsets[256];
    unsigned char bits[256];
} pdf_jpx_palette;

typedef struct {
    uint32_t width, height;
    unsigned components, common_bits, bits_count, has_image_header;
    unsigned char bits[PDF_JPX_COMPONENTS];
    pdf_jpx_palette palette;
    pdf_jpx_channel mapping[PDF_JPX_COMPONENTS];
    unsigned mapping_count, has_definitions, definition_count, color_count;
    pdf_jpx_definition definitions[PDF_JPX_DEFINITIONS];
    pdf_jpx_color colors[PDF_JPX_COLORS];
    pdf_jpx_slice opacity;
} pdf_jpx_header;

typedef struct {
    pdf_jpx_samples image;
    pdf_jpx_header header;
    unsigned channels, ordinary, opacity_channels;
    uint16_t color_order[PDF_JPX_COMPONENTS], alpha[PDF_JPX_COMPONENTS];
    unsigned char alpha_type[PDF_JPX_COMPONENTS];
    int chroma_key;
    uint64_t keys[PDF_JPX_COMPONENTS];
    unsigned char *codestream_owned, *shared[PDF_JPX_SHARED];
    unsigned shared_count;
    size_t shared_bytes;
    int boxed, extended;
} pdf_jpx_file;

/* Metadata slices borrow the encoded input until file_free. */
int cupidimage_pdf_jpx_file_decode(const unsigned char *, size_t, pdf_jpx_file *, size_t *, char *,
                                   size_t);
void cupidimage_pdf_jpx_file_free(pdf_jpx_file *);
int cupidimage_pdf_jpx_channel_sample(const pdf_jpx_file *, unsigned, unsigned, unsigned,
                                      uint64_t *, unsigned *, int *);

#endif
