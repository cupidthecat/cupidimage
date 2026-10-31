#ifndef CUPIDIMAGE_PDF_POSTSCRIPT_TYPE1_FLEX_H
#define CUPIDIMAGE_PDF_POSTSCRIPT_TYPE1_FLEX_H

/* Standard Type 1 OtherSubrs flex selection and retained-curve adjustment.
 * Controls are in character space; matrix includes the actual device origin.
 * Outputs remain unchanged on failure. Erosion is an explicit device parameter. */
int cupidimage_pdf_ps_type1_flex(const double input[12], const double reference[2],
                                 double threshold, const double matrix[6], int stroked,
                                 double erosion, double output[12], int *line);
#endif
