#include "policy.h"
#include <string.h>

typedef struct {
    uint32_t checksum, length;
} tt_program_id;

#include "../../../../../third_party/freetype-tt-policy/family-names.inc"
#include "../../../../../third_party/freetype-tt-policy/program-ids.inc"

typedef struct {
    const unsigned char *data;
    size_t length;
    unsigned wide;
} tt_family;

static unsigned policy_u16(const unsigned char *p) {
    return (unsigned)p[0] * 256 + p[1];
}

static int policy_charge(size_t *work, size_t amount) {
    if (amount > *work)
        return 0;
    *work -= amount;
    return 1;
}

static int policy_family(const pdf_tt_table *table, unsigned id, tt_family *family, size_t *work) {
    if (!table->data || table->length < 6 || policy_u16(table->data) > 1)
        return 0;
    unsigned count = policy_u16(table->data + 2), storage = policy_u16(table->data + 4);
    if (count > (table->length - 6) / 12 || storage > table->length)
        return 0;
    if (!policy_charge(work, count))
        return -1;
    int apple_roman = -1, apple_english = -1, windows = -1, unicode = -1, english = 0;
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *r = table->data + 6 + i * 12;
        unsigned platform = policy_u16(r), encoding = policy_u16(r + 2),
                 language = policy_u16(r + 4);
        unsigned length = policy_u16(r + 8), offset = policy_u16(r + 10);
        if (policy_u16(r + 6) != id || !length || offset > table->length - storage ||
            length > table->length - storage - offset)
            continue;
        if (platform == 0 || platform == 2)
            unicode = (int)i;
        else if (platform == 1) {
            if (language == 0)
                apple_english = (int)i;
            else if (encoding == 0)
                apple_roman = (int)i;
        } else if (platform == 3 && (windows < 0 || (language & 1023) == 9) &&
                   (encoding == 0 || encoding == 1 || encoding == 10)) {
            windows = (int)i;
            english = (language & 1023) == 9;
        }
    }
    int apple = apple_english >= 0 ? apple_english : apple_roman;
    int selected = windows >= 0 && !(apple >= 0 && !english) ? windows
                   : apple >= 0                              ? apple
                                                             : unicode;
    if (selected < 0)
        return 0;
    const unsigned char *r = table->data + 6 + (size_t)selected * 12;
    family->data = table->data + storage + policy_u16(r + 10);
    family->wide = policy_u16(r) != 1;
    family->length = policy_u16(r + 8) / (family->wide ? 2 : 1);
    return 1;
}

static unsigned policy_character(const tt_family *family, size_t at) {
    unsigned c = family->wide ? policy_u16(family->data + at * 2) : family->data[at];
    return c == 0 ? 0 : c < 32 || c > 127 ? '?' : c;
}

static int policy_named(const tt_family *family, size_t *work, int *matched) {
    size_t start = 0;
    if (family->length >= 8 && policy_character(family, 6) == '+' && policy_character(family, 7)) {
        start = 7;
        for (unsigned i = 0; i < 6; i++) {
            unsigned c = policy_character(family, i);
            if (c < 'A' || c > 'Z')
                start = 0;
        }
    }
    for (size_t at = start; at < family->length; at++) {
        if (!policy_charge(work, 1))
            return 0;
        if (!policy_character(family, at))
            break;
        for (unsigned n = 0; n < sizeof(tt_full_hinting_names) / sizeof(*tt_full_hinting_names);
             n++) {
            const char *name = tt_full_hinting_names[n];
            size_t length = strlen(name);
            if (length > family->length - at)
                continue;
            size_t j = 0;
            for (; j < length; j++) {
                if (!policy_charge(work, 1))
                    return 0;
                if (policy_character(family, at + j) != (unsigned char)name[j])
                    break;
            }
            if (j == length) {
                *matched = 1;
                return 1;
            }
        }
    }
    return 1;
}

static int policy_checksum(const pdf_tt_table *table, size_t *work, uint32_t *checksum) {
    size_t words = table->length / 4 + (table->length % 4 != 0);
    if ((table->length && !table->data) || !policy_charge(work, words))
        return 0;
    uint32_t sum = 0;
    for (size_t at = 0; at < table->length;) {
        uint32_t word = 0;
        for (unsigned byte = 0; byte < 4; byte++) {
            word <<= 8;
            if (at < table->length)
                word |= table->data[at++];
        }
        sum += word;
    }
    *checksum = sum;
    return 1;
}

int cupidimage_pdf_tt_full_hinting(const pdf_tt_font *font, size_t *work, int *required) {
    if (!font || !work || !required)
        return 0;
    *required = 0;
    int wws = font->os2.data && font->os2.length >= 64 && policy_u16(font->os2.data) != 65535 &&
              (policy_u16(font->os2.data + 62) & 256);
    const unsigned ids[] = {21, 16, 1};
    for (unsigned i = wws ? 1 : 0; i < 3; i++) {
        tt_family family;
        int found = policy_family(&font->name, ids[i], &family, work);
        if (found < 0)
            return 0;
        if (found) {
            if (!policy_named(&family, work, required))
                return 0;
            break;
        }
    }
    if (*required)
        return 1;
    const pdf_tt_table *tables[] = {&font->cvt, &font->fpgm, &font->prep};
    uint32_t checksums[3] = {0};
    unsigned known = 0;
    for (unsigned i = 0; i < sizeof(tt_full_hinting_ids) / sizeof(*tt_full_hinting_ids); i++) {
        if (!policy_charge(work, 3))
            return 0;
        int equal = 1;
        for (unsigned j = 0; j < 3; j++)
            equal &= tables[j]->length == tt_full_hinting_ids[i][j].length;
        if (!equal)
            continue;
        for (unsigned j = 0; j < 3; j++) {
            if (!(known & (1u << j))) {
                if (!policy_checksum(tables[j], work, checksums + j))
                    return 0;
                known |= 1u << j;
            }
            equal &= checksums[j] == tt_full_hinting_ids[i][j].checksum;
        }
        if (equal) {
            *required = 1;
            return 1;
        }
    }
    return 1;
}
