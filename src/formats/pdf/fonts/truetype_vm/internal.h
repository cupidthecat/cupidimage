#ifndef CUPIDIMAGE_PDF_TT_VM_INTERNAL_H
#define CUPIDIMAGE_PDF_TT_VM_INTERNAL_H
#include "vm.h"

int cupidimage_pdf_tt_vm_fail(pdf_tt_vm *, pdf_tt_vm_error);
int cupidimage_pdf_tt_vm_push(pdf_tt_vm *, int64_t);
int cupidimage_pdf_tt_vm_need(pdf_tt_vm *, unsigned);
int cupidimage_pdf_tt_vm_stack_op(pdf_tt_vm *, unsigned);
int cupidimage_pdf_tt_vm_numeric_op(pdf_tt_vm *, unsigned);
void cupidimage_pdf_tt_vm_graphics_defaults(pdf_tt_vm_graphics *);
int cupidimage_pdf_tt_vm_graphics_op(pdf_tt_vm *, unsigned);
int cupidimage_pdf_tt_vm_size_op(pdf_tt_vm *, unsigned);
int cupidimage_pdf_tt_vm_vector_op(pdf_tt_vm *, unsigned);
int64_t cupidimage_pdf_tt_vm_round(const pdf_tt_vm_round *, int32_t);
int cupidimage_pdf_tt_vm_normalize(pdf_tt_vm *, int32_t, int32_t, int32_t[2]);
int cupidimage_pdf_tt_vm_point_op(pdf_tt_vm *, unsigned);
int cupidimage_pdf_tt_vm_load_glyph_points(pdf_tt_vm *, const pdf_tt_vm_point *, unsigned,
                                           unsigned);
pdf_tt_vm_point *cupidimage_pdf_tt_vm_get_point(pdf_tt_vm *, unsigned, int32_t);
/* Bounded coordinate/F2Dot14 products; denominator must be nonzero. */
int64_t cupidimage_pdf_tt_vm_divide(int64_t, int64_t);
int cupidimage_pdf_tt_vm_project(pdf_tt_vm *, const int32_t[2], const int32_t[2], int32_t *);
int cupidimage_pdf_tt_vm_move(pdf_tt_vm *, pdf_tt_vm_point *, int64_t);
int cupidimage_pdf_tt_vm_shift(pdf_tt_vm *, pdf_tt_vm_point *, int64_t, int64_t);
int cupidimage_pdf_tt_vm_movement_op(pdf_tt_vm *, unsigned);
int cupidimage_pdf_tt_vm_distance(pdf_tt_vm *, const int32_t[2], const int32_t[2], const int32_t[2],
                                  int32_t *);
int cupidimage_pdf_tt_vm_relative_op(pdf_tt_vm *, unsigned);
int cupidimage_pdf_tt_vm_design_distance(pdf_tt_vm *, unsigned, int32_t, unsigned, int32_t,
                                         int32_t *);
int cupidimage_pdf_tt_vm_direct_op(pdf_tt_vm *, unsigned);
int cupidimage_pdf_tt_vm_interpolate_op(pdf_tt_vm *, unsigned);
int cupidimage_pdf_tt_vm_contour_op(pdf_tt_vm *, unsigned);
int cupidimage_pdf_tt_vm_flags_op(pdf_tt_vm *, unsigned);
int cupidimage_pdf_tt_vm_intersection_op(pdf_tt_vm *, unsigned);
int cupidimage_pdf_tt_vm_current_ppem(pdf_tt_vm *, unsigned *);
int cupidimage_pdf_tt_vm_cvt_read(pdf_tt_vm *, int32_t, int32_t *);
int cupidimage_pdf_tt_vm_cvt_write(pdf_tt_vm *, int32_t, int32_t);
int cupidimage_pdf_tt_vm_cvt_delta(pdf_tt_vm *, int32_t, int32_t);
#endif
