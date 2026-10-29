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
    PDF_TT_VM_SIZE
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
} pdf_tt_vm_size_state;

typedef struct {
    int32_t *stack, *storage, *cvt;
    pdf_tt_vm_function *functions;
    unsigned stack_size, stack_limit, storage_count, cvt_count, function_count;
    pdf_tt_vm_graphics graphics;
    pdf_tt_vm_size_state size;
    pdf_tt_vm_stage stage;
    size_t work_left;
    pdf_tt_vm_error error;
    size_t error_offset;
    unsigned error_opcode;
} pdf_tt_vm;

/* Definition bodies borrow program bytes, which must outlive the VM.
 * A failed execution invalidates its state; destroy/reinitialize before reuse. */
int cupidimage_pdf_tt_vm_init(pdf_tt_vm *, unsigned, unsigned, const int32_t *, unsigned, unsigned);
void cupidimage_pdf_tt_vm_free(pdf_tt_vm *);
int cupidimage_pdf_tt_vm_run(pdf_tt_vm *, const unsigned char *, size_t, pdf_tt_vm_stage, size_t);
/* Uniform size preparation only. CVT inputs are unscaled font-unit values. */
int cupidimage_pdf_tt_vm_set_size(pdf_tt_vm *, unsigned, int32_t, int32_t, int, const int32_t *);
#endif
