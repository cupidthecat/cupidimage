#ifndef CUPIDIMAGE_CCITT_H
#define CUPIDIMAGE_CCITT_H
#include <stddef.h>

typedef struct {
    int k, columns, rows, end_of_line, byte_align, end_of_block, black_is_one, damaged_rows;
} cupidimage_ccitt_options;

int cupidimage_ccitt_decode(const unsigned char *data, size_t length,
                            const cupidimage_ccitt_options *options, unsigned char **output,
                            size_t *output_length);
#endif
