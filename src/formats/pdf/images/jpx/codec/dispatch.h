/* Codec dispatch uses the exact generic callback types. Convert the object
 * pointer inside each ordinary call, never by casting a function pointer. */
#ifndef CUPIDIMAGE_PDF_JPX_DISPATCH_H
#define CUPIDIMAGE_PDF_JPX_DISPATCH_H

static void cupidimage_jpx_dispatch_j2k_dump(void *context, OPJ_INT32 flags, FILE *stream) {
    j2k_dump(context, flags, stream);
}

static opj_codestream_info_v2_t *cupidimage_jpx_dispatch_j2k_get_cstr_info(void *context) {
    return j2k_get_cstr_info(context);
}

static opj_codestream_index_t *cupidimage_jpx_dispatch_j2k_get_cstr_index(void *context) {
    return j2k_get_cstr_index(context);
}

static OPJ_BOOL cupidimage_jpx_dispatch_j2k_decode(void *context, struct opj_stream_private *stream,
                                                   opj_image_t *image,
                                                   struct opj_event_mgr *events) {
    return opj_j2k_decode(context, stream, image, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_j2k_end_decompress(void *context,
                                                           struct opj_stream_private *stream,
                                                           struct opj_event_mgr *events) {
    return opj_j2k_end_decompress(context, stream, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_j2k_read_header(struct opj_stream_private *stream,
                                                        void *context, opj_image_t **image,
                                                        struct opj_event_mgr *events) {
    return opj_j2k_read_header(stream, context, image, events);
}

static void cupidimage_jpx_dispatch_j2k_destroy(void *context) {
    opj_j2k_destroy(context);
}

static void cupidimage_jpx_dispatch_j2k_setup_decoder(void *context,
                                                      opj_dparameters_t *parameters) {
    opj_j2k_setup_decoder(context, parameters);
}

static void cupidimage_jpx_dispatch_j2k_decoder_set_strict_mode(void *context, OPJ_BOOL strict) {
    opj_j2k_decoder_set_strict_mode(context, strict);
}

static OPJ_BOOL cupidimage_jpx_dispatch_j2k_read_tile_header(
    void *context, OPJ_UINT32 *tile_index, OPJ_UINT32 *data_size, OPJ_INT32 *x0, OPJ_INT32 *y0,
    OPJ_INT32 *x1, OPJ_INT32 *y1, OPJ_UINT32 *components, OPJ_BOOL *go_on,
    struct opj_stream_private *stream, struct opj_event_mgr *events) {
    return opj_j2k_read_tile_header(context, tile_index, data_size, x0, y0, x1, y1, components,
                                    go_on, stream, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_j2k_decode_tile(void *context, OPJ_UINT32 tile_index,
                                                        OPJ_BYTE *data, OPJ_UINT32 data_size,
                                                        struct opj_stream_private *stream,
                                                        struct opj_event_mgr *events) {
    return opj_j2k_decode_tile(context, tile_index, data, data_size, stream, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_j2k_set_decode_area(void *context, opj_image_t *image,
                                                            OPJ_INT32 start_x, OPJ_INT32 start_y,
                                                            OPJ_INT32 end_x, OPJ_INT32 end_y,
                                                            struct opj_event_mgr *events) {
    return opj_j2k_set_decode_area(context, image, start_x, start_y, end_x, end_y, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_j2k_get_tile(void *context, opj_stream_private_t *stream,
                                                     opj_image_t *image,
                                                     struct opj_event_mgr *events,
                                                     OPJ_UINT32 tile_index) {
    return opj_j2k_get_tile(context, stream, image, events, tile_index);
}

static OPJ_BOOL
cupidimage_jpx_dispatch_j2k_set_decoded_resolution_factor(void *context, OPJ_UINT32 factor,
                                                          struct opj_event_mgr *events) {
    return opj_j2k_set_decoded_resolution_factor(context, factor, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_j2k_set_decoded_components(void *context, OPJ_UINT32 count,
                                                                   const OPJ_UINT32 *indices,
                                                                   struct opj_event_mgr *events) {
    return opj_j2k_set_decoded_components(context, count, indices, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_j2k_set_threads(void *context, OPJ_UINT32 count) {
    return opj_j2k_set_threads(context, count);
}

static void cupidimage_jpx_dispatch_jp2_dump(void *context, OPJ_INT32 flags, FILE *stream) {
    jp2_dump(context, flags, stream);
}

static opj_codestream_info_v2_t *cupidimage_jpx_dispatch_jp2_get_cstr_info(void *context) {
    return jp2_get_cstr_info(context);
}

static opj_codestream_index_t *cupidimage_jpx_dispatch_jp2_get_cstr_index(void *context) {
    return jp2_get_cstr_index(context);
}

static OPJ_BOOL cupidimage_jpx_dispatch_jp2_decode(void *context, struct opj_stream_private *stream,
                                                   opj_image_t *image,
                                                   struct opj_event_mgr *events) {
    return opj_jp2_decode(context, stream, image, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_jp2_end_decompress(void *context,
                                                           struct opj_stream_private *stream,
                                                           struct opj_event_mgr *events) {
    return opj_jp2_end_decompress(context, stream, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_jp2_read_header(struct opj_stream_private *stream,
                                                        void *context, opj_image_t **image,
                                                        struct opj_event_mgr *events) {
    return opj_jp2_read_header(stream, context, image, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_jp2_read_tile_header(
    void *context, OPJ_UINT32 *tile_index, OPJ_UINT32 *data_size, OPJ_INT32 *x0, OPJ_INT32 *y0,
    OPJ_INT32 *x1, OPJ_INT32 *y1, OPJ_UINT32 *components, OPJ_BOOL *go_on,
    struct opj_stream_private *stream, struct opj_event_mgr *events) {
    return opj_jp2_read_tile_header(context, tile_index, data_size, x0, y0, x1, y1, components,
                                    go_on, stream, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_jp2_decode_tile(void *context, OPJ_UINT32 tile_index,
                                                        OPJ_BYTE *data, OPJ_UINT32 data_size,
                                                        struct opj_stream_private *stream,
                                                        struct opj_event_mgr *events) {
    return opj_jp2_decode_tile(context, tile_index, data, data_size, stream, events);
}

static void cupidimage_jpx_dispatch_jp2_destroy(void *context) {
    opj_jp2_destroy(context);
}

static void cupidimage_jpx_dispatch_jp2_setup_decoder(void *context,
                                                      opj_dparameters_t *parameters) {
    opj_jp2_setup_decoder(context, parameters);
}

static void cupidimage_jpx_dispatch_jp2_decoder_set_strict_mode(void *context, OPJ_BOOL strict) {
    opj_jp2_decoder_set_strict_mode(context, strict);
}

static OPJ_BOOL cupidimage_jpx_dispatch_jp2_set_decode_area(void *context, opj_image_t *image,
                                                            OPJ_INT32 start_x, OPJ_INT32 start_y,
                                                            OPJ_INT32 end_x, OPJ_INT32 end_y,
                                                            struct opj_event_mgr *events) {
    return opj_jp2_set_decode_area(context, image, start_x, start_y, end_x, end_y, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_jp2_get_tile(void *context, opj_stream_private_t *stream,
                                                     opj_image_t *image,
                                                     struct opj_event_mgr *events,
                                                     OPJ_UINT32 tile_index) {
    return opj_jp2_get_tile(context, stream, image, events, tile_index);
}

static OPJ_BOOL cupidimage_jpx_dispatch_jp2_set_decoded_resolution_factor(void *context,
                                                                          OPJ_UINT32 factor,
                                                                          opj_event_mgr_t *events) {
    return opj_jp2_set_decoded_resolution_factor(context, factor, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_jp2_set_decoded_components(void *context, OPJ_UINT32 count,
                                                                   const OPJ_UINT32 *indices,
                                                                   struct opj_event_mgr *events) {
    return opj_jp2_set_decoded_components(context, count, indices, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_jp2_set_threads(void *context, OPJ_UINT32 count) {
    return opj_jp2_set_threads(context, count);
}

static OPJ_BOOL cupidimage_jpx_dispatch_j2k_encode(void *context, struct opj_stream_private *stream,
                                                   struct opj_event_mgr *events) {
    return opj_j2k_encode(context, stream, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_j2k_end_compress(void *context,
                                                         struct opj_stream_private *stream,
                                                         struct opj_event_mgr *events) {
    return opj_j2k_end_compress(context, stream, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_j2k_start_compress(void *context,
                                                           struct opj_stream_private *stream,
                                                           struct opj_image *image,
                                                           struct opj_event_mgr *events) {
    return opj_j2k_start_compress(context, stream, image, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_j2k_write_tile(void *context, OPJ_UINT32 tile_index,
                                                       OPJ_BYTE *data, OPJ_UINT32 data_size,
                                                       struct opj_stream_private *stream,
                                                       struct opj_event_mgr *events) {
    return opj_j2k_write_tile(context, tile_index, data, data_size, stream, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_j2k_setup_encoder(void *context,
                                                          opj_cparameters_t *parameters,
                                                          struct opj_image *image,
                                                          struct opj_event_mgr *events) {
    return opj_j2k_setup_encoder(context, parameters, image, events);
}

static OPJ_BOOL
cupidimage_jpx_dispatch_j2k_encoder_set_extra_options(void *context, const char *const *options,
                                                      struct opj_event_mgr *events) {
    return opj_j2k_encoder_set_extra_options(context, options, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_jp2_encode(void *context, struct opj_stream_private *stream,
                                                   struct opj_event_mgr *events) {
    return opj_jp2_encode(context, stream, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_jp2_end_compress(void *context,
                                                         struct opj_stream_private *stream,
                                                         struct opj_event_mgr *events) {
    return opj_jp2_end_compress(context, stream, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_jp2_start_compress(void *context,
                                                           struct opj_stream_private *stream,
                                                           struct opj_image *image,
                                                           struct opj_event_mgr *events) {
    return opj_jp2_start_compress(context, stream, image, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_jp2_write_tile(void *context, OPJ_UINT32 tile_index,
                                                       OPJ_BYTE *data, OPJ_UINT32 data_size,
                                                       struct opj_stream_private *stream,
                                                       struct opj_event_mgr *events) {
    return opj_jp2_write_tile(context, tile_index, data, data_size, stream, events);
}

static OPJ_BOOL cupidimage_jpx_dispatch_jp2_setup_encoder(void *context,
                                                          opj_cparameters_t *parameters,
                                                          struct opj_image *image,
                                                          struct opj_event_mgr *events) {
    return opj_jp2_setup_encoder(context, parameters, image, events);
}

static OPJ_BOOL
cupidimage_jpx_dispatch_jp2_encoder_set_extra_options(void *context, const char *const *options,
                                                      struct opj_event_mgr *events) {
    return opj_jp2_encoder_set_extra_options(context, options, events);
}

#endif
