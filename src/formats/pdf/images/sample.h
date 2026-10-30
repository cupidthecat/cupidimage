#ifndef CUPIDIMAGE_PDF_IMAGE_SAMPLE_H
#define CUPIDIMAGE_PDF_IMAGE_SAMPLE_H

typedef struct {
    unsigned x, y;
    double weight;
} pdf_image_tap;

/* Unit-square image coordinates: u increases right, v increases upward.
 * Valid outside points succeed with no taps. Samples are centred in their cells. */
int cupidimage_pdf_image_taps(unsigned width, unsigned height, double u, double v, int interpolate,
                              pdf_image_tap taps[4], unsigned *count);

#endif
