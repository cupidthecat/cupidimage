#include "pdf_calculator.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Calculator functions have no names, loops, or procedure objects. Compile
   their conditional syntax into forward jumps before evaluating any pixels. */
#define PDF_CALC_CODE 4096
#define PDF_CALC_STACK 256
#define PDF_CALC_DEPTH 16

enum { REAL, INTEGER, BOOLEAN };

enum {
    PUSH,
    JFALSE,
    JUMP,
    ABS,
    ADD,
    ATAN,
    CEILING,
    COS,
    CVI,
    CVR,
    DIV,
    EXP,
    FLOOR,
    IDIV,
    LN,
    LOG,
    MOD,
    MUL,
    NEG,
    ROUND,
    SIN,
    SQRT,
    SUB,
    TRUNCATE,
    AND,
    BITSHIFT,
    EQ,
    FALSE_OP,
    GE,
    GT,
    LE,
    LT,
    NE,
    NOT,
    OR,
    TRUE_OP,
    XOR,
    COPY,
    DUP,
    EXCH,
    INDEX,
    POP,
    ROLL
};

typedef struct {
    double number;
    int type;
} calc_value;

typedef struct {
    int op, target;
    calc_value value;
} calc_instruction;

struct pdf_calculator_program {
    int count;
    calc_instruction code[PDF_CALC_CODE];
};

size_t cupidimage_pdf_calculator_bytes(const pdf_calculator_program *program) {
    return program ? sizeof(*program) : 0;
}

typedef struct {
    const unsigned char *data;
    size_t length, pos;
    pdf_calculator_program *program;
} calc_parser;

static const char *const operators[] = {
    "abs",      "add", "atan",     "ceiling", "cos",   "cvi", "cvr",   "div",   "exp",  "floor",
    "idiv",     "ln",  "log",      "mod",     "mul",   "neg", "round", "sin",   "sqrt", "sub",
    "truncate", "and", "bitshift", "eq",      "false", "ge",  "gt",    "le",    "lt",   "ne",
    "not",      "or",  "true",     "xor",     "copy",  "dup", "exch",  "index", "pop",  "roll"};

static int whitespace(unsigned char c) {
    return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32;
}

static void skip(calc_parser *p) {
    for (;;) {
        while (p->pos < p->length && whitespace(p->data[p->pos]))
            p->pos++;
        if (p->pos == p->length || p->data[p->pos] != '%')
            return;
        while (p->pos < p->length && p->data[p->pos] != 10 && p->data[p->pos] != 13)
            p->pos++;
    }
}

static size_t token(calc_parser *p, const unsigned char **start) {
    skip(p);
    size_t begin = p->pos;
    while (p->pos < p->length && !whitespace(p->data[p->pos]) &&
           !strchr("{}%[]()/<> ", p->data[p->pos]))
        p->pos++;
    *start = p->data + begin;
    return p->pos - begin;
}

static int word(calc_parser *p, const char *name) {
    const unsigned char *start;
    size_t n = token(p, &start);
    return n == strlen(name) && !memcmp(start, name, n);
}

static int emit(calc_parser *p, int op) {
    if (p->program->count == PDF_CALC_CODE)
        return -1;
    int i = p->program->count++;
    p->program->code[i].op = op;
    return i;
}

static int block(calc_parser *p, unsigned depth) {
    if (depth == PDF_CALC_DEPTH || p->pos == p->length || p->data[p->pos++] != '{')
        return 0;
    for (;;) {
        skip(p);
        if (p->pos == p->length)
            return 0;
        if (p->data[p->pos] == '}') {
            p->pos++;
            return 1;
        }
        if (p->data[p->pos] == '{') {
            int test = emit(p, JFALSE);
            if (test < 0 || !block(p, depth + 1))
                return 0;
            skip(p);
            if (p->pos < p->length && p->data[p->pos] == '{') {
                int jump = emit(p, JUMP);
                if (jump < 0)
                    return 0;
                p->program->code[test].target = p->program->count;
                if (!block(p, depth + 1) || !word(p, "ifelse"))
                    return 0;
                p->program->code[jump].target = p->program->count;
            } else {
                if (!word(p, "if"))
                    return 0;
                p->program->code[test].target = p->program->count;
            }
            continue;
        }
        const unsigned char *start;
        size_t n = token(p, &start);
        if (!n)
            return 0;
        int op = -1;
        for (size_t i = 0; i < sizeof(operators) / sizeof(*operators); i++)
            if (n == strlen(operators[i]) && !memcmp(start, operators[i], n)) {
                op = ABS + (int)i;
                break;
            }
        if (op >= 0) {
            if (emit(p, op) < 0)
                return 0;
            continue;
        }
        /* PDF numeric tokens allow a sign and one decimal point, not exponent
           notation or the radix syntax of the general PostScript language. */
        size_t k = 0;
        int sign = 1, digits = 0, point = 0;
        double value = 0, fraction = .1;
        if (start[k] == '+' || start[k] == '-') {
            if (start[k] == '-')
                sign = -1;
            k++;
        }
        for (; k < n; k++) {
            if (start[k] == '.' && !point) {
                point = 1;
                continue;
            }
            if (start[k] < '0' || start[k] > '9')
                return 0;
            digits++;
            if (point) {
                value += (start[k] - '0') * fraction;
                fraction *= .1;
            } else
                value = value * 10 + (start[k] - '0');
        }
        value *= sign;
        if (!digits || !isfinite(value))
            return 0;
        int i = emit(p, PUSH);
        if (i < 0)
            return 0;
        p->program->code[i].value = (calc_value){
            value, !point && value >= INT32_MIN && value <= INT32_MAX ? INTEGER : REAL};
    }
}

pdf_calculator_program *cupidimage_pdf_calculator_compile(const unsigned char *data,
                                                          size_t length) {
    pdf_calculator_program *program = calloc(1, sizeof(*program));
    if (!program)
        return NULL;
    calc_parser parser = {data, length, 0, program};
    skip(&parser);
    if (!block(&parser, 0)) {
        free(program);
        return NULL;
    }
    skip(&parser);
    if (parser.pos != length) {
        free(program);
        return NULL;
    }
    return program;
}

static int numeric(calc_value v) {
    return v.type != BOOLEAN;
}

static calc_value integer_bits(uint32_t bits) {
    double value = bits <= INT32_MAX ? (double)bits : (double)bits - 4294967296.0;
    return (calc_value){value, INTEGER};
}

int cupidimage_pdf_calculator_evaluate(const pdf_calculator_program *program, const double *input,
                                       int inputs, double *output, int outputs, size_t *budget) {
    calc_value stack[PDF_CALC_STACK];
    int count = 0;
    if (inputs < 0 || inputs > PDF_CALC_STACK || outputs < 0 || outputs > PDF_CALC_STACK)
        return 0;
    for (int i = 0; i < inputs; i++) {
        if (!isfinite(input[i]))
            return 0;
        stack[count++] = (calc_value){input[i], REAL};
    }
    for (int pc = 0; pc < program->count; pc++) {
        if (!*budget)
            return 0;
        (*budget)--;
        const calc_instruction *code = &program->code[pc];
        int op = code->op;
        if (op == JUMP) {
            pc = code->target - 1;
            continue;
        }
        if (op == PUSH || op == TRUE_OP || op == FALSE_OP) {
            if (count == PDF_CALC_STACK)
                return 0;
            stack[count++] = op == PUSH ? code->value : (calc_value){op == TRUE_OP, BOOLEAN};
            continue;
        }
        if (!count)
            return 0;
        calc_value a = stack[count - 1], b = {0, REAL};
        if (op == JFALSE) {
            if (a.type != BOOLEAN)
                return 0;
            count--;
            if (!a.number)
                pc = code->target - 1;
            continue;
        }
        if (op == POP) {
            count--;
            continue;
        }
        if (op == DUP) {
            if (count == PDF_CALC_STACK)
                return 0;
            stack[count++] = a;
            continue;
        }
        if (op == EXCH) {
            if (count < 2)
                return 0;
            stack[count - 1] = stack[count - 2];
            stack[count - 2] = a;
            continue;
        }
        if (op == COPY || op == INDEX || op == ROLL) {
            if (a.type != INTEGER)
                return 0;
            int32_t n = (int32_t)a.number;
            count--;
            if (op == COPY) {
                if (n < 0 || n > count || n > PDF_CALC_STACK - count)
                    return 0;
                if ((size_t)n > *budget)
                    return 0;
                *budget -= (size_t)n;
                memcpy(stack + count, stack + count - n, (size_t)n * sizeof(*stack));
                count += n;
            } else if (op == INDEX) {
                if (n < 0 || n >= count)
                    return 0;
                stack[count] = stack[count - 1 - n];
                count++;
            } else {
                if (!count || stack[count - 1].type != INTEGER)
                    return 0;
                int32_t items = (int32_t)stack[--count].number;
                if (items < 0 || items > count)
                    return 0;
                if (!items)
                    continue;
                if ((size_t)items > *budget)
                    return 0;
                *budget -= (size_t)items;
                int rotation = n % items;
                if (rotation < 0)
                    rotation += items;
                calc_value temp[PDF_CALC_STACK];
                for (int i = 0; i < items; i++)
                    temp[(i + rotation) % items] = stack[count - items + i];
                memcpy(stack + count - items, temp, (size_t)items * sizeof(*stack));
            }
            continue;
        }
        if (op == NOT) {
            if (a.type == BOOLEAN)
                stack[count - 1].number = !a.number;
            else if (a.type == INTEGER)
                stack[count - 1] = integer_bits(~(uint32_t)(int32_t)a.number);
            else
                return 0;
            continue;
        }
        int binary = op == ADD || op == SUB || op == MUL || op == DIV || op == IDIV || op == MOD ||
                     op == EXP || op == ATAN || op == AND || op == OR || op == XOR ||
                     op == BITSHIFT || op == EQ || op == NE || op == GE || op == GT || op == LE ||
                     op == LT;
        if (binary) {
            if (count < 2)
                return 0;
            b = a;
            a = stack[count - 2];
            count--;
        }
        calc_value result = {0, REAL};
        double x = a.number, y = b.number;
        if (op == EQ || op == NE) {
            int equal = (a.type == BOOLEAN) == (b.type == BOOLEAN) && x == y;
            result = (calc_value){op == EQ ? equal : !equal, BOOLEAN};
        } else if (op == AND || op == OR || op == XOR) {
            if (a.type != b.type || a.type == REAL)
                return 0;
            uint32_t u = (uint32_t)(int32_t)x, v = (uint32_t)(int32_t)y;
            result = integer_bits(op == AND ? u & v : op == OR ? u | v : u ^ v);
            result.type = a.type;
        } else if (op == BITSHIFT) {
            if (a.type != INTEGER || b.type != INTEGER)
                return 0;
            uint32_t u = (uint32_t)(int32_t)x;
            int32_t shift = (int32_t)y;
            result = integer_bits(shift >= 32 || shift <= -32 ? 0
                                  : shift >= 0                ? u << shift
                                                              : u >> (-shift));
        } else {
            if (!numeric(a) || (binary && !numeric(b)))
                return 0;
            if (op == GE || op == GT || op == LE || op == LT)
                result = (calc_value){op == GE   ? x >= y
                                      : op == GT ? x > y
                                      : op == LE ? x <= y
                                                 : x < y,
                                      BOOLEAN};
            else if (op == IDIV || op == MOD) {
                if (a.type != INTEGER || b.type != INTEGER || y == 0)
                    return 0;
                if (op == IDIV && x == INT32_MIN && y == -1)
                    return 0;
                result = (calc_value){op == IDIV ? trunc(x / y) : fmod(x, y), INTEGER};
            } else {
                switch (op) {
                    case ABS:
                        result.number = fabs(x);
                        break;
                    case NEG:
                        result.number = -x;
                        break;
                    case ADD:
                        result.number = x + y;
                        break;
                    case SUB:
                        result.number = x - y;
                        break;
                    case MUL:
                        result.number = x * y;
                        break;
                    case DIV:
                        if (y == 0)
                            return 0;
                        result.number = x / y;
                        break;
                    case EXP:
                        result.number = pow(x, y);
                        break;
                    case ATAN:
                        if (x == 0 && y == 0)
                            return 0;
                        result.number = atan2(x, y) * 180 / 3.14159265358979323846;
                        if (result.number < 0)
                            result.number += 360;
                        break;
                    case CEILING:
                        result.number = ceil(x);
                        break;
                    case FLOOR:
                        result.number = floor(x);
                        break;
                    case ROUND:
                        result.number = floor(x);
                        if (x - result.number >= .5)
                            result.number += 1;
                        break;
                    case TRUNCATE:
                        result.number = trunc(x);
                        break;
                    case CVI:
                        result.number = trunc(x);
                        if (result.number < INT32_MIN || result.number > INT32_MAX)
                            return 0;
                        result.type = INTEGER;
                        break;
                    case CVR:
                        result.number = x;
                        break;
                    case COS:
                        result.number = cos(x * 3.14159265358979323846 / 180);
                        break;
                    case SIN:
                        result.number = sin(x * 3.14159265358979323846 / 180);
                        break;
                    case LN:
                        if (x <= 0)
                            return 0;
                        result.number = log(x);
                        break;
                    case LOG:
                        if (x <= 0)
                            return 0;
                        result.number = log10(x);
                        break;
                    case SQRT:
                        if (x < 0)
                            return 0;
                        result.number = sqrt(x);
                        break;
                    default:
                        return 0;
                }
                if (a.type == INTEGER && (!binary || b.type == INTEGER) &&
                    (op == ABS || op == NEG || op == ADD || op == SUB || op == MUL ||
                     op == CEILING || op == FLOOR || op == ROUND || op == TRUNCATE) &&
                    result.number >= INT32_MIN && result.number <= INT32_MAX)
                    result.type = INTEGER;
            }
        }
        if (!isfinite(result.number))
            return 0;
        stack[count - 1] = result;
    }
    if (count != outputs)
        return 0;
    for (int i = 0; i < outputs; i++) {
        if (!numeric(stack[i]))
            return 0;
        output[i] = stack[i].number;
    }
    return 1;
}
