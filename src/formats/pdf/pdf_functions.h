#ifndef CUPIDIMAGE_PDF_FUNCTIONS_H
#define CUPIDIMAGE_PDF_FUNCTIONS_H
#include "pdf_render_internal.h"

#define PDF_FUNCTION_NODES 64
#define PDF_FUNCTION_PARTS 32

typedef struct {
    int type, inputs, components, has_range, count;
    double domain[4], range[8], c0[4], c1[4], exponent;
    double bounds[PDF_FUNCTION_PARTS-1], encode[PDF_FUNCTION_PARTS*2];
    int children[PDF_FUNCTION_PARTS];
    int size[2], bits;
    double decode[8];
    unsigned char *samples;
} pdf_function_node;

typedef struct {
    pdf_function_node nodes[PDF_FUNCTION_NODES];
    int count;
    size_t sample_bytes;
} pdf_function_tree;

int cupidimage_pdf_function_array(const pdf_render_state *st, const pdf_object_view *parent,
                                  const char *key, double *values, int count);
int cupidimage_pdf_load_function(const pdf_render_state *st, const pdf_object_view *parent,
                                 const char *key, int inputs, int components, pdf_function_tree *tree);
int cupidimage_pdf_evaluate_function(const pdf_function_tree *tree, const double input[2],
                                     double output[4], size_t *budget);
/* Initialize trees to zero and release them after use, including failed loads. */
void cupidimage_pdf_free_function(pdf_function_tree *tree);
#endif
