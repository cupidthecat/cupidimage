#include "internal.h"
#include <stdlib.h>
#include <string.h>

#define TT_VM_PROGRAM_LIMIT (1024u * 1024u)
#define TT_VM_CALL_LIMIT 64u

int cupidimage_pdf_tt_vm_init(pdf_tt_vm *vm, unsigned stack, unsigned storage, const int32_t *cvt,
                              unsigned cvt_count, unsigned functions, unsigned instructions) {
    memset(vm, 0, sizeof(*vm));
    if (stack > 65535 || storage > 65535 || cvt_count > 65535 || functions > 65535 ||
        instructions > 65535 || (cvt_count && !cvt))
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_MEMORY);
    cupidimage_pdf_tt_vm_graphics_defaults(&vm->graphics);
    vm->stack_limit = stack;
    vm->storage_count = storage;
    vm->cvt_count = cvt_count;
    vm->function_count = functions;
    vm->instruction_limit = instructions > 256 ? 256 : instructions;
    if (stack)
        vm->stack = calloc(stack, sizeof(*vm->stack));
    if (storage)
        vm->storage = calloc(storage, sizeof(*vm->storage));
    if (cvt_count)
        vm->cvt = malloc((size_t)cvt_count * sizeof(*vm->cvt));
    if (functions)
        vm->functions = calloc(functions, sizeof(*vm->functions));
    if (instructions)
        vm->instructions = calloc(256, sizeof(*vm->instructions));
    if ((stack && !vm->stack) || (storage && !vm->storage) || (cvt_count && !vm->cvt) ||
        (functions && !vm->functions) || (instructions && !vm->instructions)) {
        cupidimage_pdf_tt_vm_free(vm);
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_MEMORY);
    }
    if (cvt_count)
        memcpy(vm->cvt, cvt, (size_t)cvt_count * sizeof(*cvt));
    return 1;
}

void cupidimage_pdf_tt_vm_free(pdf_tt_vm *vm) {
    free(vm->stack);
    free(vm->storage);
    free(vm->cvt);
    free(vm->prepared_storage);
    free(vm->prepared_cvt);
    free(vm->functions);
    free(vm->instructions);
    free(vm->zones[0].points);
    free(vm->zones[1].points);
    free(vm->zones[1].design);
    memset(vm, 0, sizeof(*vm));
}

static int vm_step(pdf_tt_vm *vm) {
    if (!vm->work_left)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_BUDGET);
    vm->work_left--;
    return 1;
}

/* Decode PUSH extents even while skipping definitions or conditional branches. */
static int vm_decode(pdf_tt_vm *vm, const unsigned char *data, size_t length, size_t at,
                     size_t *next, unsigned *count, unsigned *bytes, size_t *payload) {
    if (at >= length)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
    unsigned op = data[at++];
    *count = *bytes = 0;
    if (op == 0x40 || op == 0x41) {
        if (at == length)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
        *count = data[at++];
        *bytes = op == 0x40 ? 1 : 2;
    } else if (op >= 0xb0 && op <= 0xbf) {
        *count = (op & 7) + 1;
        *bytes = op < 0xb8 ? 1 : 2;
    }
    *payload = at;
    if ((size_t)*count * *bytes > length - at)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
    *next = at + (size_t)*count * *bytes;
    return 1;
}

static int vm_skip(pdf_tt_vm *vm, const unsigned char *data, size_t length, size_t *at,
                   int definition, int accept_else) {
    unsigned nesting = 0;
    while (*at < length) {
        size_t here = *at, payload;
        unsigned count, bytes;
        vm->error_offset = here;
        vm->error_opcode = data[here];
        if (!vm_step(vm) || !vm_decode(vm, data, length, here, at, &count, &bytes, &payload))
            return 0;
        unsigned op = data[here];
        if (definition) {
            if (op == 0x2c || op == 0x89)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_FUNCTION);
            if (op == 0x2d)
                return 1;
        } else if (op == 0x58) {
            nesting++;
        } else if (op == 0x59) {
            if (!nesting)
                return 1;
            nesting--;
        } else if (op == 0x1b && !nesting && accept_else)
            return 1;
    }
    return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
}

static int vm_execute(pdf_tt_vm *vm, const unsigned char *data, size_t length, int definitions,
                      unsigned depth) {
    if (length > TT_VM_PROGRAM_LIMIT || (length && !data))
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
    if (depth == TT_VM_CALL_LIMIT)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_FUNCTION);
    size_t at = 0;
    while (at < length) {
        size_t here = at, payload;
        unsigned count, bytes, op = data[at];
        vm->error_offset = here;
        vm->error_opcode = op;
        if (!vm_step(vm) || !vm_decode(vm, data, length, at, &at, &count, &bytes, &payload))
            return 0;
        if (bytes) {
            for (unsigned i = 0; i < count; i++) {
                int value = data[payload++];
                if (bytes == 2) {
                    value = value * 256 + data[payload++];
                    if (value >= 32768)
                        value -= 65536;
                }
                if (!vm_step(vm) || !cupidimage_pdf_tt_vm_push(vm, value))
                    return 0;
            }
            continue;
        }
        int handled = cupidimage_pdf_tt_vm_stack_op(vm, op);
        if (handled >= 0) {
            if (!handled)
                return 0;
            continue;
        }
        handled = cupidimage_pdf_tt_vm_numeric_op(vm, op);
        if (handled >= 0) {
            if (!handled)
                return 0;
            continue;
        }
        handled = cupidimage_pdf_tt_vm_graphics_op(vm, op);
        if (handled >= 0) {
            if (!handled)
                return 0;
            continue;
        }
        handled = cupidimage_pdf_tt_vm_size_op(vm, op);
        if (handled >= 0) {
            if (!handled)
                return 0;
            continue;
        }
        handled = cupidimage_pdf_tt_vm_vector_op(vm, op);
        if (handled >= 0) {
            if (!handled)
                return 0;
            continue;
        }
        handled = cupidimage_pdf_tt_vm_point_op(vm, op);
        if (handled >= 0) {
            if (!handled)
                return 0;
            continue;
        }
        handled = cupidimage_pdf_tt_vm_movement_op(vm, op);
        if (handled >= 0) {
            if (!handled)
                return 0;
            continue;
        }
        handled = cupidimage_pdf_tt_vm_relative_op(vm, op);
        if (handled >= 0) {
            if (!handled)
                return 0;
            continue;
        }
        handled = cupidimage_pdf_tt_vm_direct_op(vm, op);
        if (handled >= 0) {
            if (!handled)
                return 0;
            continue;
        }
        handled = cupidimage_pdf_tt_vm_interpolate_op(vm, op);
        if (handled >= 0) {
            if (!handled)
                return 0;
            continue;
        }
        handled = cupidimage_pdf_tt_vm_contour_op(vm, op);
        if (handled >= 0) {
            if (!handled)
                return 0;
            continue;
        }
        handled = cupidimage_pdf_tt_vm_intersection_op(vm, op);
        if (handled >= 0) {
            if (!handled)
                return 0;
            continue;
        }
        handled = cupidimage_pdf_tt_vm_flags_op(vm, op);
        if (handled >= 0) {
            if (!handled)
                return 0;
            continue;
        }
        if (op == 0x58) {
            if (!cupidimage_pdf_tt_vm_need(vm, 1))
                return 0;
            if (!vm->stack[--vm->stack_size] && !vm_skip(vm, data, length, &at, 0, 1))
                return 0;
        } else if (op == 0x1b) {
            if (!vm_skip(vm, data, length, &at, 0, 0))
                return 0;
        } else if (op == 0x59) {
            /* EIF also remains harmless when reached by a relative jump. */
        } else if (op == 0x2c || op == 0x89) {
            if (!definitions || depth || !cupidimage_pdf_tt_vm_need(vm, 1))
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_FUNCTION);
            int32_t id = vm->stack[--vm->stack_size];
            if (id < 0 || (unsigned)id >= (op == 0x2c ? vm->function_count : 256))
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_FUNCTION);
            size_t start = at;
            if (!vm_skip(vm, data, length, &at, 1, 0))
                return 0;
            if (at - start - 1 > 65536)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_FUNCTION);
            pdf_tt_vm_function body = {data + start, at - start - 1, 1};
            if (op == 0x2c)
                vm->functions[id] = body;
            else {
                if (!vm->instructions || (!vm->instructions[id].defined &&
                                          vm->instruction_count == vm->instruction_limit))
                    return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_FUNCTION);
                if (!vm->instructions[id].defined)
                    vm->instruction_count++;
                vm->instructions[id] = body;
            }
        } else if (op == 0x2b || op == 0x2a) {
            if (!cupidimage_pdf_tt_vm_need(vm, op == 0x2b ? 1 : 2))
                return 0;
            int32_t id = vm->stack[--vm->stack_size];
            int32_t repeats = op == 0x2a ? vm->stack[--vm->stack_size] : 1;
            if (id < 0 || (unsigned)id >= vm->function_count || repeats < 0 ||
                !vm->functions[id].defined)
                return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_FUNCTION);
            for (int32_t i = 0; i < repeats; i++) {
                if (!vm_step(vm) ||
                    !vm_execute(vm, vm->functions[id].data, vm->functions[id].length, 0, depth + 1))
                    return 0;
            }
        } else if (op == 0x1c || op == 0x78 || op == 0x79) {
            if (!cupidimage_pdf_tt_vm_need(vm, op == 0x1c ? 1 : 2))
                return 0;
            int jump = 1;
            if (op != 0x1c) {
                int32_t condition = vm->stack[--vm->stack_size];
                jump = op == 0x78 ? condition != 0 : condition == 0;
            }
            int32_t offset = vm->stack[--vm->stack_size];
            if (jump) {
                int64_t destination = (int64_t)here + offset;
                if (destination < 0 || (uint64_t)destination > length)
                    return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
                at = (size_t)destination;
            }
        } else if (op == 0x2d)
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_FUNCTION);
        else if ((op == 0x28 || op == 0x7b || op == 0x83 || op == 0x84 ||
                  (op >= 0x8f && op <= 0xaf)) &&
                 vm->instructions && vm->instructions[op].defined) {
            /* Definitions patch unassigned opcodes, never existing instructions. */
            if (!vm_execute(vm, vm->instructions[op].data, vm->instructions[op].length, 0,
                            depth + 1))
                return 0;
        } else
            return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_UNSUPPORTED);
    }
    return 1;
}

int cupidimage_pdf_tt_vm_run(pdf_tt_vm *vm, const unsigned char *data, size_t length,
                             pdf_tt_vm_stage stage, size_t work) {
    if (vm->error != PDF_TT_VM_OK)
        return 0;
    if (stage != PDF_TT_VM_GLYPH && stage != PDF_TT_VM_FONT && stage != PDF_TT_VM_PREP)
        return cupidimage_pdf_tt_vm_fail(vm, PDF_TT_VM_PROGRAM);
    vm->stage = stage;
    if (stage != PDF_TT_VM_GLYPH)
        vm->prepared = 0;
    vm->stack_size = 0;
    vm->work_left = work;
    vm->error_offset = vm->error_opcode = 0;
    if (stage == PDF_TT_VM_GLYPH && (vm->graphics.instruction_control & 1))
        return 1;
    int ok = vm_execute(vm, data, length, stage != PDF_TT_VM_GLYPH, 0);
    if (ok)
        vm->error_offset = vm->error_opcode = 0;
    return ok;
}
