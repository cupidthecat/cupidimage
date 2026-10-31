#ifndef CUPIDIMAGE_CCITT_H
#define CUPIDIMAGE_CCITT_H
#include <stddef.h>

typedef struct {
    int k, columns, rows, end_of_line, byte_align, end_of_block, black_is_one, damaged_rows;
} cupidimage_ccitt_options;

int cupidimage_ccitt_decode(const unsigned char *data, size_t length,
                            const cupidimage_ccitt_options *options, unsigned char **output,
                            size_t *output_length);
/* A bounded prefix decoder for embedded MMR bitmaps. The caller budget is
 * debited on success and failure; consumed_bits is set only on success. */
int cupidimage_ccitt_decode_bounded(const unsigned char *data, size_t length,
                                    const cupidimage_ccitt_options *options, unsigned char **output,
                                    size_t *output_length, size_t output_limit, size_t *work_left,
                                    int mmr_only, size_t *consumed_bits);
#endif
