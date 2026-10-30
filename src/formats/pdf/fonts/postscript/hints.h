#ifndef CUPIDIMAGE_PDF_POSTSCRIPT_HINTS_H
#define CUPIDIMAGE_PDF_POSTSCRIPT_HINTS_H

/* Private DICT fields shared by CFF and PostScript font hinting. */
enum pdf_ps_hint_field {
    PDF_PS_BLUE_VALUES,
    PDF_PS_OTHER_BLUES,
    PDF_PS_FAMILY_BLUES,
    PDF_PS_FAMILY_OTHER_BLUES,
    PDF_PS_STEM_SNAP_H,
    PDF_PS_STEM_SNAP_V,
    PDF_PS_BLUE_SCALE,
    PDF_PS_BLUE_SHIFT,
    PDF_PS_BLUE_FUZZ,
    PDF_PS_STD_HW,
    PDF_PS_STD_VW,
    PDF_PS_FORCE_BOLD,
    PDF_PS_LANGUAGE_GROUP,
    PDF_PS_EXPANSION_FACTOR,
    PDF_PS_HINT_FIELDS
};

typedef struct {
    double arrays[6][14];
    unsigned counts[6];
    /* Scalar indices are the corresponding field minus PDF_PS_BLUE_SCALE. */
    double scalars[8];
    unsigned seen, invalid;
} pdf_ps_private_hints;

void cupidimage_pdf_ps_hints_init(pdf_ps_private_hints *hints);
void cupidimage_pdf_ps_hint_set(pdf_ps_private_hints *hints, unsigned field, const double *values,
                                unsigned count, int delta, int valid);

enum {
    PDF_PS_HINT_LATE_STEMS = 1,
    PDF_PS_HINT_BAD_WIDTH = 2,
    PDF_PS_HINT_EMPTY_MASK = 4,
    PDF_PS_HINT_MASK_PADDING = 8,
    PDF_PS_HINT_STEM_ORDER = 16
};

typedef struct {
    double start, width;
    unsigned vertical;
} pdf_ps_stem;

typedef struct {
    unsigned segment, stem_count, counter;
    unsigned char mask[12];
} pdf_ps_hint_event;

typedef struct {
    pdf_ps_stem stems[96];
    unsigned stem_count, diagnostics;
    pdf_ps_hint_event *events;
    unsigned event_count, event_capacity;
} pdf_ps_hint_program;

void cupidimage_pdf_ps_hint_program_free(pdf_ps_hint_program *program);
#endif
