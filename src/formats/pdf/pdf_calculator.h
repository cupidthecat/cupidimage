#ifndef CUPIDIMAGE_PDF_CALCULATOR_H
#define CUPIDIMAGE_PDF_CALCULATOR_H
#include <stddef.h>
typedef struct pdf_calculator_program pdf_calculator_program;
pdf_calculator_program *cupidimage_pdf_calculator_compile(const unsigned char *data, size_t length);
size_t cupidimage_pdf_calculator_bytes(const pdf_calculator_program *program);
int cupidimage_pdf_calculator_evaluate(const pdf_calculator_program *program, const double *input,
                                       int inputs, double *output, int outputs, size_t *budget);
#endif
