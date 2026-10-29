#include "hints.h"
#include <math.h>
#include <string.h>

unsigned cupidimage_pdf_type1_hint_field(const char *name) {
    static const char *const names[] = {
        "BlueValues", "OtherBlues", "FamilyBlues",   "FamilyOtherBlues", "StemSnapH",
        "StemSnapV",  "BlueScale",  "BlueShift",     "BlueFuzz",         "StdHW",
        "StdVW",      "ForceBold",  "LanguageGroup", "ExpansionFactor"};
    for (unsigned i = 0; i < PDF_PS_HINT_FIELDS; i++)
        if (!strcmp(name, names[i]))
            return i;
    return PDF_PS_HINT_FIELDS;
}

static int t1_hint_word(pdf_type1_token token, const char *word) {
    return token.type == PDF_T1_WORD && !strcmp(token.text, word);
}

static int t1_hint_array(pdf_cff_slice source, size_t *position, int procedure, double values[14],
                         unsigned *count) {
    for (unsigned work = 0; work < 65536; work++) {
        pdf_type1_token token = cupidimage_pdf_type1_lex(source, position, 0);
        if (procedure ? token.type == PDF_T1_EOF
                      : token.type == PDF_T1_OTHER && !strcmp(token.text, "]"))
            return 1;
        if (token.type != PDF_T1_NUMBER)
            return 0;
        if (*count < 14)
            values[*count] = token.number;
        if (*count < 15)
            (*count)++;
    }
    return 0;
}

void cupidimage_pdf_type1_hint_read(pdf_ps_private_hints *hints, unsigned *unresolved,
                                    unsigned field, pdf_cff_slice source, size_t *position,
                                    const char definers[16][128], unsigned definer_count) {
    if (field >= PDF_PS_HINT_FIELDS)
        return;
    size_t pos = *position;
    double values[14] = {0};
    unsigned count = 0, bit = 1u << field;
    int known = 1, valid = 1;
    pdf_type1_token value = cupidimage_pdf_type1_lex(source, &pos, 0);
    int array = field < PDF_PS_BLUE_SCALE || field == PDF_PS_STD_HW || field == PDF_PS_STD_VW;
    if (array) {
        if (value.type == PDF_T1_PROC) {
            size_t inner = 0;
            known = t1_hint_array(value.body, &inner, 1, values, &count);
        } else if (value.type == PDF_T1_OTHER && !strcmp(value.text, "["))
            known = t1_hint_array(source, &pos, 0, values, &count);
        else
            valid = 0;
    } else if (field == PDF_PS_FORCE_BOLD) {
        if (t1_hint_word(value, "true") || t1_hint_word(value, "false")) {
            values[0] = t1_hint_word(value, "true");
            count = 1;
        } else
            valid = 0;
    } else if (value.type == PDF_T1_NUMBER) {
        values[0] = value.number;
        count = 1;
    } else
        valid = 0;
    if (value.type == PDF_T1_IMMEDIATE ||
        (value.type == PDF_T1_WORD && !t1_hint_word(value, "true") &&
         !t1_hint_word(value, "false")))
        known = 0;
    /* A literal followed by arithmetic or a conditional is not its final value. */
    pdf_type1_token end = cupidimage_pdf_type1_lex(source, &pos, 0);
    for (unsigned i = 0; i < 2 && (t1_hint_word(end, "readonly") || t1_hint_word(end, "noaccess"));
         i++)
        end = cupidimage_pdf_type1_lex(source, &pos, 0);
    int defined = t1_hint_word(end, "def");
    for (unsigned i = 0; i < definer_count && i < 16; i++)
        defined |= t1_hint_word(end, definers[i]);
    known &= defined;
    /* Type 1 blue zones, stem widths and shift/fuzz values are integers. */
    if (field != PDF_PS_BLUE_SCALE && field != PDF_PS_EXPANSION_FACTOR)
        for (unsigned i = 0; i < count && i < 14; i++)
            if (floor(values[i]) != values[i])
                valid = 0;
    if (field < PDF_PS_STEM_SNAP_H && count <= 14)
        for (unsigned i = 1; i < count; i += 2)
            if (values[i - 1] > values[i])
                valid = 0;
    cupidimage_pdf_ps_hint_set(hints, field, values, count, 0, known && valid);
    *unresolved &= ~bit;
    if (!known)
        *unresolved |= bit;
    else
        *position = pos;
}
