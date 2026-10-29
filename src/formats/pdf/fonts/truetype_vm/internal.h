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
#endif
