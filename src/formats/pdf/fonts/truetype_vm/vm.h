#ifndef CUPIDIMAGE_PDF_TT_VM_H
#define CUPIDIMAGE_PDF_TT_VM_H
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PDF_TT_VM_OK,
    PDF_TT_VM_MEMORY,
    PDF_TT_VM_PROGRAM,
    PDF_TT_VM_STACK,
    PDF_TT_VM_STORAGE,
    PDF_TT_VM_FUNCTION,
    PDF_TT_VM_ARITHMETIC,
    PDF_TT_VM_BUDGET,
    PDF_TT_VM_UNSUPPORTED,
    PDF_TT_VM_SIZE,
    PDF_TT_VM_POINT
} pdf_tt_vm_error;

typedef struct {
    const unsigned char *data;
    size_t length;
    int defined;
} pdf_tt_vm_function;

typedef struct {
    int period, phase, threshold;
    int off;
} pdf_tt_vm_round;

typedef struct {
    pdf_tt_vm_round rounding;
    int32_t projection[2], freedom[2], dual_projection[2];
    uint32_t reference[3], zone[3], loop, delta_base, delta_shift;
    int32_t minimum_distance, cvt_cut_in, single_width_cut_in;
    int auto_flip;
    unsigned instruction_control, scan_flags, scan_type;
    int32_t single_width;
} pdf_tt_vm_graphics;

typedef enum { PDF_TT_VM_GLYPH = 0, PDF_TT_VM_FONT = 1, PDF_TT_VM_PREP = 2 } pdf_tt_vm_stage;

typedef struct {
    int32_t scale, point_size;
    unsigned ppem;
    int configured, grayscale;
    int32_t axis_scale[2], axis_ratio[2];
    unsigned transform_flags;
} pdf_tt_vm_size_state;

typedef struct {
    int32_t current[2], original[2];
    unsigned char touched, on, end;
} pdf_tt_vm_point;

typedef struct {
    pdf_tt_vm_point *points;
    unsigned count, outline_count;
    int32_t (*design)[2];
    int32_t design_scale, design_scale_y;
} pdf_tt_vm_zone;

typedef struct {
    int32_t *stack, *storage, *cvt;
    int32_t *prepared_storage, *prepared_cvt;
    pdf_tt_vm_function *functions, *instructions;
    unsigned instruction_limit, instruction_count;
    unsigned stack_size, stack_limit, storage_count, cvt_count, function_count;
    pdf_tt_vm_graphics graphics, prepared_graphics;
    int prepared;
    pdf_tt_vm_size_state size;
    pdf_tt_vm_stage stage;
    pdf_tt_vm_zone zones[2];
    size_t work_left;
    pdf_tt_vm_error error;
    size_t error_offset;
    unsigned error_opcode;
} pdf_tt_vm;

/* Definition bodies borrow program bytes, which must outlive the VM.
 * A failed execution invalidates its state; destroy/reinitialize before reuse. */
int cupidimage_pdf_tt_vm_init(pdf_tt_vm *, unsigned, unsigned, const int32_t *, unsigned, unsigned,
                              unsigned);
void cupidimage_pdf_tt_vm_free(pdf_tt_vm *);
int cupidimage_pdf_tt_vm_run(pdf_tt_vm *, const unsigned char *, size_t, pdf_tt_vm_stage, size_t);

enum { PDF_TT_VM_ROTATED = 1, PDF_TT_VM_STRETCHED = 2 };

/* CVT inputs are unscaled font units. Transform flags describe an additional
 * transform; differing device-axis sizes do not infer those flags. */
int cupidimage_pdf_tt_vm_set_size(pdf_tt_vm *, unsigned, int32_t, int32_t, int, const int32_t *);
int cupidimage_pdf_tt_vm_set_size_xy(pdf_tt_vm *, unsigned, int32_t, int32_t, int32_t, int,
                                     unsigned, const int32_t *);
/* Geometry is already scaled to F26Dot6. Glyph count may include phantom points. */
int cupidimage_pdf_tt_vm_set_zones(pdf_tt_vm *, unsigned, const pdf_tt_vm_point *, unsigned,
                                   unsigned);
/* Optional original design plane, copied with its positive 16.16 uniform scale.
 * Without it, original coordinates describe an already scaled outline (as for
 * a composite instruction phase). Installing new points clears this plane. */
int cupidimage_pdf_tt_vm_set_design(pdf_tt_vm *, const int32_t (*)[2], unsigned, int32_t);
int cupidimage_pdf_tt_vm_set_design_xy(pdf_tt_vm *, const int32_t (*)[2], unsigned, int32_t,
                                       int32_t);
int cupidimage_pdf_tt_vm_save_preparation(pdf_tt_vm *);
int cupidimage_pdf_tt_vm_begin_glyph(pdf_tt_vm *, const pdf_tt_vm_point *, unsigned, unsigned);
/* Component/parent instruction phases reset graphics and geometry but share
 * the enclosing glyph's private CVT/storage until the next begin_glyph call. */
int cupidimage_pdf_tt_vm_begin_component(pdf_tt_vm *, const pdf_tt_vm_point *, unsigned, unsigned);
#endif
