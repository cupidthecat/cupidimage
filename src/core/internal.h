#ifndef CUPIDIMAGE_INTERNAL_H
#define CUPIDIMAGE_INTERNAL_H

#include "cupidimage.h"

#include <stddef.h>

typedef int (*cupidimage_image_loader_fn)(const unsigned char *data, size_t size,
                                          cupidimage_image *out, char *err, size_t errcap);
typedef int (*cupidimage_animation_loader_fn)(const unsigned char *data, size_t size,
                                              cupidimage_animation *out, char *err, size_t errcap);

void cupidimage_set_err(char *err, size_t errcap, const char *msg);

int cupidimage_read_file_bytes(const char *path, unsigned char **data, size_t *size, char *err,
                               size_t errcap);

int cupidimage_load_image_file_via_memory(const char *path, cupidimage_image *out, char *err,
                                          size_t errcap, cupidimage_image_loader_fn loader);

int cupidimage_load_animation_file_via_memory(const char *path, cupidimage_animation *out,
                                              char *err, size_t errcap,
                                              cupidimage_animation_loader_fn loader);

/* Decode bounded DCT samples without converting native components to RGBA. */
int cupidimage_jpeg_decode_samples(const unsigned char *data, size_t size, int width, int height,
                                   int components, int color_transform, unsigned char **samples);

/* Deflate compression for Kitty protocol (zlib format) */
int cupidimage_deflate_compress(const uint8_t *data, size_t size, uint8_t *out, size_t outcap,
                                size_t *outlen);

/* Decode one complete zlib stream, checking its header and Adler-32. */
int cupidimage_inflate_zlib_prefix(const uint8_t *data, size_t size, uint8_t *out, size_t outcap,
                                   size_t *outlen, size_t *consumed);
int cupidimage_inflate_zlib(const uint8_t *data, size_t size, uint8_t *out, size_t outcap,
                            size_t *outlen);

#endif
