#ifndef CUPIDIMAGE_PDF_INTERNAL_H
#define CUPIDIMAGE_PDF_INTERNAL_H

#include "cupidimage.h"
#include "core/internal.h"

typedef struct {
    int obj_num;
    int gen_num;
    size_t offset;
} pdf_xref_entry;

typedef struct {
    pdf_xref_entry *entries;
    int count;
    int root_obj;
    int info_obj;
} pdf_xref;

typedef struct {
    const unsigned char *data;
    size_t len;
    unsigned char *owned;
} pdf_object_view;

typedef struct {
    double x0, y0, x1, y1;
    int rotation;
    int width, height;
} pdf_page_geometry;

typedef struct {
    uint32_t src_code;
    uint32_t dst_codepoint;
    uint32_t order;
    uint8_t src_len;
} pdf_tounicode_entry;

typedef struct {
    double matrix[6], bounds[4], widths[256];
    int glyphs[256];
    pdf_object_view dictionary;
} pdf_type3_font;

typedef struct {
    char name[32];
    pdf_tounicode_entry *entries;
    int count;
    int cap;
    int max_src_len;
    int has_widths, has_missing_width, first_char, last_char;
    double widths[256], missing_width;
    pdf_type3_font *type3;
} pdf_font_map;

typedef struct {
    pdf_font_map *fonts;
    int count;
    int cap;
} pdf_font_table;


extern const uint8_t cupidimage_pdf_font8x8_basic[128][8];

int cupidimage_pdf_buffer_contains(const unsigned char *data, size_t start, size_t end, const char *needle);
int cupidimage_pdf_extract_dict_ref(const unsigned char *data, size_t start, size_t end,
                            const char *key, int *obj_num);
int cupidimage_pdf_extract_stream_from_object(const unsigned char *obj_data, size_t obj_len,
                                      const unsigned char **stream_data, size_t *stream_len,
                                      size_t *stream_keyword_pos);
int cupidimage_pdf_find_eof_marker(const unsigned char *data, size_t size, size_t *eof_pos);
int cupidimage_pdf_find_startxref(const unsigned char *data, size_t eof_pos, size_t *xref_offset);
int cupidimage_pdf_get_object_content(const unsigned char *data, size_t size, int obj_num,
                              pdf_object_view *view);
int cupidimage_pdf_get_page_content_stream(const unsigned char *data, size_t size, pdf_xref *xref,
                                   int page_obj, unsigned char **stream_data,
                                   size_t *stream_len, int *is_flate,
                                   char *err, size_t errcap);
int cupidimage_pdf_get_page_geometry(const unsigned char *data, size_t size, int page_obj,
                              pdf_page_geometry *geometry, char *err, size_t errcap);
int cupidimage_pdf_get_page_obj_by_index(const unsigned char *data, size_t size, pdf_xref *xref,
                                 int pages_obj, int page_index, int *page_obj,
                                 char *err, size_t errcap);
int cupidimage_pdf_get_pages_count_from_obj(const unsigned char *data, size_t size, int pages_obj,
                                    int *count, char *err, size_t errcap);
int cupidimage_pdf_get_pages_obj(const unsigned char *data, size_t size, pdf_xref *xref,
                         int *pages_obj, char *err, size_t errcap);
int cupidimage_pdf_parse_pdf_number(const unsigned char *data, size_t end, size_t *pos, double *out);
int cupidimage_pdf_parse_trailer(const unsigned char *data, size_t size, size_t xref_pos,
                         pdf_xref *xref, char *err, size_t errcap);
int cupidimage_pdf_parse_xref_table(const unsigned char *data, size_t size, size_t xref_pos,
                            pdf_xref *xref, char *err, size_t errcap);
int cupidimage_pdf_check_header(const unsigned char *data, size_t size);
int cupidimage_pdf_collect_page_fonts(const unsigned char *data, size_t size, int page_obj,
                                  pdf_font_table *table, char *err, size_t errcap);
const pdf_font_map *cupidimage_pdf_font_table_find(const pdf_font_table *table, const char *name);
void cupidimage_pdf_font_table_free(pdf_font_table *table);
void cupidimage_pdf_font_table_init(pdf_font_table *table);
int cupidimage_pdf_image_filter_is(const pdf_object_view *view, const char *name);
int cupidimage_pdf_image_name_is(const pdf_object_view *view, const char *key, const char *name);
int cupidimage_pdf_inflate_auto(const unsigned char *data, size_t size,
                            unsigned char **out_data, size_t *out_len);
int cupidimage_pdf_is_delim(unsigned char c);
void cupidimage_pdf_object_view_release(pdf_object_view *view);
int cupidimage_pdf_page_rectangle(const unsigned char *data, size_t size,
                               const pdf_object_view *view, size_t pos, double box[4]);
int cupidimage_pdf_page_reference(const pdf_object_view *view, size_t pos, int *object);
int cupidimage_pdf_page_value(const pdf_object_view *view, const char *key, size_t *value_pos);
int cupidimage_pdf_parse_decimal(const unsigned char *data, size_t end, size_t *pos,
                             size_t limit, size_t *out);
int cupidimage_pdf_render_content_stream(const unsigned char *stream, size_t stream_len,
                                     const unsigned char *document, size_t document_size, int page_object,
                                     const pdf_font_table *font_table,
                                     cupidimage_image *img, const pdf_page_geometry *geometry,
                                     char *err, size_t errcap);
void cupidimage_pdf_skip_ws_and_comments(const unsigned char *data, size_t size, size_t *pos);

int cupidimage_pdf_is_ws(unsigned char c);
int cupidimage_pdf_dictionary_integer(const unsigned char *document, size_t document_size,
                                      const pdf_object_view *view, const char *key, int *value);
int cupidimage_pdf_object_stream(const unsigned char *document, size_t document_size,
                                 const pdf_object_view *view, const unsigned char **stream, size_t *length);

int cupidimage_pdf_decode_stream(const pdf_object_view *view, const unsigned char *data,
                                size_t length, unsigned char **decoded, size_t *decoded_length);

int cupidimage_pdf_dictionary_view(const pdf_object_view *parent, size_t start,
                                   pdf_object_view *view, size_t *end);

int cupidimage_pdf_parse_type3_font(const unsigned char *data, size_t size,
                                   const pdf_object_view *view, pdf_font_map *font);
int cupidimage_pdf_collect_resource_fonts(const unsigned char *data, size_t size,
                                         const pdf_object_view *owner, pdf_font_table *table,
                                         char *error, size_t capacity);

#endif
