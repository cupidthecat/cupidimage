#ifndef CUPIDIMAGE_SVG_INTERNAL_H
#define CUPIDIMAGE_SVG_INTERNAL_H

#include "core/internal.h"
#include "cupidimage.h"
#include "formats/svg/cupidimage_svg_base.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SVG_MAX_ATTR 64
#define SVG_MAX_DEPTH 64
#define SVG_MAX_DASH 16
#define SVG_MAX_TRANSFER_TABLE 64
#define SVG_MAX_INFO_ATTR 32
#define SVG_MAX_SAMPLES (64u * 1024u * 1024u)

typedef struct svg_attr {
    char *name;
    char *value;
} svg_attr;

typedef struct svg_tag {
    char *name;
    svg_attr attrs[SVG_MAX_ATTR];
    char override_names[SVG_MAX_ATTR][64];
    char override_values[SVG_MAX_ATTR][128];
    uint8_t has_override_name[SVG_MAX_ATTR];
    uint8_t has_override_value[SVG_MAX_ATTR];
    int attr_count;
    int is_end;
    int is_self_closing;
} svg_tag;

typedef enum {
    SVG_PAINT_NONE,
    SVG_PAINT_COLOR,
    SVG_PAINT_LINEAR_GRADIENT,
    SVG_PAINT_RADIAL_GRADIENT,
    SVG_PAINT_PATTERN
} svg_paint_type;

typedef enum { SVG_LINECAP_BUTT, SVG_LINECAP_ROUND, SVG_LINECAP_SQUARE } svg_linecap;

typedef enum { SVG_LINEJOIN_MITER, SVG_LINEJOIN_ROUND, SVG_LINEJOIN_BEVEL } svg_linejoin;

typedef enum {
    SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX,
    SVG_GRADIENT_UNITS_USER_SPACE
} svg_gradient_units;

typedef enum { SVG_SPREAD_PAD, SVG_SPREAD_REFLECT, SVG_SPREAD_REPEAT } svg_spread_method;

typedef enum { SVG_MASK_TYPE_LUMINANCE, SVG_MASK_TYPE_ALPHA } svg_mask_type;

typedef struct svg_grad_coord {
    float value;
    int is_percent;
} svg_grad_coord;

typedef struct svg_gradient_stop {
    float offset;
    uint32_t color;
} svg_gradient_stop;

typedef struct svg_gradient {
    char *id;
    char *href;
    svg_paint_type type;
    svg_gradient_units units;
    svg_spread_method spread;
    svg_matrix transform;
    svg_matrix inv_transform;
    int has_transform;
    int has_units;
    int has_spread;
    int has_coords;
    int has_focal;
    svg_grad_coord x1, y1, x2, y2;
    svg_grad_coord cx, cy, r, fx, fy;
    svg_gradient_stop *stops;
    int stop_count;
} svg_gradient;

typedef struct svg_path_def {
    char *id;
    char *d;
} svg_path_def;

typedef struct svg_use_def {
    char *id;
    char *tag_name;
    svg_attr attrs[SVG_MAX_ATTR];
    int attr_count;
} svg_use_def;

typedef struct svg_symbol {
    char *id;
    char *content;
    float vb_x;
    float vb_y;
    float vb_w;
    float vb_h;
    int vb_ok;
    int preserve_none;
    float align_x;
    float align_y;
} svg_symbol;

typedef struct svg_marker {
    char *id;
    char *content;
    float ref_x;
    float ref_y;
    float marker_w;
    float marker_h;
    int units_stroke_width;
    int orient_auto;
    int orient_auto_start_reverse;
    float orient_angle;
    float vb_x;
    float vb_y;
    float vb_w;
    float vb_h;
    int vb_ok;
    int preserve_none;
    float align_x;
    float align_y;
} svg_marker;

typedef struct svg_pattern {
    char *id;
    char *href;
    svg_gradient_units units;
    svg_gradient_units content_units;
    int has_units;
    int has_content_units;
    int has_coords;
    svg_grad_coord x;
    svg_grad_coord y;
    svg_grad_coord width;
    svg_grad_coord height;
    svg_matrix transform;
    svg_matrix inv_transform;
    int has_transform;
    char *content;
    uint8_t *rgba;
    uint32_t tile_w;
    uint32_t tile_h;
    float cached_bbox_x;
    float cached_bbox_y;
    float cached_bbox_w;
    float cached_bbox_h;
    int has_cached_bbox;
    int rendered;
    int rendering;
} svg_pattern;

typedef struct svg_clip_path {
    char *id;
    char *href;
    svg_gradient_units units;
    int has_units;
    svg_matrix transform;
    svg_matrix inv_transform;
    int has_transform;
    char *content;
    uint8_t *rgba;
    uint32_t mask_w;
    uint32_t mask_h;
    float cached_bbox_x;
    float cached_bbox_y;
    float cached_bbox_w;
    float cached_bbox_h;
    int has_cached_bbox;
    int rendered;
    int rendering;
} svg_clip_path;

typedef struct svg_mask {
    char *id;
    char *href;
    svg_gradient_units units;
    svg_gradient_units content_units;
    int has_units;
    int has_content_units;
    int has_coords;
    svg_grad_coord x;
    svg_grad_coord y;
    svg_grad_coord width;
    svg_grad_coord height;
    svg_matrix transform;
    svg_matrix inv_transform;
    int has_transform;
    int type;
    int has_type;
    char *content;
    uint8_t *rgba;
    uint32_t mask_w;
    uint32_t mask_h;
    float cached_bbox_x;
    float cached_bbox_y;
    float cached_bbox_w;
    float cached_bbox_h;
    int has_cached_bbox;
    int rendered;
    int rendering;
} svg_mask;

typedef struct svg_filter {
    char *id;
    char *content;
    svg_gradient_units units;
    int has_units;
    int has_coords;
    svg_grad_coord x;
    svg_grad_coord y;
    svg_grad_coord width;
    svg_grad_coord height;
} svg_filter;

typedef struct svg_defs {
    svg_gradient *gradients;
    int gradient_count;
    int gradient_cap;
    svg_pattern *patterns;
    int pattern_count;
    int pattern_cap;
    struct svg_path_def *paths;
    int path_count;
    int path_cap;
    struct svg_use_def *uses;
    int use_count;
    int use_cap;
    struct svg_symbol *symbols;
    int symbol_count;
    int symbol_cap;
    struct svg_marker *markers;
    int marker_count;
    int marker_cap;
    svg_clip_path *clips;
    int clip_count;
    int clip_cap;
    svg_mask *masks;
    int mask_count;
    int mask_cap;
    svg_filter *filters;
    int filter_count;
    int filter_cap;
} svg_defs;

typedef struct svg_css_decl {
    int prop;
    char *value;
} svg_css_decl;

typedef struct svg_selector_part {
    char *tag;
    char *id;
    char **classes;
    int class_count;
    char **attr_names;
    char **attr_values;
    uint8_t *attr_has_value;
    int attr_count;
} svg_selector_part;

typedef struct svg_selector {
    svg_selector_part *parts;
    int *combinators;
    int part_count;
    int specificity;
} svg_selector;

typedef struct svg_css_rule {
    svg_selector *selectors;
    int selector_count;
    svg_css_decl *decls;
    int decl_count;
    int order;
} svg_css_rule;

typedef enum {
    SVG_CSS_ANIM_PROP_NONE = 0,
    SVG_CSS_ANIM_PROP_TRANSFORM,
    SVG_CSS_ANIM_PROP_STROKE_DASHOFFSET,
    SVG_CSS_ANIM_PROP_STOP_COLOR
} svg_css_anim_prop;

typedef enum {
    SVG_CSS_ANIM_DIR_NORMAL = 0,
    SVG_CSS_ANIM_DIR_REVERSE,
    SVG_CSS_ANIM_DIR_ALTERNATE,
    SVG_CSS_ANIM_DIR_ALTERNATE_REVERSE
} svg_css_anim_direction;

typedef enum {
    SVG_CSS_ANIM_FILL_NONE = 0,
    SVG_CSS_ANIM_FILL_FORWARDS,
    SVG_CSS_ANIM_FILL_BACKWARDS,
    SVG_CSS_ANIM_FILL_BOTH
} svg_css_anim_fill_mode;

typedef struct svg_css_keyframe_step {
    float offset;
    char *transform;
    float number;
    uint32_t color;
} svg_css_keyframe_step;

typedef struct svg_css_keyframes {
    char *name;
    int prop;
    svg_css_keyframe_step *steps;
    int step_count;
    int step_cap;
} svg_css_keyframes;

typedef struct svg_css_animation_binding {
    char *target_id;
    char *name;
    float duration;
    float delay;
    float repeat_count;
    int repeat_indefinite;
    int direction;
    int fill_mode;
} svg_css_animation_binding;

typedef struct svg_css {
    svg_css_rule *rules;
    int rule_count;
    int rule_cap;
    svg_css_keyframes *keyframes;
    int keyframe_count;
    int keyframe_cap;
    svg_css_animation_binding *animations;
    int animation_count;
    int animation_cap;
} svg_css;

typedef struct svg_style {
    svg_paint_type fill_type;
    svg_paint_type stroke_type;
    uint32_t fill_color;
    uint32_t stroke_color;
    const svg_gradient *fill_gradient;
    const svg_gradient *stroke_gradient;
    svg_pattern *fill_pattern;
    svg_pattern *stroke_pattern;
    svg_clip_path *clip_path;
    svg_mask *mask;
    svg_marker *marker_start;
    svg_marker *marker_mid;
    svg_marker *marker_end;
    const svg_filter *filter;
    uint32_t color;
    float fill_opacity;
    float stroke_opacity;
    float opacity;
    float stroke_width;
    int stroke_linecap;
    int stroke_linejoin;
    float stroke_miterlimit;
    float stroke_dasharray[SVG_MAX_DASH];
    int stroke_dashcount;
    float stroke_dashoffset;
    int fill_rule_evenodd;
    float font_size;
    float letter_spacing;
    float word_spacing;
    int text_anchor;
    int bold;
    int italic;
    int dominant_baseline;
    int display_none;
    int visibility_hidden;
} svg_style;

typedef struct svg_bbox {
    float x;
    float y;
    float w;
    float h;
} svg_bbox;

typedef struct svg_segment {
    float x0, y0, x1, y1;
} svg_segment;

typedef struct svg_segments {
    svg_segment *stroke;
    size_t stroke_count;
    size_t stroke_cap;
    svg_segment *fill;
    size_t fill_count;
    size_t fill_cap;
    float minx;
    float miny;
    float maxx;
    float maxy;
} svg_segments;

typedef struct svg_text_state {
    svg_style style;
    svg_matrix transform;
    int preserve;
} svg_text_state;

typedef struct svg_text_path_state {
    svg_segments segs;
    float *seg_lengths;
    size_t seg_count;
    float total_len;
    float pos;
    int active;
    svg_bbox bbox;
} svg_text_path_state;

typedef enum {
    SVG_FILTER_OP_BLUR,
    SVG_FILTER_OP_OFFSET,
    SVG_FILTER_OP_COLOR_MATRIX,
    SVG_FILTER_OP_FLOOD,
    SVG_FILTER_OP_BLEND,
    SVG_FILTER_OP_COMPOSITE,
    SVG_FILTER_OP_MERGE,
    SVG_FILTER_OP_COMPONENT_TRANSFER,
    SVG_FILTER_OP_MORPHOLOGY,
    SVG_FILTER_OP_CONVOLVE,
    SVG_FILTER_OP_TURBULENCE,
    SVG_FILTER_OP_DISPLACEMENT,
    SVG_FILTER_OP_DIFFUSE_LIGHTING,
    SVG_FILTER_OP_SPECULAR_LIGHTING,
    SVG_FILTER_OP_TILE,
    SVG_FILTER_OP_IMAGE
} svg_filter_op_type;

typedef enum {
    SVG_FILTER_IN_CURRENT,
    SVG_FILTER_IN_SOURCE_GRAPHIC,
    SVG_FILTER_IN_SOURCE_ALPHA,
    SVG_FILTER_IN_RESULT,
    SVG_FILTER_IN_BACKGROUND_IMAGE,
    SVG_FILTER_IN_BACKGROUND_ALPHA,
    SVG_FILTER_IN_FILL_PAINT,
    SVG_FILTER_IN_STROKE_PAINT
} svg_filter_input;

typedef struct svg_filter_ref {
    int type;
    int index;
} svg_filter_ref;

typedef enum {
    SVG_TRANSFER_IDENTITY,
    SVG_TRANSFER_TABLE,
    SVG_TRANSFER_DISCRETE,
    SVG_TRANSFER_LINEAR,
    SVG_TRANSFER_GAMMA
} svg_transfer_type;

typedef enum { SVG_MORPHOLOGY_ERODE, SVG_MORPHOLOGY_DILATE } svg_morphology_op;

typedef enum { SVG_EDGE_NONE, SVG_EDGE_DUPLICATE, SVG_EDGE_WRAP } svg_edge_mode;

typedef enum {
    SVG_DISPLACE_R,
    SVG_DISPLACE_G,
    SVG_DISPLACE_B,
    SVG_DISPLACE_A
} svg_displace_channel;

typedef struct svg_transfer_func {
    int type;
    float table[SVG_MAX_TRANSFER_TABLE];
    int table_count;
    float slope;
    float intercept;
    float amplitude;
    float exponent;
    float offset;
} svg_transfer_func;

typedef enum {
    SVG_BLEND_NORMAL,
    SVG_BLEND_MULTIPLY,
    SVG_BLEND_SCREEN,
    SVG_BLEND_DARKEN,
    SVG_BLEND_LIGHTEN,
    SVG_BLEND_OVERLAY,
    SVG_BLEND_HARDLIGHT,
    SVG_BLEND_SOFTLIGHT
} svg_blend_mode;

typedef enum {
    SVG_COMPOSITE_OVER,
    SVG_COMPOSITE_IN,
    SVG_COMPOSITE_OUT,
    SVG_COMPOSITE_ATOP,
    SVG_COMPOSITE_XOR,
    SVG_COMPOSITE_ARITHMETIC
} svg_composite_op;

typedef struct svg_filter_op {
    svg_filter_op_type type;
    float a;
    float b;
    float c;
    float d;
    uint32_t color;
    float matrix[20];
    int has_matrix;
    svg_filter_ref in1;
    svg_filter_ref in2;
    int mode;
    int merge_count;
    svg_filter_ref merge_inputs[8];
    int result_id;
    svg_transfer_func tr;
    svg_transfer_func tg;
    svg_transfer_func tb;
    svg_transfer_func ta;
    int has_region;
    svg_grad_coord rx;
    svg_grad_coord ry;
    svg_grad_coord rwidth;
    svg_grad_coord rheight;
    float *kernel;
    int kernel_count;
    int order_x;
    int order_y;
    int target_x;
    int target_y;
    float divisor;
    float bias;
    int preserve_alpha;
    float turb_freq_x;
    float turb_freq_y;
    int turb_octaves;
    int turb_seed;
    int turb_fractal;
    int displace_x_channel;
    int displace_y_channel;
    float surface_scale;
    float diffuse_constant;
    float specular_constant;
    float specular_exponent;
    float light_x;
    float light_y;
    float light_z;
    char *image_href;
    float img_x;
    float img_y;
    float img_w;
    float img_h;
    int has_image_geom;
} svg_filter_op;

typedef struct svg_join_tri {
    float ax, ay;
    float bx, by;
    float cx, cy;
} svg_join_tri;

typedef struct svg_filter_region {
    int x0, y0;
    int x1, y1;
    int valid;
} svg_filter_region;

typedef struct svg_render_ctx {
    uint32_t width;
    uint32_t height;
    uint32_t ss;
    float vb_x;
    float vb_y;
    float vb_w;
    float vb_h;
    float dpi;
    float anim_time;
    uint8_t *hi_rgba;
} svg_render_ctx;

typedef struct svg_preamble {
    int found_svg;
    float width_attr;
    float height_attr;
    int width_ok;
    int height_ok;
    float vb_x;
    float vb_y;
    float vb_w;
    float vb_h;
    int vb_ok;
    int preserve_none;
    float align_x;
    float align_y;
} svg_preamble;

typedef struct svg_elem_info {
    const char *tag;
    const char *id;
    const char *class_attr;
    const char *attr_names[SVG_MAX_INFO_ATTR];
    const char *attr_values[SVG_MAX_INFO_ATTR];
    int attr_count;
} svg_elem_info;

typedef struct svg_stack_item {
    svg_style style;
    svg_matrix transform;
    svg_elem_info info;
} svg_stack_item;

typedef struct svg_sibling_frame {
    svg_elem_info *items;
    int count;
    int cap;
} svg_sibling_frame;

typedef enum {
    SVG_PROP_FILL,
    SVG_PROP_STROKE,
    SVG_PROP_STROKE_WIDTH,
    SVG_PROP_STROKE_LINECAP,
    SVG_PROP_STROKE_LINEJOIN,
    SVG_PROP_STROKE_MITERLIMIT,
    SVG_PROP_STROKE_DASHARRAY,
    SVG_PROP_STROKE_DASHOFFSET,
    SVG_PROP_CLIP_PATH,
    SVG_PROP_MASK,
    SVG_PROP_MARKER,
    SVG_PROP_MARKER_START,
    SVG_PROP_MARKER_MID,
    SVG_PROP_MARKER_END,
    SVG_PROP_FILTER,
    SVG_PROP_OPACITY,
    SVG_PROP_FILL_OPACITY,
    SVG_PROP_STROKE_OPACITY,
    SVG_PROP_FILL_RULE,
    SVG_PROP_COLOR,
    SVG_PROP_FONT_SIZE,
    SVG_PROP_TEXT_ANCHOR,
    SVG_PROP_FONT_WEIGHT,
    SVG_PROP_FONT_STYLE,
    SVG_PROP_DOMINANT_BASELINE,
    SVG_PROP_LETTER_SPACING,
    SVG_PROP_WORD_SPACING,
    SVG_PROP_DISPLAY,
    SVG_PROP_VISIBILITY,
    SVG_PROP_COUNT
} svg_property;

typedef struct svg_named_color {
    const char *name;
    uint32_t rgba;
} svg_named_color;

void cupidimage_svg_apply_animation_children(svg_tag *target, const char *target_local,
                                             char *child_start, float time_s);
void cupidimage_svg_apply_filter_ops(const svg_filter *filter, svg_render_ctx *ctx, uint8_t *buf,
                                     float base_len, const svg_filter_region *region,
                                     const svg_bbox *bbox, const svg_matrix *m, uint8_t *background,
                                     uint8_t *background_alpha, uint8_t *fill_paint,
                                     uint8_t *stroke_paint);
void cupidimage_svg_apply_inline_style_attr(svg_style *style, const svg_tag *tag, float base_len,
                                            float dpi, const svg_defs *defs);
void cupidimage_svg_apply_presentation_attrs(svg_style *style, const svg_tag *tag, float base_len,
                                             float dpi, const svg_defs *defs);
float cupidimage_svg_apply_spread(float t, svg_spread_method spread);
void cupidimage_svg_bbox_add_rect(svg_bbox *bbox, float x, float y, float w, float h);
void cupidimage_svg_bbox_expand(svg_bbox *bbox, float amount);
void cupidimage_svg_bbox_init(svg_bbox *bbox);
int cupidimage_svg_bbox_valid(const svg_bbox *bbox);
void cupidimage_svg_blend_premul(uint8_t *dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa);
void cupidimage_svg_build_join_tris(const svg_segments *segs, const svg_style *style,
                                    const uint8_t *start_open, const uint8_t *end_open,
                                    svg_join_tri **out_tris, int *out_count);
char *cupidimage_svg_build_use_wrapper_attrs(const svg_tag *tag);
float cupidimage_svg_clamp01(float v);
void cupidimage_svg_clip_finalize(svg_clip_path *c);
void cupidimage_svg_clip_inherit(svg_clip_path *c, svg_defs *defs);
void cupidimage_svg_composite_buffer(svg_render_ctx *ctx, const uint8_t *src);
void cupidimage_svg_compute_filter_region(const svg_filter *filter, const svg_render_ctx *ctx,
                                          const svg_bbox *bbox, const svg_matrix *m,
                                          svg_filter_region *out);
void cupidimage_svg_css_apply(svg_style *style, const svg_css *css, const svg_defs *defs,
                              const svg_elem_info *stack, int depth, const svg_elem_info *siblings,
                              int sibling_count, float base_len, float dpi);
void cupidimage_svg_css_apply_animations_to_tag(svg_tag *tag, const svg_css *css, float time_s);
void cupidimage_svg_css_free(svg_css *css);
void cupidimage_svg_css_init(svg_css *css);
int cupidimage_svg_css_parse(svg_css *css, const char *text);
svg_clip_path *cupidimage_svg_defs_add_clip(svg_defs *defs, const char *id);
svg_filter *cupidimage_svg_defs_add_filter(svg_defs *defs, const char *id);
svg_gradient *cupidimage_svg_defs_add_gradient(svg_defs *defs, const char *id, svg_paint_type type);
svg_marker *cupidimage_svg_defs_add_marker(svg_defs *defs, const char *id);
svg_mask *cupidimage_svg_defs_add_mask(svg_defs *defs, const char *id);
svg_path_def *cupidimage_svg_defs_add_path(svg_defs *defs, const char *id, const char *d);
svg_pattern *cupidimage_svg_defs_add_pattern(svg_defs *defs, const char *id);
svg_symbol *cupidimage_svg_defs_add_symbol(svg_defs *defs, const char *id);
svg_use_def *cupidimage_svg_defs_add_use(svg_defs *defs, const svg_tag *tag);
svg_clip_path *cupidimage_svg_defs_find_clip(svg_defs *defs, const char *id);
const svg_filter *cupidimage_svg_defs_find_filter(const svg_defs *defs, const char *id);
const svg_gradient *cupidimage_svg_defs_find_gradient(const svg_defs *defs, const char *id);
svg_marker *cupidimage_svg_defs_find_marker(svg_defs *defs, const char *id);
svg_mask *cupidimage_svg_defs_find_mask(svg_defs *defs, const char *id);
const svg_path_def *cupidimage_svg_defs_find_path(const svg_defs *defs, const char *id);
const svg_pattern *cupidimage_svg_defs_find_pattern(const svg_defs *defs, const char *id);
const svg_symbol *cupidimage_svg_defs_find_symbol(const svg_defs *defs, const char *id);
svg_use_def *cupidimage_svg_defs_find_use(svg_defs *defs, const char *id);
void cupidimage_svg_defs_free(svg_defs *defs);
void cupidimage_svg_defs_init(svg_defs *defs);
int cupidimage_svg_dimension(float value, uint32_t *out);
void cupidimage_svg_draw_samples_ellipse(svg_render_ctx *ctx, const svg_style *style,
                                         const svg_matrix *m, float cx, float cy, float rx,
                                         float ry);
void cupidimage_svg_draw_samples_image(svg_render_ctx *ctx, const svg_style *style,
                                       const svg_matrix *m, const cupidimage_image *img, float x,
                                       float y, float w, float h, int preserve_none, float align_x,
                                       float align_y);
void cupidimage_svg_draw_samples_line(svg_render_ctx *ctx, const svg_style *style,
                                      const svg_matrix *m, float x0, float y0, float x1, float y1);
void cupidimage_svg_draw_samples_rect(svg_render_ctx *ctx, const svg_style *style,
                                      const svg_matrix *m, float x, float y, float w, float h,
                                      float rx, float ry);
void cupidimage_svg_draw_samples_segments(svg_render_ctx *ctx, const svg_style *style,
                                          const svg_matrix *m, const svg_segments *segs);
void cupidimage_svg_elem_info_from_tag(svg_elem_info *info, const char *local, const svg_tag *tag);
void cupidimage_svg_filter_ops_cleanup(svg_filter_op *ops, int count);
char *cupidimage_svg_find_close_tag(char *p, const char *tag_name, char **after);
void cupidimage_svg_gaussian_blur(uint8_t *buf, uint32_t w, uint32_t h, float sigma_x,
                                  float sigma_y);
const char *cupidimage_svg_get_attr(const svg_tag *tag, const char *name);
const char *cupidimage_svg_get_attr_exact(const svg_tag *tag, const char *name);
int cupidimage_svg_gradient_add_stop(svg_gradient *g, float offset, uint32_t color);
void cupidimage_svg_gradient_finalize(svg_gradient *g);
void cupidimage_svg_gradient_inherit(svg_gradient *g, const svg_defs *defs);
uint32_t cupidimage_svg_gradient_stop_color(const svg_gradient *g, float t);
int cupidimage_svg_load_href_image(const char *href, cupidimage_image *img);
svg_filter_ref cupidimage_svg_make_filter_ref(int type, int index);
void cupidimage_svg_mask_finalize(svg_mask *m);
void cupidimage_svg_mask_inherit(svg_mask *m, svg_defs *defs);
int cupidimage_svg_matrix_to_string(const svg_matrix *m, char *buf, size_t cap);
int cupidimage_svg_next_tag(char **p, svg_tag *tag);
void cupidimage_svg_parse_clip_tag(const svg_tag *tag, svg_clip_path *c);
int cupidimage_svg_parse_clock_seconds(const char *s, float *out);
int cupidimage_svg_parse_color(const char *s, uint32_t *out, int *is_none);
int cupidimage_svg_parse_filter_ops(const char *content, svg_filter_op **out_ops, int *out_count,
                                    int *out_result_count, float base_len, float dpi,
                                    svg_gradient_units units);
void cupidimage_svg_parse_filter_tag(const svg_tag *tag, svg_filter *f);
int cupidimage_svg_parse_grad_coord(const char *s, svg_grad_coord *out, svg_gradient_units units);
void cupidimage_svg_parse_gradient_tag(const svg_tag *tag, svg_gradient *g);
int cupidimage_svg_parse_href_id(const char *value, char *out, size_t outcap);
float cupidimage_svg_parse_length(const char *s, float base, float dpi, int *ok);
int cupidimage_svg_parse_length_attr(const svg_tag *tag, const char *name, float base, float dpi,
                                     float *out);
int cupidimage_svg_parse_length_value(const char **p, float base, float dpi, float *out);
void cupidimage_svg_parse_marker_tag(const svg_tag *tag, svg_marker *mk, float base_len, float dpi);
void cupidimage_svg_parse_mask_tag(const svg_tag *tag, svg_mask *m);
int cupidimage_svg_parse_number(const char **p, float *out);
int cupidimage_svg_parse_opacity_value(const char *s, float *out);
int cupidimage_svg_parse_path(const char *d, svg_segments *segs);
void cupidimage_svg_parse_pattern_tag(const svg_tag *tag, svg_pattern *p);
int cupidimage_svg_parse_points(const char *s, float **out_points, int *out_count);
int cupidimage_svg_parse_preamble(const unsigned char *data, size_t size, svg_preamble *pre,
                                  svg_defs *defs, svg_css *css, float dpi, float anim_time,
                                  char *err, size_t errcap);
void cupidimage_svg_parse_preserve_aspect_ratio(const char *s, int *preserve_none, float *align_x,
                                                float *align_y);
void cupidimage_svg_parse_symbol_tag(const svg_tag *tag, svg_symbol *sym);
int cupidimage_svg_parse_transform(const char *value, svg_matrix *out);
int cupidimage_svg_parse_transform_number(const char **p, float *out);
int cupidimage_svg_parse_viewbox(const char *s, float *x, float *y, float *w, float *h);
void cupidimage_svg_pattern_finalize(svg_pattern *p);
void cupidimage_svg_pattern_inherit(svg_pattern *p, const svg_defs *defs);
int cupidimage_svg_point_in_segments(const svg_segments *segs, int evenodd, float px, float py);
int cupidimage_svg_point_on_segments(const svg_segments *segs, float px, float py,
                                     float half_width);
int cupidimage_svg_point_on_segments_cap(const svg_segments *segs, const svg_style *style, float px,
                                         float py, float half_width, const uint8_t *start_open,
                                         const uint8_t *end_open, const svg_join_tri *joins,
                                         int join_count);
int cupidimage_svg_prepare_const_paint(const svg_style *style, int is_stroke, uint8_t *pr,
                                       uint8_t *pg, uint8_t *pb, uint8_t *pa);
int cupidimage_svg_property_id(const char *name);
int cupidimage_svg_raster_bound(float value, int upper);
int cupidimage_svg_render(svg_render_ctx *ctx, const unsigned char *data, size_t size, char *err,
                          size_t errcap, const cupidimage_svg_options *opts);
void cupidimage_svg_render_text_element(svg_render_ctx *ctx, const svg_style *style,
                                        const svg_matrix *m, const svg_tag *text_tag, char **p,
                                        const svg_defs *defs, const svg_css *css,
                                        const svg_elem_info *stack_info, int stack_depth,
                                        float base_len, float dpi, svg_bbox *out_bbox);
float cupidimage_svg_resolve_grad_coord(const svg_grad_coord *c, float base, float origin,
                                        svg_gradient_units units);
uint8_t cupidimage_svg_sample_mask_alpha(svg_style *style, const svg_render_ctx *ctx,
                                         const svg_bbox *bbox, int sx, int sy);
int cupidimage_svg_sample_paint(const svg_style *style, const svg_render_ctx *ctx, int is_stroke,
                                const svg_bbox *bbox, float x, float y, uint8_t *pr, uint8_t *pg,
                                uint8_t *pb, uint8_t *pa);
int cupidimage_svg_segments_add(svg_segments *segs, float x0, float y0, float x1, float y1,
                                int to_stroke, int to_fill);
void cupidimage_svg_segments_add_ellipse(svg_segments *segs, float cx, float cy, float rx,
                                         float ry);
void cupidimage_svg_segments_add_rect(svg_segments *segs, float x, float y, float w, float h,
                                      float rx, float ry);
int cupidimage_svg_segments_build_dashed(const svg_segments *src, const svg_style *style,
                                         svg_segments *out);
void cupidimage_svg_segments_endpoint_flags(const svg_segments *segs, uint8_t *start_open,
                                            uint8_t *end_open);
void cupidimage_svg_segments_free(svg_segments *segs);
void cupidimage_svg_segments_init(svg_segments *segs);
int cupidimage_svg_sibling_frame_push(svg_sibling_frame *frame, const svg_elem_info *info);
void cupidimage_svg_sibling_frames_free(svg_sibling_frame *frames, int depth_cap);
void cupidimage_svg_skip_separators(const char **p);
int cupidimage_svg_stop_parse(const svg_tag *tag, float *offset, uint32_t *color);
void cupidimage_svg_style_apply_property_id(svg_style *style, int prop, const char *value,
                                            float base_len, float dpi, const svg_defs *defs);
void cupidimage_svg_style_init(svg_style *style);
char *cupidimage_svg_text_normalize(const char *text, int preserve);
float cupidimage_svg_transfer_apply(const svg_transfer_func *func, float v);
void cupidimage_svg_transform_bounds(const svg_matrix *m, float minx, float miny, float maxx,
                                     float maxy, float *out_minx, float *out_miny, float *out_maxx,
                                     float *out_maxy);
char *cupidimage_svg_wrap_with_group_attrs(const char *content, const char *attrs);
int cupidimage_svg_xml_space_preserve(const svg_tag *tag, int inherited);

#endif
