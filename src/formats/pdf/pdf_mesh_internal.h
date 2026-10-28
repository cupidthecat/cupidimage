#ifndef CUPIDIMAGE_PDF_MESH_INTERNAL_H
#define CUPIDIMAGE_PDF_MESH_INTERNAL_H
#include "pdf_functions.h"

typedef struct {
    double *values;
    int painted;
} pdf_mesh_sample;

typedef struct {
    pdf_render_state *state;
    pdf_image_colors colors;
    pdf_function_tree *function;
    pdf_mesh_sample *pixels;
    double decode[4 + 2 * PDF_COLOR_COMPONENTS], box[4], inverse[6];
    int has_box, samples, interpolate, coordinate_bits, component_bits, flag_bits;
} pdf_mesh_context;

uint32_t cupidimage_pdf_mesh_bits(const unsigned char *, unsigned *, unsigned);
double cupidimage_pdf_mesh_decode(uint32_t, unsigned, const double *);
int cupidimage_pdf_mesh_inside_box(const pdf_mesh_context *, unsigned, unsigned);
void cupidimage_pdf_mesh_color(const pdf_image_colors *, const double *, pdf_mesh_sample *, int);
int cupidimage_pdf_paint_patches(pdf_mesh_context *, const unsigned char *, size_t, int, int);
#endif
