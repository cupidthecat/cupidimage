#ifndef CUPIDIMAGE_PDF_RENDER_INTERNAL_H
#define CUPIDIMAGE_PDF_RENDER_INTERNAL_H

#include "clipping/geometry.h"
#include "fonts/page_hint.h"
#include "optional_content/state.h"
#include "pdf_internal.h"

#define PDF_COLOR_COMPONENTS 32

typedef struct {
    float x0;
    float y0;
    float x1;
    float y1;
    int starts_subpath;
    int closes_subpath;
    int curve;
    float cx1, cy1, cx2, cy2;
} pdf_seg;

#define PDF_PATH_INLINE_SEGMENTS 4096u
#define PDF_PATH_SEGMENT_LIMIT 65536u

typedef struct {
    pdf_seg segs[PDF_PATH_INLINE_SEGMENTS];
    pdf_seg *extra;
    unsigned capacity;
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

static inline const pdf_seg *cupidimage_pdf_path_segments(const pdf_path *path) {
    return path->extra ? path->extra : path->segs;
}

static inline pdf_seg *cupidimage_pdf_path_mutable(pdf_path *path) {
    return path->extra ? path->extra : path->segs;
}

static inline int cupidimage_pdf_path_valid(const pdf_path *path) {
    if (!path || path->overflow || path->seg_count < 0)
        return 0;
    unsigned capacity = path->extra ? path->capacity : PDF_PATH_INLINE_SEGMENTS;
    return capacity >= PDF_PATH_INLINE_SEGMENTS && capacity <= PDF_PATH_SEGMENT_LIMIT &&
           (unsigned)path->seg_count <= capacity;
}

typedef struct {
    unsigned references;
    size_t bytes;
    pdf_clip_geometry *geometry;
    unsigned char *coverage;
    size_t coverage_bytes;
    unsigned captured_form_depth;
    pdf_region_edge *pending_edges;
    unsigned pending_count, pending_capacity;
    unsigned char bits[];
} pdf_clip_mask;

typedef struct {
    size_t bytes_used;
    size_t tests_left;
    unsigned calls_left;
} pdf_clip_budget;

typedef struct pdf_icc_profile pdf_icc_profile;
typedef struct pdf_tint pdf_tint;
typedef struct pdf_palette pdf_palette;
typedef struct pdf_color_function pdf_color_function;

typedef struct {
    pdf_color_function *head;
    size_t bytes;
    unsigned count;
} pdf_color_function_store;

typedef struct {
    pdf_palette *head;
    size_t bytes;
    unsigned count;
} pdf_palette_store;

typedef struct {
    pdf_tint *head;
    size_t bytes;
    unsigned count;
} pdf_tint_store;

typedef struct {
    pdf_icc_profile *head;
    size_t bytes;
    unsigned count;
} pdf_icc_store;

typedef struct {
    int components, indexed, high, pattern;
    pdf_icc_profile *icc;
    pdf_tint *tint;
    const unsigned char *palette;
    int calibrated;       /* 0: device; 1: CalGray; 2: CalRGB; 3: Lab. */
    int encoded_transfer; /* Source-only: 1 sRGB, 2 ROMM RGB, 3 extended Lab ranges, 4 sYCC. */
    double source_range[6], source_matrix[9];
    double white[3], gamma[3], matrix[9], adaptation[9], lab_range[4];
} pdf_image_colors;

typedef struct {
    pdf_image_colors colors;
    double inverse_adaptation[9], inverse_matrix[9];
} pdf_blend_space;

typedef struct pdf_pattern {
    struct pdf_pattern *next;
    cupidimage_image tile;
    double *shape, *alpha, *color;
    unsigned channels;
    double inverse[6], xstep, ystep;
    int shading, uncolored, deferred, gray;
    pdf_blend_space blend;
    pdf_object_id resource_object;
    pdf_object_view definition;
    unsigned char *program;
    size_t program_length, definition_bytes, surface_bytes;
    struct pdf_graphics_state *initial;
    struct pdf_transparency_group *surface;
    double matrix[6], bounds[4], base[PDF_COLOR_COMPONENTS];
    pdf_image_colors base_space;
    uint64_t serial;
    float initial_ctm[6];
    double page_x0, page_top;
} pdf_pattern;

typedef struct {
    pdf_pattern *head;
    size_t bytes_used;
    unsigned count;
} pdf_pattern_store;

typedef struct {
    unsigned references;
    uint32_t width, height;
    size_t bytes;
    double values[];
} pdf_soft_mask;

typedef struct pdf_graphics_state {
    float ctm[6];
    double font_size_pts, leading_pts, char_space, word_space, horizontal_scale, text_rise;
    int text_mode;
    int rendering_intent; /* Relative, perceptual, saturation, absolute. */
    const pdf_color_function *black_generation, *undercolor_removal;
    pdf_soft_mask *soft_mask;
    const pdf_font_map *current_font;
    double fill_color[PDF_COLOR_COMPONENTS], stroke_color[PDF_COLOR_COMPONENTS];
    float line_width_pts;
    int line_cap, line_join, dash_count;
    double miter_limit, flatness, dash_phase, dash[32];
    double fill_alpha, stroke_alpha;
    int blend_mode, alpha_is_shape, text_knockout;
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
    size_t transparency_bytes_live;
    uint64_t paint_serial;
    size_t outline_cache_bytes_left;
    pdf_page_hint *hint_cache;
} pdf_form_budget;

typedef struct pdf_transparency_group pdf_transparency_group;

typedef struct {
    cupidimage_image *img;
    pdf_transparency_group *group, *text_group;
    double *shape_plane, *alpha_plane;
    int pattern_deferred, *pattern_needs_backdrop;
    uint64_t paint_serial;
    const unsigned char *document;
    size_t document_size;
    const pdf_xref *xref;
    pdf_object_id page_object;
    const pdf_object_view *resource_override;
    int image_failed;
    int optional_hidden;
    pdf_optional_content *optional_content;
    int paint_coverage_clipped;
    int text_antialias, truetype_hinting, cff_hinting, type1_hinting, graphics_antialias;
    char *error;
    size_t error_capacity;
    float page_x0_pts;
    float page_top_pts;
    double text_matrix[6], text_line_matrix[6];
    pdf_graphics_state gs;
    pdf_graphics_state initial_gs;
    pdf_pattern_store *patterns;
    pdf_icc_store *icc_store;
    pdf_color_function_store *color_functions;
    pdf_tint_store *tints;
    pdf_palette_store *palettes;
    unsigned pattern_depth;
    int pattern_uncolored, mask_rendering;
    pdf_object_id mask_root_object;
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
    pdf_object_id form_objects[16];
    pdf_form_clip form_clips[16];
    int form_depth;
    int glyph_depth;
    int glyph_active, glyph_seen_metrics, glyph_uncolored;
    double glyph_expected_width;
    pdf_form_budget *form_budget;
} pdf_render_state;

static inline int cupidimage_pdf_clip_antialias(const pdf_render_state *st) {
    return st->text_antialias || st->graphics_antialias;
}

const pdf_blend_space *cupidimage_pdf_current_blend(const pdf_render_state *);
int cupidimage_pdf_blend_prepare(const pdf_render_state *, const pdf_image_colors *,
                                 pdf_blend_space *);
int cupidimage_pdf_blend_equal(const pdf_blend_space *, const pdf_blend_space *);
int cupidimage_pdf_color_function_state(pdf_render_state *, const pdf_object_view *,
                                        pdf_graphics_state *);
int cupidimage_pdf_rgb_to_cmyk(const pdf_render_state *, const double[3], int, double[4]);
void cupidimage_pdf_color_functions_free(pdf_color_function_store *);

int cupidimage_pdf_blend_convert(const pdf_render_state *, const pdf_image_colors *,
                                 const pdf_blend_space *, const double *, double *);
int cupidimage_pdf_blend_luminosity(const pdf_render_state *, const pdf_blend_space *,
                                    const double *, double *);

#define PDF_TRANSPARENCY_MEMORY_LIMIT (256u * 1024u * 1024u)

struct pdf_transparency_group {
    cupidimage_image image;
    double *alpha, *color, *shape;
    unsigned channels;
    const pdf_transparency_group *parent;
    const cupidimage_image *backdrop;
    int isolated, gray, knockout, lazy_backdrop;
    pdf_blend_space blend;
    unsigned left, top, right, bottom;
    size_t bytes;
};

typedef struct {
    pdf_transparency_group surface;
    pdf_soft_mask *mask;
} pdf_paint_group;

int cupidimage_pdf_fill_stroke_begin(pdf_render_state *, pdf_paint_group *, int);
int cupidimage_pdf_fill_stroke_end(pdf_render_state *, pdf_paint_group *, int);
void cupidimage_pdf_group_paint(pdf_transparency_group *group, size_t pixel, const double *source,
                                double alpha, double shape, int blend_mode);
int cupidimage_pdf_intent_name(const char *);
int cupidimage_pdf_read_intent(const pdf_render_state *, const pdf_object_view *, const char *,
                               int *);
int cupidimage_pdf_dictionary_boolean(const pdf_render_state *st, const pdf_object_view *view,
                                      const char *key, int *value);
int cupidimage_pdf_group_space(const pdf_render_state *st, const pdf_object_view *attributes,
                               int required, pdf_blend_space *blend);
int cupidimage_pdf_group_surface(const pdf_render_state *parent, pdf_transparency_group *group,
                                 int isolated, const double *background,
                                 const pdf_blend_space *blend);
void cupidimage_pdf_soft_mask_retain(pdf_soft_mask *mask);
void cupidimage_pdf_soft_mask_release(pdf_render_state *st, pdf_soft_mask *mask);
int cupidimage_pdf_load_soft_mask(pdf_render_state *st, const pdf_graphics_state *graphics,
                                  const pdf_object_view *view, pdf_soft_mask **mask, int *replace);

int cupidimage_pdf_group_begin(pdf_render_state *child, const pdf_render_state *parent,
                               const pdf_object_view *form, pdf_transparency_group *group);
int cupidimage_pdf_page_group_begin(pdf_render_state *st, pdf_transparency_group *group);
void cupidimage_pdf_group_source(const pdf_transparency_group *group, size_t pixel, double *color,
                                 double *alpha, double *shape);
int cupidimage_pdf_group_finish(pdf_render_state *parent, const pdf_transparency_group *group);
void cupidimage_pdf_group_free(pdf_render_state *parent, pdf_transparency_group *group);

int cupidimage_pdf_text_group_begin(pdf_render_state *st);
int cupidimage_pdf_text_group_end(pdf_render_state *st);
void cupidimage_pdf_text_group_discard(pdf_render_state *st);
int cupidimage_pdf_type3_stencil_finish(pdf_render_state *st, const pdf_transparency_group *group);
int cupidimage_pdf_composite_implicit_group(pdf_render_state *st,
                                            const pdf_transparency_group *group);

void cupidimage_pdf_matrix_identity(float[6]);
void cupidimage_pdf_matrix_concat(float[6], const float[6]);
int cupidimage_pdf_form_matrix(const pdf_render_state *, const pdf_object_view *, float[6]);
int cupidimage_pdf_draw_appearance(pdf_render_state *, pdf_object_id, const pdf_object_view *);
int cupidimage_pdf_render_annotations(pdf_render_state *, const pdf_page_geometry *);
int cupidimage_pdf_appearance_group_begin(pdf_render_state *, const pdf_render_state *,
                                          const pdf_object_view *, pdf_transparency_group *);
int cupidimage_pdf_draw_form(pdf_render_state *st, pdf_object_id object,
                             const pdf_object_view *form);
int cupidimage_pdf_image_dictionary(const pdf_render_state *st, const pdf_object_view *parent,
                                    const char *key, pdf_object_view *result);
int cupidimage_pdf_image_integer(const pdf_render_state *st, const pdf_object_view *view,
                                 const char *key, int *value);
int cupidimage_pdf_image_stream(const pdf_render_state *st, const pdf_object_view *view,
                                const unsigned char **stream, size_t *length);
int cupidimage_pdf_draw_image(pdf_render_state *st, const char *name);
uint8_t cupidimage_pdf_clamp_u8(double v);
void cupidimage_pdf_user_to_pixel(const pdf_render_state *st, float ux, float uy, float *px,
                                  float *py);
const unsigned char *cupidimage_pdf_palette_intern(pdf_palette_store *, const unsigned char *,
                                                   size_t);
void cupidimage_pdf_palettes_free(pdf_palette_store *);
int cupidimage_pdf_color_space_value(const pdf_render_state *, const pdf_object_view *, size_t *,
                                     unsigned, pdf_image_colors *, int);
int cupidimage_pdf_tint_space(const pdf_render_state *, const pdf_object_view *, size_t, size_t *,
                              unsigned, pdf_image_colors *, int, int);
int cupidimage_pdf_tint_none(const pdf_tint *);
void cupidimage_pdf_tint_free(pdf_tint_store *);
void cupidimage_pdf_tint_paint(pdf_render_state *, int, int, const double *, const pdf_tint *,
                               double, double);
int cupidimage_pdf_empty_color(const pdf_image_colors *, const pdf_pattern *);
void cupidimage_pdf_device_color(const double values[4], int components, int gray, double out[3]);
int cupidimage_pdf_calibrated_output(pdf_image_colors *);
int cupidimage_pdf_icc_space(const pdf_render_state *, pdf_object_id, pdf_image_colors *);
/* Raw embedded profiles: 1 supported, 0 unsupported, -1 resource failure. */
int cupidimage_pdf_icc_bytes(const pdf_render_state *, const unsigned char *, size_t, unsigned,
                             pdf_image_colors *);
void cupidimage_pdf_calibrated_xyz(const pdf_image_colors *, const double[4], double[3]);
void cupidimage_pdf_xyz_color(const pdf_image_colors *, const double[3], int, double[3]);
int cupidimage_pdf_icc_color(const pdf_icc_profile *, const double[4], int, int, double[3]);
int cupidimage_pdf_icc_working_xyz(const pdf_icc_profile *, const double[4], int, double[3]);
int cupidimage_pdf_icc_luminosity(const pdf_icc_profile *, const double[4], int, double *);
int cupidimage_pdf_icc_equal(const pdf_icc_profile *, const pdf_icc_profile *);
int cupidimage_pdf_icc_bidirectional(pdf_icc_profile *, int, size_t *);
int cupidimage_pdf_icc_from_working(pdf_icc_profile *, const double[3], int, double *, size_t *);
void cupidimage_pdf_icc_free(pdf_icc_store *);
int cupidimage_pdf_calibrated_space(const pdf_render_state *, const pdf_object_view *, size_t *,
                                    int, pdf_image_colors *);
void cupidimage_pdf_color_range(const pdf_image_colors *, int, double *, double *);
double cupidimage_pdf_color_component(const pdf_image_colors *, int, double);
double cupidimage_pdf_color_palette_value(const pdf_image_colors *, int, unsigned char);
void cupidimage_pdf_calibrated_color(const pdf_image_colors *, const double[4], int, double[3]);
void cupidimage_pdf_composite_space(pdf_render_state *, int, int, const double[4],
                                    const pdf_image_colors *, double, double);
void cupidimage_pdf_composite_device(pdf_render_state *st, int x, int y, const double values[4],
                                     int components, double alpha, double shape);

int cupidimage_pdf_image_color_space(const pdf_render_state *st, const pdf_object_view *view,
                                     size_t *pos, unsigned depth, pdf_image_colors *colors);

int cupidimage_pdf_color_space(const pdf_render_state *st, const char *name, unsigned depth,
                               pdf_image_colors *colors);

int cupidimage_pdf_apply_extgstate(pdf_render_state *st, const char *name);
void cupidimage_pdf_blend_components(int, const double *, const double *, double[4], unsigned);
void cupidimage_pdf_blend_colors(int mode, const double source[3], const double backdrop[3],
                                 double result[3]);
int cupidimage_pdf_pixel_visible(const pdf_render_state *, int, int);
void cupidimage_pdf_composite_color(pdf_render_state *st, int x, int y, const double source[3],
                                    double alpha, double shape);
void cupidimage_pdf_blend_rgb(int mode, const uint8_t source[3], const uint8_t backdrop[3],
                              double result[3]);
void cupidimage_pdf_composite_pixel(pdf_render_state *st, int x, int y, uint8_t r, uint8_t g,
                                    uint8_t b, double alpha, double shape);

int cupidimage_pdf_graphics_number(const pdf_render_state *st, const pdf_object_view *view,
                                   const char *key, double minimum, double maximum, double *number);
int cupidimage_pdf_read_number(const pdf_render_state *st, const pdf_object_view *view, size_t pos,
                               unsigned depth, double *value);
int cupidimage_pdf_shading_colors(const pdf_render_state *st, const pdf_object_view *view,
                                  pdf_image_colors *colors);
int cupidimage_pdf_paint_mesh(pdf_render_state *st, const pdf_object_view *view, int type,
                              int background);
int cupidimage_pdf_draw_shading(pdf_render_state *st, const char *name);

/* Borrowed image objects and bounded inline-image syntax. */
int cupidimage_pdf_draw_image_view(pdf_render_state *st, const pdf_object_view *view);
int cupidimage_pdf_draw_inline_image(pdf_render_state *st, const unsigned char *data, size_t length,
                                     size_t *position);

int cupidimage_pdf_text_clip_prepare(pdf_render_state *st);
void cupidimage_pdf_clip_retain(pdf_clip_mask *);
void cupidimage_pdf_clip_release(pdf_render_state *, pdf_clip_mask *);
int cupidimage_pdf_clip_capture_forms(pdf_render_state *);
int cupidimage_pdf_clip_limit_mask(pdf_render_state *, const unsigned char *, int, unsigned char *,
                                   int, int, int, int);
int cupidimage_pdf_clip_append_edge_to(pdf_render_state *, pdf_clip_mask *, double, double, double,
                                       double);
void cupidimage_pdf_clip_pending_clear(pdf_render_state *, pdf_clip_mask *);
int cupidimage_pdf_optional_resource(pdf_render_state *, const char *, int *);
int cupidimage_pdf_optional_dictionary_visibility(pdf_render_state *, const pdf_object_view *,
                                                  int *);
int cupidimage_pdf_optional_object_visibility(pdf_render_state *, const pdf_object_view *, int *);
/* clear_bits is required for text masks with provisional glyph-winding bits. */
int cupidimage_pdf_clip_render_geometry(pdf_render_state *, pdf_clip_mask *, int clear_bits);
int cupidimage_pdf_clip_contains_crop(pdf_render_state *, int, int, int, int);
int cupidimage_pdf_clip_intersect(pdf_render_state *, const pdf_region_edge *, unsigned, unsigned,
                                  const pdf_region_ellipse *, unsigned, const double[4], int, int,
                                  int, int, unsigned char **);
int cupidimage_pdf_clip_append_edge(pdf_render_state *, double, double, double, double);
int cupidimage_pdf_clip_path_geometry(pdf_render_state *, pdf_clip_mask *, const pdf_path *, int);
int cupidimage_pdf_clip_finish_geometry(pdf_render_state *, pdf_clip_mask *);
void cupidimage_pdf_text_end(pdf_render_state *st);
void cupidimage_pdf_stroke_segments(pdf_render_state *st, const pdf_path *path,
                                    unsigned char *target);
int cupidimage_pdf_stroke_path_coverage(pdf_render_state *, const pdf_path *, int, int, int, int,
                                        int, unsigned char **);
void cupidimage_pdf_text_begin(pdf_render_state *st);
void cupidimage_pdf_text_move(pdf_render_state *st, double x, double y);
void cupidimage_pdf_text_advance(pdf_render_state *st, double distance);
void cupidimage_pdf_render_text_string(pdf_render_state *st, const char *text, size_t length);

int cupidimage_pdf_state_object(const pdf_render_state *st, pdf_object_id object,
                                pdf_object_view *view);
/* Returns 1 for a resource, 0 for absence, and -1 for malformed/failed lookup. */
int cupidimage_pdf_find_resource_checked(const pdf_render_state *st, const char *category,
                                         const char *name, pdf_object_view *value,
                                         pdf_object_id *reference);
int cupidimage_pdf_find_resource(const pdf_render_state *st, const char *category, const char *name,
                                 pdf_object_view *value, pdf_object_id *reference);

int cupidimage_pdf_draw_type3_glyph(pdf_render_state *st, const pdf_font_map *font, unsigned code);

int cupidimage_pdf_select_pattern(pdf_render_state *st, const char *name, int stroke);
void cupidimage_pdf_patterns_free(pdf_pattern_store *store);
void cupidimage_pdf_pattern_surface_release(pdf_render_state *st, pdf_pattern *pattern);
void cupidimage_pdf_patterns_end_paint(pdf_render_state *st);
int cupidimage_pdf_pattern_sample(pdf_render_state *st, pdf_pattern *pattern, double x, double y,
                                  double rgb[4], double *shape, double *alpha);
void cupidimage_pdf_matrix_concat_double(double m[6], const double n[6]);
int cupidimage_pdf_paint_path_coverage(pdf_render_state *, const pdf_path *, int, int, int);
int cupidimage_pdf_path_fill_edges(const pdf_path *, size_t *, pdf_region_edge **, unsigned *);
/* Init detaches borrowed storage after a state copy; clear releases owned storage. */
void cupidimage_pdf_path_init(pdf_path *path);
void cupidimage_pdf_path_clear(pdf_path *path);
int cupidimage_pdf_path_reserve(pdf_path *path, unsigned additional);
void cupidimage_pdf_path_close(pdf_path *path);
void cupidimage_pdf_path_move_user(pdf_render_state *state, float x, float y);
void cupidimage_pdf_path_line_user(pdf_render_state *state, float x, float y);
void cupidimage_pdf_path_curve_to(pdf_render_state *state, float x1, float y1, float x2, float y2,
                                  float x3, float y3, int first_is_current);
int cupidimage_pdf_path_flatten(pdf_render_state *state);
void cupidimage_pdf_render_state_release(pdf_render_state *st);
int cupidimage_pdf_render_stream(pdf_render_state *st, const unsigned char *stream, size_t len);
int cupidimage_pdf_render_pattern_page(pdf_render_state *parent, pdf_pattern *pattern);
int cupidimage_pdf_refresh_pattern_cache(pdf_render_state *st, pdf_pattern *pattern);
int cupidimage_pdf_render_pattern_cell(pdf_render_state *parent, const pdf_object_view *pattern,
                                       const unsigned char *stream, size_t length,
                                       pdf_pattern *cache, double sx, double sy, double ox,
                                       double oy, const double bbox[4], int uncolored);
void cupidimage_pdf_paint_pixel(pdf_render_state *st, int x, int y, int stroke, double coverage);

int cupidimage_pdf_paint_shading_view(pdf_render_state *st, const pdf_object_view *view,
                                      int background);
int cupidimage_pdf_apply_shading_graphics_view(pdf_render_state *st, const pdf_object_view *view);
int cupidimage_pdf_apply_graphics_view(pdf_render_state *st, const pdf_object_view *view);
int cupidimage_pdf_render_shading_pattern(pdf_render_state *parent, const pdf_object_view *pattern,
                                          const double matrix[6], pdf_pattern *cache,
                                          pdf_transparency_group *live);

int cupidimage_pdf_set_dash(pdf_graphics_state *gs, const unsigned char *data, size_t length,
                            double phase);

int cupidimage_pdf_set_flatness(pdf_graphics_state *state, double value);
double cupidimage_pdf_flatness(const pdf_graphics_state *state, double ceiling);
int cupidimage_pdf_apply_stroke_view(const pdf_render_state *st, const pdf_object_view *view,
                                     pdf_graphics_state *next);

int cupidimage_pdf_draw_font_program_glyph(pdf_render_state *st, pdf_font_program *program,
                                           unsigned glyph);

#endif
