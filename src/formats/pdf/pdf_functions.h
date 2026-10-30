#ifndef CUPIDIMAGE_PDF_FUNCTIONS_H
#define CUPIDIMAGE_PDF_FUNCTIONS_H
#include "pdf_calculator.h"
#include "pdf_render_internal.h"

#define PDF_FUNCTION_NODES 64
#define PDF_FUNCTION_PARTS 32

typedef struct {
    int type, inputs, components, has_range, count;
    double domain[2 * PDF_COLOR_COMPONENTS], range[2 * PDF_COLOR_COMPONENTS];
    double c0[PDF_COLOR_COMPONENTS], c1[PDF_COLOR_COMPONENTS], exponent;
    double bounds[PDF_FUNCTION_PARTS - 1], encode[PDF_FUNCTION_PARTS * 2];
    int children[PDF_FUNCTION_PARTS];
    int size[PDF_COLOR_COMPONENTS], bits;
    double decode[2 * PDF_COLOR_COMPONENTS];
    unsigned char *samples;
    pdf_calculator_program *calculator;
} pdf_function_node;

typedef struct {
    pdf_function_node nodes[PDF_FUNCTION_NODES];
    int count;
    size_t sample_bytes;
} pdf_function_tree;

int cupidimage_pdf_function_array(const pdf_render_state *st, const pdf_object_view *parent,
                                  const char *key, double *values, int count);
int cupidimage_pdf_load_function(const pdf_render_state *st, const pdf_object_view *parent,
                                 const char *key, int inputs, int components,
                                 pdf_function_tree *tree);
int cupidimage_pdf_load_function_value(const pdf_render_state *, const pdf_object_view *, size_t *,
                                       int, int, pdf_function_tree *);
size_t cupidimage_pdf_function_bytes(const pdf_function_tree *);
int cupidimage_pdf_evaluate_function(const pdf_function_tree *tree, const double *input,
                                     double *output, size_t *budget);
/* Initialize trees to zero and release them after use, including failed loads. */
void cupidimage_pdf_free_function(pdf_function_tree *tree);
#endif
