#ifndef CUPIDIMAGE_PDF_RENDER_INTERNAL_H
#define CUPIDIMAGE_PDF_RENDER_INTERNAL_H

#include "pdf_internal.h"

typedef struct {
    float x0;
    float y0;
    float x1;
    float y1;
    int starts_subpath;
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
    float ctm[6];
    uint8_t fill_r;
    uint8_t fill_g;
    uint8_t fill_b;
    uint8_t stroke_r;
    uint8_t stroke_g;
    uint8_t stroke_b;
    float line_width_pts;
    double fill_alpha, stroke_alpha;
    int blend_mode;
    int fill_space;
    int stroke_space;
    pdf_clip_mask *clip;
} pdf_graphics_state;

typedef struct {
    double inverse[6];
    double box[4];
} pdf_form_clip;

typedef struct {
    size_t bytes_left;
    unsigned calls_left;
    size_t shade_pixels_left;
} pdf_form_budget;

typedef struct {
    cupidimage_image *img;
    const unsigned char *document;
    size_t document_size;
    int page_object;
    int image_failed;
    char *error;
    size_t error_capacity;
    float page_x0_pts;
    float page_top_pts;
    float line_x_pts;
    float line_y_pts;
    float text_x_pts;
    float text_y_pts;
    float font_size_pts;
    float leading_pts;
    pdf_graphics_state gs;
    pdf_graphics_state gs_stack[32];
    int gs_stack_len;
    pdf_path path;
    int pending_clip;
    pdf_clip_budget *clip_budget;
    int in_text;
    const pdf_font_table *font_table;
    const pdf_font_map *current_font;
    int form_objects[16];
    pdf_form_clip form_clips[16];
    int form_depth;
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

typedef struct {
    int components, indexed, high;
    unsigned char palette[1024];
} pdf_image_colors;
int cupidimage_pdf_image_color_space(const pdf_render_state *st, const pdf_object_view *view,
                                     size_t *pos, unsigned depth, pdf_image_colors *colors);

int cupidimage_pdf_apply_extgstate(pdf_render_state *st, const char *name);
void cupidimage_pdf_blend_rgb(int mode, const uint8_t source[3], const uint8_t backdrop[3], double result[3]);
void cupidimage_pdf_composite_pixel(pdf_render_state *st, int x, int y, uint8_t r, uint8_t g, uint8_t b, double alpha);

int cupidimage_pdf_read_number(const pdf_render_state *st, const pdf_object_view *view,
                              size_t pos, unsigned depth, double *value);
int cupidimage_pdf_draw_shading(pdf_render_state *st, const char *name);

#endif
