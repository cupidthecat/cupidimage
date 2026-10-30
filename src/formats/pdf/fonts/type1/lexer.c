#include "lexer.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cupidimage_pdf_type1_space(unsigned char c) {
    return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32;
}

static int t1_delimiter(unsigned char c) {
    return cupidimage_pdf_type1_space(c) || c == '(' || c == ')' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == '/' || c == '<' || c == '>' || c == '%';
}

/* PLRM 3, section 3.2.2. Radix integers use a bounded 32-bit representation. */
static unsigned t1_number(const char *text, double *value) {
    const char *hash = strchr(text, '#');
    if (hash) {
        unsigned base = 0;
        if (hash == text || !hash[1])
            return PDF_T1_WORD;
        for (const char *p = text; p < hash; p++) {
            if (*p < '0' || *p > '9' || base > 36)
                return PDF_T1_WORD;
            base = base * 10 + (unsigned)(*p - '0');
        }
        if (base < 2 || base > 36)
            return PDF_T1_WORD;
        uint32_t result = 0;
        int overflow = 0;
        for (const char *p = hash + 1; *p; p++) {
            unsigned digit = *p >= '0' && *p <= '9'   ? (unsigned)(*p - '0')
                             : *p >= 'A' && *p <= 'Z' ? (unsigned)(*p - 'A' + 10)
                             : *p >= 'a' && *p <= 'z' ? (unsigned)(*p - 'a' + 10)
                                                      : 36;
            if (digit >= base)
                return PDF_T1_WORD;
            if (result > (UINT32_MAX - digit) / base)
                overflow = 1;
            if (!overflow)
                result = result * base + digit;
        }
        if (overflow)
            return PDF_T1_ERROR;
        *value = (double)result - (result > INT32_MAX ? 4294967296.0 : 0.0);
        return PDF_T1_NUMBER;
    }
    /* Remove the decimal point before strtod, so LC_NUMERIC cannot change syntax. */
    char normalized[160];
    unsigned used = 0, digits = 0, fraction = 0;
    const char *p = text;
    if (*p == '+' || *p == '-')
        normalized[used++] = *p++;
    while (*p >= '0' && *p <= '9') {
        normalized[used++] = *p++;
        digits++;
    }
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            normalized[used++] = *p++;
            digits++;
            fraction++;
        }
    }
    if (!digits)
        return PDF_T1_WORD;
    int exponent = 0, negative = 0;
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-')
            negative = *p++ == '-';
        if (*p < '0' || *p > '9')
            return PDF_T1_WORD;
        while (*p >= '0' && *p <= '9') {
            if (exponent < 100000)
                exponent = exponent * 10 + (*p - '0');
            p++;
        }
    }
    if (*p)
        return PDF_T1_WORD;
    if (negative)
        exponent = -exponent;
    exponent -= (int)fraction;
    snprintf(normalized + used, sizeof(normalized) - used, "e%d", exponent);
    *value = strtod(normalized, NULL);
    return isfinite(*value) ? PDF_T1_NUMBER : PDF_T1_ERROR;
}

static int t1_hex(unsigned c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* Validate an ASCII85 string without allocating its decoded contents. */
static int t1_ascii85(pdf_cff_slice source, size_t *position) {
    unsigned count = 0;
    uint64_t group = 0;
    while (*position < source.length) {
        unsigned c = source.data[(*position)++];
        if (cupidimage_pdf_type1_space((unsigned char)c))
            continue;
        if (c == '~') {
            if (*position == source.length || source.data[(*position)++] != '>' || count == 1)
                return 0;
            if (count) {
                while (count++ < 5)
                    group = group * 85 + 84;
                if (group > UINT32_MAX)
                    return 0;
            }
            return 1;
        }
        if (c == 'z') {
            if (count)
                return 0;
            continue;
        }
        if (c < '!' || c > 'u')
            return 0;
        group = group * 85 + c - '!';
        if (++count == 5) {
            if (group > UINT32_MAX)
                return 0;
            count = 0;
            group = 0;
        }
    }
    return 0;
}

pdf_type1_token cupidimage_pdf_type1_lex(pdf_cff_slice source, size_t *position, unsigned depth) {
    pdf_type1_token token = {0};
    if (!position || *position > source.length || (!source.data && source.length) || depth >= 64) {
        token.type = PDF_T1_ERROR;
        return token;
    }
    size_t pos = *position;
    while (pos < source.length) {
        if (cupidimage_pdf_type1_space(source.data[pos])) {
            pos++;
            continue;
        }
        if (source.data[pos] != '%')
            break;
        while (pos < source.length && source.data[pos] != '\r' && source.data[pos] != '\n' &&
               source.data[pos] != '\f')
            pos++;
    }
    if (pos == source.length) {
        *position = pos;
        return token;
    }
    unsigned c = source.data[pos++];
    if (c == '(') {
        unsigned nested = 1;
        while (pos < source.length && nested) {
            c = source.data[pos++];
            if (c == '\\' && pos < source.length)
                pos++;
            else if (c == '(') {
                if (++nested == 64)
                    break;
            } else if (c == ')')
                nested--;
        }
        token.type = nested ? PDF_T1_ERROR : PDF_T1_OTHER;
    } else if (c == ')') {
        token.type = PDF_T1_ERROR;
    } else if (c == '{') {
        size_t start = pos;
        while (pos < source.length) {
            pdf_type1_token item = cupidimage_pdf_type1_lex(source, &pos, depth + 1);
            if (item.type == PDF_T1_OTHER && item.text[0] == '}') {
                token.type = PDF_T1_PROC;
                token.body = (pdf_cff_slice){source.data + start, pos - start - 1};
                break;
            }
            if (item.type == PDF_T1_ERROR || item.type == PDF_T1_EOF)
                break;
        }
        if (token.type != PDF_T1_PROC)
            token.type = PDF_T1_ERROR;
    } else if (c == '<') {
        token.type = PDF_T1_OTHER;
        if (pos < source.length && source.data[pos] == '<') {
            pos++;
            strcpy(token.text, "<<");
        } else if (pos < source.length && source.data[pos] == '~') {
            pos++;
            if (!t1_ascii85(source, &pos))
                token.type = PDF_T1_ERROR;
        } else {
            while (pos < source.length && source.data[pos] != '>') {
                unsigned byte = source.data[pos++];
                if (!cupidimage_pdf_type1_space((unsigned char)byte) && !t1_hex(byte)) {
                    token.type = PDF_T1_ERROR;
                    break;
                }
            }
            if (pos == source.length)
                token.type = PDF_T1_ERROR;
            else if (token.type != PDF_T1_ERROR)
                pos++;
        }
    } else if (c == '>') {
        token.type = PDF_T1_ERROR;
        if (pos < source.length && source.data[pos] == '>') {
            pos++;
            token.type = PDF_T1_OTHER;
            strcpy(token.text, ">>");
        }
    } else if (c != '/' && t1_delimiter((unsigned char)c)) {
        token.type = PDF_T1_OTHER;
        token.text[0] = (char)c;
    } else {
        unsigned used = 0;
        token.type = c == '/' ? PDF_T1_NAME : PDF_T1_WORD;
        if (c != '/')
            pos--;
        else if (pos < source.length && source.data[pos] == '/') {
            pos++;
            token.type = PDF_T1_IMMEDIATE;
        }
        while (pos < source.length && !t1_delimiter(source.data[pos])) {
            c = source.data[pos++];
            if (used == 127 || !c) {
                token.type = PDF_T1_ERROR;
                break;
            }
            token.text[used++] = (char)c;
        }
        if (token.type == PDF_T1_WORD)
            token.type = t1_number(token.text, &token.number);
    }
    *position = pos;
    return token;
}
