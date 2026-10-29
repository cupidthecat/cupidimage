#ifndef CUPIDIMAGE_PDF_INTERNAL_H
#define CUPIDIMAGE_PDF_INTERNAL_H

#include "core/internal.h"
#include "cupidimage.h"

typedef uint64_t pdf_object_id;
#define PDF_OBJECT_ID(number, generation)                                                          \
    ((pdf_object_id)(uint32_t)(number) | ((pdf_object_id)(uint32_t)(generation) << 32))
#define PDF_OBJECT_NUMBER(object) ((uint32_t)(object))
#define PDF_OBJECT_GENERATION(object) ((uint32_t)((object) >> 32))

typedef struct {
    int obj_num;
    int gen_num;
    size_t offset;
    unsigned kind, index, order;
} pdf_xref_entry;

typedef struct pdf_xref {
    pdf_xref_entry *entries;
    int count;
    pdf_object_id root_obj;
    pdf_object_id info_obj;
    const unsigned char *data;
    size_t size, object_limit;
} pdf_xref;

typedef struct {
    const unsigned char *data;
    size_t len;
    unsigned char *owned;
    const pdf_xref *xref;
} pdf_object_view;

typedef struct {
    double x0, y0, x1, y1;
    int rotation;
    int width, height;
    double user_unit;
} pdf_page_geometry;

typedef struct {
    uint32_t src_code;
    uint32_t dst_codepoint;
    uint32_t order;
    uint8_t src_len;
} pdf_tounicode_entry;

typedef struct {
    double matrix[6], bounds[4], widths[256];
    pdf_object_id glyphs[256];
    pdf_object_view dictionary;
} pdf_type3_font;

typedef struct {
    double width, advance_y, origin_x, origin_y;
    unsigned flags;
} pdf_cid_metric;
typedef struct pdf_cmap pdf_cmap;

typedef struct {
    pdf_cmap *encoding;
    int vertical;
    double default_width, default_advance_y, default_origin_y;
    pdf_cid_metric *metrics;
} pdf_cid_font;

typedef struct pdf_font_program pdf_font_program;

typedef struct {
    char name[128];
    pdf_object_id graphics_object;
    pdf_tounicode_entry *entries;
    int count;
    int cap;
    int max_src_len;
    int has_widths, has_missing_width, first_char, last_char;
    double widths[256], missing_width;
    pdf_type3_font *type3;
    pdf_cid_font *cid;
    pdf_font_program *program;
} pdf_font_map;

typedef struct {
    size_t cid_bytes, program_bytes;
    pdf_font_map *fonts;
    int count;
    int cap;
} pdf_font_table;

extern const uint8_t cupidimage_pdf_font8x8_basic[128][8];

int cupidimage_pdf_buffer_contains(const unsigned char *data, size_t start, size_t end,
                                   const char *needle);
int cupidimage_pdf_extract_dict_ref(const unsigned char *data, size_t start, size_t end,
                                    const char *key, pdf_object_id *obj_num);
int cupidimage_pdf_extract_stream_from_object(const unsigned char *obj_data, size_t obj_len,
                                              const unsigned char **stream_data, size_t *stream_len,
                                              size_t *stream_keyword_pos);
int cupidimage_pdf_find_eof_marker(const unsigned char *data, size_t size, size_t *eof_pos);
int cupidimage_pdf_find_startxref(const unsigned char *data, size_t eof_pos, size_t *xref_offset);
int cupidimage_pdf_object_header(const unsigned char *data, size_t size, size_t pos,
                                 pdf_object_id *object, size_t *end);
int cupidimage_pdf_object_end(const unsigned char *data, size_t size, size_t start, size_t *end);
/* Lookup distinguishes an absent reference (0) from malformed data or exhaustion (-1). */
int cupidimage_pdf_lookup_object(const pdf_xref *xref, const unsigned char *data, size_t size,
                                 pdf_object_id object, pdf_object_view *view);
int cupidimage_pdf_view_object(const unsigned char *data, size_t size,
                               const pdf_object_view *parent, pdf_object_id object,
                               pdf_object_view *view);
int cupidimage_pdf_scan_object(const unsigned char *data, size_t size, pdf_object_id object,
                               pdf_object_view *view);
int cupidimage_pdf_get_object_content(const unsigned char *data, size_t size, pdf_object_id obj_num,
                                      pdf_object_view *view);
int cupidimage_pdf_get_page_content_stream(const unsigned char *data, size_t size, pdf_xref *xref,
                                           pdf_object_id page_obj, unsigned char **stream_data,
                                           size_t *stream_len, int *is_flate, char *err,
                                           size_t errcap);
int cupidimage_pdf_get_page_geometry(const unsigned char *data, size_t size, pdf_object_id page_obj,
                                     pdf_page_geometry *geometry, char *err, size_t errcap);
int cupidimage_pdf_get_page_obj_by_index(const unsigned char *data, size_t size, pdf_xref *xref,
                                         pdf_object_id pages_obj, int page_index,
                                         pdf_object_id *page_obj, char *err, size_t errcap);
int cupidimage_pdf_get_pages_count_from_obj(const unsigned char *data, size_t size,
                                            pdf_object_id pages_obj, int *count, char *err,
                                            size_t errcap);
int cupidimage_pdf_get_pages_obj(const unsigned char *data, size_t size, pdf_xref *xref,
                                 pdf_object_id *pages_obj, char *err, size_t errcap);
int cupidimage_pdf_parse_pdf_number(const unsigned char *data, size_t end, size_t *pos,
                                    double *out);
const pdf_xref_entry *cupidimage_pdf_xref_find(const pdf_xref *xref, pdf_object_id object);
int cupidimage_pdf_parse_trailer(const unsigned char *data, size_t size, size_t xref_pos,
                                 pdf_xref *xref, char *err, size_t errcap);
int cupidimage_pdf_parse_xref_table(const unsigned char *data, size_t size, size_t xref_pos,
                                    pdf_xref *xref, char *err, size_t errcap);
int cupidimage_pdf_check_header(const unsigned char *data, size_t size);
int cupidimage_pdf_collect_page_fonts(const unsigned char *data, size_t size,
                                      pdf_object_id page_obj, pdf_font_table *table, char *err,
                                      size_t errcap);
const pdf_font_map *cupidimage_pdf_font_table_find(const pdf_font_table *table, const char *name);
void cupidimage_pdf_font_table_free(pdf_font_table *table);
void cupidimage_pdf_font_table_init(pdf_font_table *table);
int cupidimage_pdf_image_name_is(const pdf_object_view *view, const char *key, const char *name);
int cupidimage_pdf_inflate_auto(const unsigned char *data, size_t size, unsigned char **out_data,
                                size_t *out_len);
int cupidimage_pdf_is_delim(unsigned char c);
void cupidimage_pdf_object_view_release(pdf_object_view *view);
int cupidimage_pdf_page_rectangle(const unsigned char *data, size_t size,
                                  const pdf_object_view *view, size_t pos, double box[4]);
int cupidimage_pdf_parse_reference(const pdf_object_view *view, size_t *cursor,
                                   pdf_object_id *object);
int cupidimage_pdf_page_reference(const pdf_object_view *view, size_t pos, pdf_object_id *object);
int cupidimage_pdf_page_value(const pdf_object_view *view, const char *key, size_t *value_pos);
int cupidimage_pdf_parse_decimal(const unsigned char *data, size_t end, size_t *pos, size_t limit,
                                 size_t *out);
int cupidimage_pdf_render_content_stream(const unsigned char *stream, size_t stream_len,
                                         const unsigned char *document, size_t document_size,
                                         pdf_object_id page_object,
                                         const pdf_font_table *font_table, cupidimage_image *img,
                                         const pdf_page_geometry *geometry, char *err,
                                         size_t errcap);
int cupidimage_pdf_render_content_stream_indexed(const unsigned char *stream, size_t stream_len,
                                                 const unsigned char *document,
                                                 size_t document_size, pdf_object_id page_object,
                                                 const pdf_font_table *font_table,
                                                 cupidimage_image *img,
                                                 const pdf_page_geometry *geometry, char *err,
                                                 size_t errcap, const pdf_xref *xref);
int cupidimage_pdf_render_content_stream_options(
    const unsigned char *stream, size_t stream_len, const unsigned char *document,
    size_t document_size, pdf_object_id page_object, const pdf_font_table *font_table,
    cupidimage_image *img, const pdf_page_geometry *geometry, char *err, size_t errcap,
    const pdf_xref *xref, const cupidimage_pdf_options *opts);
int cupidimage_pdf_get_pages_count_indexed(const unsigned char *data, size_t size,
                                           pdf_object_id page, int *count, char *error,
                                           size_t capacity, const pdf_xref *xref);
int cupidimage_pdf_get_page_geometry_indexed(const unsigned char *data, size_t size,
                                             pdf_object_id page, pdf_page_geometry *geometry,
                                             char *error, size_t capacity, const pdf_xref *xref);
int cupidimage_pdf_collect_page_fonts_indexed(const unsigned char *data, size_t size,
                                              pdf_object_id page, pdf_font_table *fonts,
                                              char *error, size_t capacity, const pdf_xref *xref);
void cupidimage_pdf_skip_ws_and_comments(const unsigned char *data, size_t size, size_t *pos);

int cupidimage_pdf_is_ws(unsigned char c);
int cupidimage_pdf_dictionary_integer(const unsigned char *document, size_t document_size,
                                      const pdf_object_view *view, const char *key, int *value);
int cupidimage_pdf_object_stream(const unsigned char *document, size_t document_size,
                                 const pdf_object_view *view, const unsigned char **stream,
                                 size_t *length);

typedef struct {
    int width, height, components, bits;
    const unsigned char *document;
    size_t document_size;
} pdf_image_stream_profile;

int cupidimage_pdf_filter_parameter_applies(const char *filter, const char *key);
int cupidimage_pdf_stream_filter_names(const pdf_object_view *view, char filters[8][32],
                                       int *total);
int cupidimage_pdf_decode_direct_image_stream(const pdf_object_view *view,
                                              const unsigned char *data, size_t length,
                                              unsigned char **decoded, size_t *decoded_length,
                                              const pdf_image_stream_profile *profile);
int cupidimage_pdf_decode_image_stream(const pdf_object_view *view, const unsigned char *data,
                                       size_t length, unsigned char **decoded,
                                       size_t *decoded_length,
                                       const pdf_image_stream_profile *profile);

int cupidimage_pdf_decode_direct_stream(const pdf_object_view *view, const unsigned char *data,
                                        size_t length, unsigned char **decoded,
                                        size_t *decoded_length);
int cupidimage_pdf_decode_stream(const pdf_object_view *view, const unsigned char *data,
                                 size_t length, unsigned char **decoded, size_t *decoded_length);

int cupidimage_pdf_dictionary_view(const pdf_object_view *parent, size_t start,
                                   pdf_object_view *view, size_t *end);

int cupidimage_pdf_parse_type3_font(const unsigned char *data, size_t size,
                                    const pdf_object_view *view, pdf_font_map *font);
int cupidimage_pdf_collect_resource_fonts(const unsigned char *data, size_t size,
                                          const pdf_object_view *owner, pdf_font_table *table,
                                          char *error, size_t capacity);

int cupidimage_pdf_font_dictionary_at(const unsigned char *data, size_t size,
                                      const pdf_object_view *parent, size_t pos,
                                      pdf_object_view *out);
int cupidimage_pdf_parse_cid_font(const unsigned char *data, size_t size,
                                  const pdf_object_view *view, pdf_font_map *font, size_t *budget);
void cupidimage_pdf_cid_font_free(pdf_cid_font *font);
size_t cupidimage_pdf_cid_font_bytes(const pdf_cid_font *font);
void cupidimage_pdf_cid_metrics(const pdf_cid_font *font, unsigned cid, double *width,
                                double *advance_y, double *origin_x, double *origin_y);

int cupidimage_pdf_resolve_value(const unsigned char *data, size_t size,
                                 const pdf_object_view *view, size_t pos, pdf_object_view *out,
                                 unsigned depth);
/* Optional values: 0 for absent/null, 1 for a resolved value, -1 for an error. */
int cupidimage_pdf_optional_dictionary(const unsigned char *data, size_t size,
                                       const pdf_object_view *parent, const char *key,
                                       pdf_object_view *out);
int cupidimage_pdf_resolved_number(const unsigned char *data, size_t size,
                                   const pdf_object_view *view, size_t *pos, double *number);
int cupidimage_pdf_optional_value(const unsigned char *data, size_t size,
                                  const pdf_object_view *view, const char *key,
                                  pdf_object_view *out);
int cupidimage_pdf_dictionary_value(const unsigned char *data, size_t size,
                                    const pdf_object_view *view, const char *key,
                                    pdf_object_view *out);
int cupidimage_pdf_read_name(const pdf_object_view *view, size_t *pos, char name[128]);
int cupidimage_pdf_entry_is_name(const unsigned char *data, size_t size,
                                 const pdf_object_view *view, const char *key,
                                 const char *expected);
int cupidimage_pdf_encoding_differences(const unsigned char *data, size_t size,
                                        const pdf_object_view *encoding, unsigned glyphs[256],
                                        const void *context,
                                        unsigned (*lookup)(const void *, const char *));
int cupidimage_pdf_simple_encoding(const unsigned char *data, size_t size,
                                   const pdf_object_view *view, unsigned glyphs[256],
                                   const void *context,
                                   unsigned (*lookup)(const void *, const char *));
int cupidimage_pdf_graphics_font(const unsigned char *data, size_t size,
                                 const pdf_object_view *state, pdf_object_id *object,
                                 double *font_size);
const pdf_font_map *cupidimage_pdf_font_table_object(const pdf_font_table *table,
                                                     pdf_object_id object);
int cupidimage_pdf_parse_standard_font(const unsigned char *data, size_t size,
                                       const pdf_object_view *view, pdf_font_map *font,
                                       size_t *budget);
int cupidimage_pdf_parse_substitute_font(const unsigned char *, size_t, const pdf_object_view *,
                                         pdf_font_map *, size_t *);
int cupidimage_pdf_truetype_encoding(const unsigned char *data, size_t size,
                                     const pdf_object_view *view, const pdf_object_view *descriptor,
                                     pdf_font_program *program);
int cupidimage_pdf_parse_font_program(const unsigned char *data, size_t size,
                                      const pdf_object_view *view, pdf_font_map *font,
                                      size_t *budget);
size_t cupidimage_pdf_font_program_bytes(const pdf_font_program *program);
void cupidimage_pdf_font_program_free(pdf_font_program *program);
int cupidimage_pdf_font_program_has_glyph(const pdf_font_program *program, unsigned code);
unsigned cupidimage_pdf_font_program_glyph(const pdf_font_program *program, unsigned code);
int cupidimage_pdf_font_program_advance(pdf_font_program *program, unsigned glyph,
                                        size_t *cache_bytes_left, double *advance);

#endif
