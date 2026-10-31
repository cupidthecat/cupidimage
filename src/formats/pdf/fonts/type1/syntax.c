#include "syntax.h"
#include <limits.h>
#include <string.h>

static int t1_syntax_word(pdf_type1_token token, const char *word) {
    return token.type == PDF_T1_WORD && !strcmp(token.text, word);
}

int cupidimage_pdf_type1_syntax_take(pdf_type1_syntax *syntax, size_t amount) {
    if (syntax->failed || amount > syntax->work) {
        syntax->failed = 1;
        return 0;
    }
    syntax->work -= amount;
    return 1;
}

void cupidimage_pdf_type1_syntax_init(pdf_type1_syntax *syntax) {
    memset(syntax, 0, sizeof(*syntax));
    syntax->work = 1048576;
    /* Compatibility spellings remain available unless explicitly shadowed. */
    strcpy(syntax->bindings[0].name, "RD");
    strcpy(syntax->bindings[1].name, "-|");
    syntax->bindings[0].kind = syntax->bindings[1].kind = PDF_T1_READER;
    syntax->count = 2;
}

unsigned cupidimage_pdf_type1_syntax_dictionary(const pdf_type1_syntax *syntax) {
    return syntax->dictionaries[syntax->depth];
}

int cupidimage_pdf_type1_syntax_begin_dictionary(pdf_type1_syntax *syntax, unsigned target) {
    if (syntax->depth == 64 || target > PDF_T1_SYSTEM_DICTIONARY ||
        !cupidimage_pdf_type1_syntax_take(syntax, 1))
        return 0;
    unsigned dictionary = target == PDF_T1_SYSTEM_DICTIONARY ? UINT_MAX
                          : target == PDF_T1_USER_DICTIONARY ? 0
                          : target == PDF_T1_CURRENT_DICTIONARY
                              ? cupidimage_pdf_type1_syntax_dictionary(syntax)
                              : ++syntax->next_dictionary;
    syntax->dictionaries[++syntax->depth] = dictionary;
    return 1;
}

int cupidimage_pdf_type1_syntax_begin(pdf_type1_syntax *syntax) {
    return cupidimage_pdf_type1_syntax_begin_dictionary(syntax, 0);
}

int cupidimage_pdf_type1_syntax_end(pdf_type1_syntax *syntax) {
    if (!syntax->depth || !cupidimage_pdf_type1_syntax_take(syntax, 1))
        return 0;
    unsigned dictionary = syntax->dictionaries[syntax->depth--];
    for (unsigned i = 0; i <= syntax->depth; i++)
        if (syntax->dictionaries[i] == dictionary)
            return 1;
    unsigned count = 0;
    for (unsigned i = 0; i < syntax->count; i++)
        if (syntax->bindings[i].dictionary != dictionary)
            syntax->bindings[count++] = syntax->bindings[i];
    syntax->count = count;
    return 1;
}

static unsigned t1_syntax_find(pdf_type1_syntax *syntax, const char *name) {
    for (unsigned depth = syntax->depth + 1; depth;) {
        unsigned dictionary = syntax->dictionaries[--depth];
        for (unsigned i = syntax->count; i;) {
            i--;
            if (!cupidimage_pdf_type1_syntax_take(syntax, 1))
                return 0;
            if (syntax->bindings[i].dictionary == dictionary &&
                !strcmp(name, syntax->bindings[i].name))
                return i + 1;
        }
    }
    return 0;
}

unsigned cupidimage_pdf_type1_syntax_lookup(pdf_type1_syntax *syntax, const char *name) {
    unsigned found = t1_syntax_find(syntax, name);
    return found ? syntax->bindings[found - 1].kind : 0;
}

static unsigned t1_syntax_procedure(pdf_type1_token value) {
    if (value.type != PDF_T1_PROC)
        return 0;
    static const char *const reader[] = {"string", "currentfile", "exch", "readstring", "pop"};
    size_t position = 0;
    unsigned i;
    for (i = 0; i < 5; i++)
        if (!t1_syntax_word(cupidimage_pdf_type1_lex(value.body, &position, 0), reader[i]))
            break;
    if (i == 5 && cupidimage_pdf_type1_lex(value.body, &position, 0).type == PDF_T1_EOF)
        return PDF_T1_READER;
    position = 0;
    pdf_type1_token word = cupidimage_pdf_type1_lex(value.body, &position, 0);
    if (t1_syntax_word(word, "noaccess") || t1_syntax_word(word, "readonly"))
        word = cupidimage_pdf_type1_lex(value.body, &position, 0);
    unsigned kind = t1_syntax_word(word, "def")   ? PDF_T1_DEFINER
                    : t1_syntax_word(word, "put") ? PDF_T1_PUTTER
                                                  : 0;
    return kind && cupidimage_pdf_type1_lex(value.body, &position, 0).type == PDF_T1_EOF ? kind : 0;
}

static int t1_syntax_end_definition(pdf_type1_syntax *syntax, pdf_cff_slice source,
                                    size_t *position, int put) {
    for (unsigned i = 0; i < 5; i++) {
        if (!cupidimage_pdf_type1_syntax_take(syntax, 1))
            return 0;
        pdf_type1_token token = cupidimage_pdf_type1_lex(source, position, 0);
        if (t1_syntax_word(token, put ? "put" : "def"))
            return 1;
        if (token.type == PDF_T1_WORD && (cupidimage_pdf_type1_syntax_lookup(syntax, token.text) &
                                          (put ? PDF_T1_PUTTER : PDF_T1_DEFINER)))
            return 1;
        if (!t1_syntax_word(token, "executeonly") && !t1_syntax_word(token, "readonly") &&
            !t1_syntax_word(token, "noaccess") && !t1_syntax_word(token, "bind"))
            return 0;
    }
    return 0;
}

int cupidimage_pdf_type1_syntax_end_definition(pdf_type1_syntax *syntax, pdf_cff_slice source,
                                               size_t *position) {
    return t1_syntax_end_definition(syntax, source, position, 0);
}

static int t1_syntax_literal(pdf_type1_syntax *syntax, pdf_cff_slice source, size_t *position,
                             pdf_type1_token value) {
    if (value.type != PDF_T1_OTHER || (strcmp(value.text, "[") && strcmp(value.text, "<<")))
        return value.type != PDF_T1_ERROR && value.type != PDF_T1_EOF;
    unsigned close[64], depth = 1;
    close[0] = !strcmp(value.text, "[") ? 1 : 2;
    while (depth) {
        if (!cupidimage_pdf_type1_syntax_take(syntax, 1))
            return 0;
        value = cupidimage_pdf_type1_lex(source, position, 0);
        if (value.type == PDF_T1_ERROR || value.type == PDF_T1_EOF)
            return 0;
        if (value.type != PDF_T1_OTHER)
            continue;
        unsigned opening = !strcmp(value.text, "[") ? 1 : !strcmp(value.text, "<<") ? 2 : 0;
        unsigned closing = !strcmp(value.text, "]") ? 1 : !strcmp(value.text, ">>") ? 2 : 0;
        if (opening) {
            if (depth == 64)
                return 0;
            close[depth++] = opening;
        } else if (closing) {
            if (closing != close[--depth])
                return 0;
        }
    }
    return 1;
}

static int t1_syntax_definition(pdf_type1_syntax *syntax, const char *name, pdf_cff_slice source,
                                size_t *position, unsigned dictionary, int put) {
    size_t pos = *position;
    if (!name || strlen(name) >= sizeof(syntax->bindings[0].name) ||
        !cupidimage_pdf_type1_syntax_take(syntax, 1))
        return 0;
    pdf_type1_token value = cupidimage_pdf_type1_lex(source, &pos, 0);
    unsigned kind = t1_syntax_procedure(value);
    unsigned previous = t1_syntax_find(syntax, name);
    unsigned index = syntax->count;
    for (unsigned i = 0; i < syntax->count; i++)
        if (syntax->bindings[i].dictionary == dictionary &&
            !strcmp(syntax->bindings[i].name, name)) {
            index = i;
            break;
        }
    if (!t1_syntax_literal(syntax, source, &pos, value) ||
        !t1_syntax_end_definition(syntax, source, &pos, put))
        return !syntax->failed && !previous && index == syntax->count;
    if (dictionary == UINT_MAX)
        return 0;
    if (kind || previous || index != syntax->count) {
        if (index == syntax->count) {
            if (syntax->count == 128)
                return 0;
            syntax->count++;
        }
        strcpy(syntax->bindings[index].name, name);
        syntax->bindings[index].dictionary = dictionary;
        syntax->bindings[index].kind = kind;
    }
    *position = pos;
    return !syntax->failed;
}

int cupidimage_pdf_type1_syntax_definition(pdf_type1_syntax *syntax, const char *name,
                                           pdf_cff_slice source, size_t *position) {
    return t1_syntax_definition(syntax, name, source, position,
                                cupidimage_pdf_type1_syntax_dictionary(syntax), 0);
}

int cupidimage_pdf_type1_syntax_put_definition(pdf_type1_syntax *syntax, const char *name,
                                               pdf_cff_slice source, size_t *position, int target) {
    if (target < PDF_T1_USER_DICTIONARY || target > PDF_T1_SYSTEM_DICTIONARY)
        return 0;
    unsigned dictionary = target == PDF_T1_SYSTEM_DICTIONARY ? UINT_MAX
                          : target == PDF_T1_USER_DICTIONARY
                              ? 0
                              : cupidimage_pdf_type1_syntax_dictionary(syntax);
    return t1_syntax_definition(syntax, name, source, position, dictionary, 1);
}
