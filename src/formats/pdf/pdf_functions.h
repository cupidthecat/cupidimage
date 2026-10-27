#ifndef CUPIDIMAGE_PDF_FUNCTIONS_H
#define CUPIDIMAGE_PDF_FUNCTIONS_H
#include "pdf_render_internal.h"

#define PDF_FUNCTION_NODES 64
#define PDF_FUNCTION_PARTS 32

typedef struct {
    int type, components, has_range, count;
    double domain[2], range[8], c0[4], c1[4], exponent;
    double bounds[PDF_FUNCTION_PARTS-1], encode[PDF_FUNCTION_PARTS*2];
    int children[PDF_FUNCTION_PARTS];
} pdf_function_node;

typedef struct {
    pdf_function_node nodes[PDF_FUNCTION_NODES];
    int count;
} pdf_function_tree;

int cupidimage_pdf_function_array(const pdf_render_state *st, const pdf_object_view *parent,
                                  const char *key, double *values, int count);
int cupidimage_pdf_load_function(const pdf_render_state *st, const pdf_object_view *parent,
                                 const char *key, int components, pdf_function_tree *tree);
int cupidimage_pdf_evaluate_function(const pdf_function_tree *tree, double input,
                                     double output[4], size_t *budget);
#endif
