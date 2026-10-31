#include "formats/pdf/fonts/type1/syntax.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;

static int define(pdf_type1_syntax *syntax, const char *name, const char *source) {
    size_t position = 0;
    int result = cupidimage_pdf_type1_syntax_definition(
        syntax, name, (pdf_cff_slice){(const unsigned char *)source, strlen(source)}, &position);
    checks++;
    return result;
}

static void check_names(pdf_type1_syntax *syntax, unsigned expected, const char *name) {
    assert(cupidimage_pdf_type1_syntax_lookup(syntax, name ? name : "D") ==
           (expected ? PDF_T1_DEFINER : 0));
    checks++;
}

int main(void) {
    const char *reader = "{string currentfile exch readstring pop} executeonly def";
    pdf_type1_syntax syntax;
    cupidimage_pdf_type1_syntax_init(&syntax);
    assert(cupidimage_pdf_type1_syntax_lookup(&syntax, "RD") == PDF_T1_READER);
    assert(cupidimage_pdf_type1_syntax_lookup(&syntax, "-|") == PDF_T1_READER);
    assert(define(&syntax, "D", "{def} bind def"));
    assert(define(&syntax, "Read", reader));
    check_names(&syntax, 1, "D");
    assert(cupidimage_pdf_type1_syntax_begin(&syntax));
    assert(define(&syntax, "D", "42 def"));
    assert(define(&syntax, "Read", "[1 [2] << /Hidden {def} >>] def"));
    assert(!cupidimage_pdf_type1_syntax_lookup(&syntax, "D"));
    assert(!cupidimage_pdf_type1_syntax_lookup(&syntax, "Read"));
    assert(!cupidimage_pdf_type1_syntax_lookup(&syntax, "Hidden"));
    check_names(&syntax, 0, NULL);
    assert(define(&syntax, "D", "{readonly def} executeonly def"));
    assert(define(&syntax, "Read", "{string currentfile exch readstring pop} D"));
    check_names(&syntax, 1, "D");
    assert(cupidimage_pdf_type1_syntax_lookup(&syntax, "Read") == PDF_T1_READER);
    assert(cupidimage_pdf_type1_syntax_begin(&syntax));
    assert(define(&syntax, "D", "{noaccess def} def"));
    check_names(&syntax, 1, "D");
    assert(define(&syntax, "Inner", reader));
    assert(cupidimage_pdf_type1_syntax_end(&syntax));
    assert(!cupidimage_pdf_type1_syntax_lookup(&syntax, "Inner"));
    check_names(&syntax, 1, "D");
    assert(cupidimage_pdf_type1_syntax_end(&syntax));
    check_names(&syntax, 1, "D");
    assert(cupidimage_pdf_type1_syntax_lookup(&syntax, "Read") == PDF_T1_READER);
    assert(define(&syntax, "Unused", "{string currentfile exch readstring pop} pop"));
    assert(!cupidimage_pdf_type1_syntax_lookup(&syntax, "Unused"));
    assert(!define(&syntax, "Read", "{string currentfile exch readstring pop} pop"));
    assert(!define(&syntax, "Read", "[ 1 2 >> def"));
    assert(define(&syntax, "Read", "{pop} def"));
    assert(!cupidimage_pdf_type1_syntax_lookup(&syntax, "Read"));
    assert(define(&syntax, "Read", reader));
    assert(cupidimage_pdf_type1_syntax_lookup(&syntax, "Read") == PDF_T1_READER);
    /* Explicit userdict writes persist; newer global definitions do not hide
     * bindings in an active inner dictionary. Re-entering currentdict retains it. */
    cupidimage_pdf_type1_syntax_init(&syntax);
    assert(cupidimage_pdf_type1_syntax_begin(&syntax));
    assert(define(&syntax, "RD", reader));
    size_t position = 0;
    const char *put = "{pop} put";
    assert(cupidimage_pdf_type1_syntax_put_definition(
        &syntax, "RD", (pdf_cff_slice){(const unsigned char *)put, strlen(put)}, &position, 1));
    assert(cupidimage_pdf_type1_syntax_lookup(&syntax, "RD") == PDF_T1_READER);
    assert(cupidimage_pdf_type1_syntax_begin_dictionary(&syntax, 2));
    assert(define(&syntax, "Shared", "{def} def"));
    assert(cupidimage_pdf_type1_syntax_end(&syntax));
    assert(cupidimage_pdf_type1_syntax_lookup(&syntax, "Shared") == PDF_T1_DEFINER);
    assert(cupidimage_pdf_type1_syntax_end(&syntax));
    assert(!cupidimage_pdf_type1_syntax_lookup(&syntax, "Shared"));
    assert(!cupidimage_pdf_type1_syntax_lookup(&syntax, "RD"));
    assert(cupidimage_pdf_type1_syntax_begin_dictionary(&syntax, 1));
    assert(define(&syntax, "Global", "{def} def"));
    assert(define(&syntax, "RD", reader));
    assert(cupidimage_pdf_type1_syntax_end(&syntax));
    assert(cupidimage_pdf_type1_syntax_lookup(&syntax, "Global") == PDF_T1_DEFINER);
    assert(cupidimage_pdf_type1_syntax_lookup(&syntax, "RD") == PDF_T1_READER);
    assert(define(&syntax, "P", "{noaccess put} def"));
    put = "{string currentfile exch readstring pop} P";
    position = 0;
    assert(cupidimage_pdf_type1_syntax_put_definition(
        &syntax, "Read", (pdf_cff_slice){(const unsigned char *)put, strlen(put)}, &position, 1));
    assert(cupidimage_pdf_type1_syntax_lookup(&syntax, "Read") == PDF_T1_READER);
    for (unsigned i = 0; i < 64; i++) {
        assert(cupidimage_pdf_type1_syntax_begin(&syntax));
        assert(define(&syntax, "Read", i % 2 ? reader : "17 def"));
        assert(cupidimage_pdf_type1_syntax_lookup(&syntax, "Read") == (i % 2 ? PDF_T1_READER : 0));
    }
    assert(!cupidimage_pdf_type1_syntax_begin(&syntax));
    for (unsigned i = 64; i; i--) {
        assert(cupidimage_pdf_type1_syntax_end(&syntax));
        unsigned expected = i == 1 || i % 2 ? PDF_T1_READER : 0;
        assert(cupidimage_pdf_type1_syntax_lookup(&syntax, "Read") == expected);
    }
    assert(!cupidimage_pdf_type1_syntax_end(&syntax));
    cupidimage_pdf_type1_syntax_init(&syntax);
    assert(cupidimage_pdf_type1_syntax_begin_dictionary(&syntax, 3));
    assert(!define(&syntax, "D", "{def} def"));
    position = 0;
    put = "{def} put";
    assert(cupidimage_pdf_type1_syntax_put_definition(
        &syntax, "D", (pdf_cff_slice){(const unsigned char *)put, strlen(put)}, &position, 1));
    assert(cupidimage_pdf_type1_syntax_lookup(&syntax, "D") == PDF_T1_DEFINER);
    assert(cupidimage_pdf_type1_syntax_end(&syntax));
    assert(cupidimage_pdf_type1_syntax_lookup(&syntax, "D") == PDF_T1_DEFINER);
    for (unsigned i = 0; i < 126; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Reader%u", i);
        if (i == 0)
            cupidimage_pdf_type1_syntax_init(&syntax);
        assert(define(&syntax, name, reader));
    }
    assert(!define(&syntax, "Overflow", reader));
    for (unsigned i = 0; i < 17; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Definer%u", i);
        if (i == 0)
            cupidimage_pdf_type1_syntax_init(&syntax);
        assert(define(&syntax, name, "{def} def"));
    }
    for (unsigned i = 0; i < 17; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Definer%u", i);
        assert(cupidimage_pdf_type1_syntax_lookup(&syntax, name) == PDF_T1_DEFINER);
    }
    for (size_t work = 0; work < 160; work++) {
        cupidimage_pdf_type1_syntax_init(&syntax);
        syntax.work = work;
        int ok = define(&syntax, "D", "{def} def");
        if (ok)
            ok = define(&syntax, "Read", "{string currentfile exch readstring pop} D");
        if (ok)
            assert(cupidimage_pdf_type1_syntax_lookup(&syntax, "Read") == PDF_T1_READER ||
                   syntax.failed);
        assert(syntax.work <= work);
    }
    cupidimage_pdf_type1_syntax_init(&syntax);
    assert(cupidimage_pdf_type1_syntax_builtin(&syntax, "closefile"));
    assert(define(&syntax, "closefile", "{pop} def"));
    assert(!cupidimage_pdf_type1_syntax_builtin(&syntax, "closefile"));
    assert(cupidimage_pdf_type1_syntax_begin_dictionary(&syntax, PDF_T1_SYSTEM_DICTIONARY));
    assert(cupidimage_pdf_type1_syntax_builtin(&syntax, "closefile"));
    assert(cupidimage_pdf_type1_syntax_begin(&syntax));
    assert(define(&syntax, "closefile", "{pop} def"));
    assert(!cupidimage_pdf_type1_syntax_builtin(&syntax, "closefile"));
    assert(cupidimage_pdf_type1_syntax_end(&syntax));
    assert(cupidimage_pdf_type1_syntax_builtin(&syntax, "closefile"));
    assert(cupidimage_pdf_type1_syntax_end(&syntax));
    assert(!cupidimage_pdf_type1_syntax_builtin(&syntax, "closefile"));
    syntax.work = 0;
    assert(!cupidimage_pdf_type1_syntax_builtin(&syntax, "closefile") && syntax.failed);
    checks += 12;
    printf("Type 1 scoped syntax: %u definition, inheritance, shadowing and budget checks\n",
           checks);
    return 0;
}
