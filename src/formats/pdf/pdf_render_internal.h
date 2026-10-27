#ifndef CUPIDIMAGE_PDF_RENDER_INTERNAL_H
#define CUPIDIMAGE_PDF_RENDER_INTERNAL_H

#include "pdf_internal.h"

typedef struct {
    float x0;
    float y0;
    float x1;
    float y1;
    int starts_subpath;
    int closes_subpath;
} pdf_seg;

typedef struct {
    pdf_seg segs[4096];
    int seg_count;
    int subpath_first_seg;
    float current_x;
    float current_y;
    float subpath_start_x;
    float subpath_start_y;
    int has_current;
    int has_subpath;
    int overflow;
} pdf_path;

typedef struct {
    unsigned references;
    size_t bytes;
    unsigned char bits[];
} pdf_clip_mask;

typedef struct {
    size_t bytes_used;
    size_t tests_left;
    unsigned calls_left;
} pdf_clip_budget;

typedef struct {
    int components, indexed, high, pattern;
    unsigned char palette[1024];
} pdf_image_colors;

typedef struct pdf_pattern {
    struct pdf_pattern *next;
    cupidimage_image tile;
    double inverse[6], xstep, ystep;
    int shading, resource_object;
    float initial_ctm[6];
    double page_x0, page_top;
} pdf_pattern;
typedef struct {
    pdf_pattern *head;
    size_t bytes_used;
    unsigned count;
} pdf_pattern_store;

typedef struct {
    float ctm[6];
    double font_size_pts, leading_pts, char_space, word_space, horizontal_scale, text_rise;
    int text_mode;
    const pdf_font_map *current_font;
    uint8_t fill_r;
    uint8_t fill_g;
    uint8_t fill_b;
    uint8_t stroke_r;
    uint8_t stroke_g;
    uint8_t stroke_b;
    float line_width_pts;
    int line_cap, line_join, dash_count;
    double miter_limit, dash_phase, dash[32];
    double fill_alpha, stroke_alpha;
    int blend_mode;
    pdf_image_colors fill_space;
    pdf_image_colors stroke_space;
    pdf_clip_mask *clip;
    pdf_pattern *fill_pattern, *stroke_pattern;
} pdf_graphics_state;

typedef struct {
    double inverse[6];
    double box[4];
} pdf_form_clip;

typedef struct {
    size_t bytes_left;
    unsigned calls_left;
    size_t shade_evaluations_left;
    unsigned glyph_calls_left;
    size_t cmap_steps_left;
} pdf_form_budget;

typedef struct {
    cupidimage_image *img;
    const unsigned char *document;
    size_t document_size;
    int page_object;
    const pdf_object_view *resource_override;
    int image_failed;
    char *error;
    size_t error_capacity;
    float page_x0_pts;
    float page_top_pts;
    double text_matrix[6], text_line_matrix[6];
    pdf_graphics_state gs;
    pdf_graphics_state initial_gs;
    pdf_pattern_store *patterns;
    unsigned pattern_depth;
    int pattern_uncolored;
    pdf_graphics_state gs_stack[32];
    int gs_stack_len;
    pdf_path path;
    int pending_clip;
    pdf_clip_budget *clip_budget;
    int in_text;
    pdf_clip_mask *text_clip;
    int32_t *text_winding;
    size_t text_winding_bytes;
    const pdf_font_table *font_table;
    int form_objects[16];
    pdf_form_clip form_clips[16];
    int form_depth;
    int glyph_depth;
    int glyph_active, glyph_seen_metrics, glyph_uncolored;
    double glyph_expected_width;
    pdf_form_budget *form_budget;
} pdf_render_state;

int cupidimage_pdf_draw_form(pdf_render_state *st, int object, const pdf_object_view *form);
int cupidimage_pdf_image_dictionary(const pdf_render_state *st, const pdf_object_view *parent,
                                   const char *key, pdf_object_view *result);
int cupidimage_pdf_image_integer(const pdf_render_state *st, const pdf_object_view *view,
                                const char *key, int *value);
int cupidimage_pdf_image_stream(const pdf_render_state *st, const pdf_object_view *view,
                               const unsigned char **stream, size_t *length);
int cupidimage_pdf_draw_image(pdf_render_state *st, const char *name);
uint8_t cupidimage_pdf_clamp_u8(double v);
void cupidimage_pdf_user_to_pixel(const pdf_render_state *st, float ux, float uy, float *px, float *py);
void cupidimage_pdf_set_pixel(pdf_render_state *st, int x, int y, uint8_t r, uint8_t g, uint8_t b);

int cupidimage_pdf_image_color_space(const pdf_render_state *st, const pdf_object_view *view,
                                     size_t *pos, unsigned depth, pdf_image_colors *colors);

int cupidimage_pdf_color_space(const pdf_render_state *st, const char *name,
                               unsigned depth, pdf_image_colors *colors);

int cupidimage_pdf_apply_extgstate(pdf_render_state *st, const char *name);
void cupidimage_pdf_blend_rgb(int mode, const uint8_t source[3], const uint8_t backdrop[3], double result[3]);
void cupidimage_pdf_composite_pixel(pdf_render_state *st, int x, int y, uint8_t r, uint8_t g, uint8_t b, double alpha);

int cupidimage_pdf_read_number(const pdf_render_state *st, const pdf_object_view *view,
                              size_t pos, unsigned depth, double *value);
int cupidimage_pdf_draw_shading(pdf_render_state *st, const char *name);


/* Borrowed image objects and bounded inline-image syntax. */
int cupidimage_pdf_draw_image_view(pdf_render_state *st, const pdf_object_view *view);
int cupidimage_pdf_draw_inline_image(pdf_render_state *st, const unsigned char *data,
                                     size_t length, size_t *position);

int cupidimage_pdf_text_clip_prepare(pdf_render_state *st);
void cupidimage_pdf_text_end(pdf_render_state *st);
void cupidimage_pdf_stroke_segments(pdf_render_state *st, const pdf_path *path, unsigned char *target);
void cupidimage_pdf_text_begin(pdf_render_state *st);
void cupidimage_pdf_text_move(pdf_render_state *st, double x, double y);
void cupidimage_pdf_text_advance(pdf_render_state *st, double distance);
void cupidimage_pdf_render_text_string(pdf_render_state *st, const char *text, size_t length);

int cupidimage_pdf_find_resource(const pdf_render_state *st, const char *category,
                                const char *name, pdf_object_view *value, int *reference);

int cupidimage_pdf_draw_type3_glyph(pdf_render_state *st, const pdf_font_map *font, unsigned code);

int cupidimage_pdf_select_pattern(pdf_render_state *st, const char *name, int stroke);
void cupidimage_pdf_patterns_free(pdf_pattern_store *store);
int cupidimage_pdf_pattern_sample(const pdf_pattern *pattern, double x, double y, uint8_t rgba[4]);
int cupidimage_pdf_render_pattern_cell(pdf_render_state *parent, const pdf_object_view *pattern,
                                       const unsigned char *stream, size_t length,
                                       cupidimage_image *image, double sx, double sy, double ox,
                                       double oy, const double bbox[4], int uncolored,
                                       const uint8_t color[3]);
void cupidimage_pdf_paint_pixel(pdf_render_state *st, int x, int y, int stroke, double coverage);

int cupidimage_pdf_paint_shading_view(pdf_render_state *st, const pdf_object_view *view,
                                      int background);
int cupidimage_pdf_apply_graphics_view(pdf_render_state *st, const pdf_object_view *view);
int cupidimage_pdf_render_shading_pattern(pdf_render_state *parent, const pdf_object_view *pattern,
                                          const double matrix[6], cupidimage_image *image);

int cupidimage_pdf_set_dash(pdf_graphics_state *gs, const unsigned char *data, size_t length,
                            double phase);

int cupidimage_pdf_apply_stroke_view(const pdf_render_state *st, const pdf_object_view *view,
                                     pdf_graphics_state *next);

int cupidimage_pdf_draw_truetype_glyph(pdf_render_state *st, const pdf_tt_program *program,
                                       unsigned glyph);

#endif
