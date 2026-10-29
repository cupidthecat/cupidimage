#include "cff.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t cff_uint(const unsigned char *p, unsigned n) {
    uint32_t value = 0;
    for (unsigned i = 0; i < n; i++)
        value = value * 256 + p[i];
    return value;
}

int cupidimage_pdf_cff_index(pdf_cff_slice source, size_t *position, pdf_cff_index *index) {
    memset(index, 0, sizeof(*index));
    size_t pos = *position;
    if (pos > source.length || source.length - pos < 2)
        return 0;
    unsigned count = cff_uint(source.data + pos, 2);
    pos += 2;
    if (!count) {
        *position = pos;
        return 1;
    }
    if (pos == source.length)
        return 0;
    unsigned size = source.data[pos++];
    if (!size || size > 4 || (size_t)(count + 1) * size > source.length - pos)
        return 0;
    size_t offsets = pos;
    pos += (size_t)(count + 1) * size;
    uint32_t previous = 1;
    for (unsigned i = 0; i <= count; i++) {
        uint32_t value = cff_uint(source.data + offsets + (size_t)i * size, size);
        if (!value || (!i && value != 1) || value < previous ||
            (size_t)(value - 1) > source.length - pos)
            return 0;
        previous = value;
    }
    index->count = count;
    index->offset_size = size;
    index->offsets = (pdf_cff_slice){source.data + offsets, (size_t)(count + 1) * size};
    index->objects = (pdf_cff_slice){source.data + pos, previous - 1};
    *position = pos + previous - 1;
    return 1;
}

int cupidimage_pdf_cff_item(const pdf_cff_index *index, unsigned item, pdf_cff_slice *slice) {
    *slice = (pdf_cff_slice){0};
    if (item >= index->count || !index->offset_size || index->offset_size > 4 ||
        index->offsets.length < ((size_t)item + 2) * index->offset_size)
        return 0;
    const unsigned char *p = index->offsets.data + (size_t)item * index->offset_size;
    uint32_t start = cff_uint(p, index->offset_size),
             end = cff_uint(p + index->offset_size, index->offset_size);
    if (!start || end < start || (size_t)(end - 1) > index->objects.length)
        return 0;
    *slice = (pdf_cff_slice){index->objects.data + start - 1, end - start};
    return 1;
}

static int cff_number(pdf_cff_slice source, size_t *position, double *value) {
    size_t pos = *position;
    if (pos >= source.length)
        return 0;
    unsigned first = source.data[pos++];
    if (first >= 32 && first <= 246)
        *value = (int)first - 139;
    else if (first >= 247 && first <= 254) {
        if (pos == source.length)
            return 0;
        unsigned next = source.data[pos++];
        *value = first <= 250 ? (double)((first - 247) * 256 + next + 108)
                              : -(double)((first - 251) * 256 + next + 108);
    } else if (first == 28 || first == 29) {
        unsigned n = first == 28 ? 2 : 4;
        if (source.length - pos < n)
            return 0;
        uint32_t raw = cff_uint(source.data + pos, n);
        *value = (double)raw -
                 ((raw & (n == 2 ? 0x8000u : 0x80000000u)) ? (n == 2 ? 65536.0 : 4294967296.0) : 0);
        pos += n;
    } else if (first == 30) {
        char number[128];
        unsigned used = 0;
        int ended = 0;
        while (pos < source.length && !ended) {
            unsigned byte = source.data[pos++];
            for (unsigned part = 0; part < 2; part++) {
                unsigned nibble = part ? byte & 15 : byte >> 4;
                if (nibble == 15) {
                    if (!part && (byte & 15) != 15)
                        return 0;
                    ended = 1;
                    break;
                }
                if (nibble == 13 || used > sizeof(number) - 3)
                    return 0;
                number[used++] = nibble < 10    ? (char)('0' + nibble)
                                 : nibble == 10 ? '.'
                                 : nibble == 14 ? '-'
                                                : 'E';
                if (nibble == 12)
                    number[used++] = '-';
            }
        }
        if (!ended || !used)
            return 0;
        number[used] = 0;
        char *end;
        *value = strtod(number, &end);
        if (*end || !isfinite(*value))
            return 0;
    } else
        return 0;
    *position = pos;
    return 1;
}

static int cff_offset(double value, size_t *offset) {
    /* PDF font programs have a 16 MiB outer bound; this also avoids unsafe casts. */
    if (!isfinite(value) || value < 0 || value > 16u * 1024u * 1024u || floor(value) != value)
        return 0;
    *offset = (size_t)value;
    return 1;
}

int cupidimage_pdf_cff_dict(pdf_cff_slice source, pdf_cff_dict *dict, int private_dict) {
    memset(dict, 0, sizeof(*dict));
    dict->matrix[0] = dict->matrix[3] = .001;
    cupidimage_pdf_ps_hints_init(&dict->hints);
    double operands[48];
    unsigned char integer_operand[48];
    unsigned count = 0;
    size_t pos = 0;
    while (pos < source.length) {
        unsigned op = source.data[pos];
        if (op >= 28) {
            if (count == 48 || !cff_number(source, &pos, operands + count))
                return 0;
            integer_operand[count] = op != 30;
            count++;
            continue;
        }
        pos++;
        if (op == 12) {
            if (pos == source.length)
                return 0;
            op = 256 + source.data[pos++];
        }
        size_t *offset = NULL;
        if (private_dict) {
            static const unsigned hint_ops[PDF_PS_HINT_FIELDS] = {6,   7,   8,  9,  268, 269, 265,
                                                                  266, 267, 10, 11, 270, 273, 274};
            for (unsigned field = 0; field < PDF_PS_HINT_FIELDS; field++) {
                if (op != hint_ops[field])
                    continue;
                int valid = 1;
                if (field == PDF_PS_OTHER_BLUES || field == PDF_PS_FAMILY_OTHER_BLUES)
                    valid = (dict->hints.seen & (1u << (field - 1))) != 0;
                if (field == PDF_PS_FORCE_BOLD && count == 1 && !integer_operand[0])
                    valid = 0;
                cupidimage_pdf_ps_hint_set(&dict->hints, field, operands, count, 1, valid);
                break;
            }
            if (op == 19)
                offset = &dict->subrs;
            else if (op == 275) {
                if (count != 1 || operands[0] < -2147483648.0 || operands[0] > 2147483647.0 ||
                    floor(operands[0]) != operands[0])
                    return 0;
                dict->random_seed = (int32_t)operands[0];
            } else if (op == 20 || op == 21) {
                if (count != 1)
                    return 0;
                if (op == 20)
                    dict->default_width = operands[0];
                else
                    dict->nominal_width = operands[0];
            }
        } else {
            if (op == 15)
                offset = &dict->charset;
            if (op == 16)
                offset = &dict->encoding;
            if (op == 17)
                offset = &dict->charstrings;
            if (op == 292)
                offset = &dict->fdarray;
            if (op == 293)
                offset = &dict->fdselect;
            if (op == 18) {
                if (count != 2 || !cff_offset(operands[0], &dict->private_size) ||
                    !cff_offset(operands[1], &dict->private_offset))
                    return 0;
            } else if (op == 263) {
                if (count != 6)
                    return 0;
                memcpy(dict->matrix, operands, sizeof(dict->matrix));
                dict->has_matrix = 1;
            } else if (op == 262 && (count != 1 || operands[0] != 2))
                return 0;
            else if (op == 286) {
                if (count != 3 || operands[0] < 0 || operands[0] > 64999 || operands[1] < 0 ||
                    operands[1] > 64999 || operands[2] < 0 || floor(operands[0]) != operands[0] ||
                    floor(operands[1]) != operands[1] || floor(operands[2]) != operands[2])
                    return 0;
                dict->cid = 1;
            } else if (op == 276)
                return 0; /* Synthetic font inheritance needs a separate program resolver. */
        }
        if (offset && (count != 1 || !cff_offset(operands[0], offset)))
            return 0;
        if ((op > 21 && op < 256) || op > 294)
            return 0;
        count = 0;
    }
    return count == 0;
}

#include "cff_tables.inc"

static int cff_slice(pdf_cff_slice source, size_t start, size_t length, pdf_cff_slice *out) {
    if (start > source.length || length > source.length - start)
        return 0;
    *out = (pdf_cff_slice){source.data + start, length};
    return 1;
}

static int cff_selector(pdf_cff_font *font, unsigned gid, unsigned sid) {
    if (!sid || sid > 65535 || font->selectors[sid] ||
        (!font->top.cid && (sid >= 65000 || sid >= 391 + font->strings.count)))
        return 0;
    font->selectors[sid] = (uint16_t)gid;
    return 1;
}

static int cff_charset(pdf_cff_font *font) {
    size_t pos = font->top.charset;
    unsigned count = font->chars.count;
    if (pos <= 2) {
        if (font->top.cid)
            return 0;
        unsigned limit = pos == 0 ? 229 : pos == 1 ? 166 : 87;
        if (count > limit)
            return 0;
        for (unsigned gid = 1; gid < count; gid++) {
            unsigned sid = pos == 0   ? gid
                           : pos == 1 ? cff_expert_charset[gid]
                                      : cff_expert_subset_charset[gid];
            if (!cff_selector(font, gid, sid))
                return 0;
        }
        return 1;
    }
    if (pos >= font->source.length)
        return 0;
    unsigned format = font->source.data[pos++], gid = 1;
    if (format > 2)
        return 0;
    while (gid < count) {
        size_t needed = format == 0 ? 2 : format == 1 ? 3 : 4;
        if (font->source.length - pos < needed)
            return 0;
        unsigned first = cff_uint(font->source.data + pos, 2),
                 left = format == 0 ? 0 : cff_uint(font->source.data + pos + 2, format);
        pos += needed;
        if (left >= count - gid || first + left > 65535)
            return 0;
        for (unsigned i = 0; i <= left; i++)
            if (!cff_selector(font, gid++, first + i))
                return 0;
    }
    return 1;
}

static int cff_encoding(pdf_cff_font *font) {
    if (font->top.cid)
        return 1;
    size_t pos = font->top.encoding;
    if (pos <= 1) {
        const unsigned short *table = pos ? cff_expert_encoding : cff_standard_encoding;
        for (unsigned i = 0; i < 256; i++)
            font->encoding[i] = font->selectors[table[i]];
        return 1;
    }
    if (pos >= font->source.length || font->source.length - pos < 2)
        return 0;
    unsigned format = font->source.data[pos++], count = font->source.data[pos++], gid = 1;
    if ((format & 127) > 1)
        return 0;
    for (unsigned i = 0; i < count; i++) {
        unsigned needed = (format & 127) ? 2 : 1;
        if (font->source.length - pos < needed)
            return 0;
        unsigned first = font->source.data[pos++],
                 left = needed == 2 ? font->source.data[pos++] : 0;
        if (first + left > 255 || gid >= font->chars.count || left >= font->chars.count - gid)
            return 0;
        for (unsigned j = 0; j <= left; j++) {
            if (font->encoding[first + j])
                return 0;
            font->encoding[first + j] = gid++;
        }
    }
    if (format & 128) {
        if (pos == font->source.length)
            return 0;
        unsigned supplements = font->source.data[pos++];
        if ((size_t)supplements * 3 > font->source.length - pos)
            return 0;
        for (unsigned i = 0; i < supplements; i++) {
            unsigned code = font->source.data[pos++], sid = cff_uint(font->source.data + pos, 2);
            pos += 2;
            if (!font->selectors[sid] || font->encoding[code])
                return 0;
            font->encoding[code] = font->selectors[sid];
        }
    }
    return 1;
}

static int cff_private(pdf_cff_font *font, const pdf_cff_dict *owner, pdf_cff_private *result) {
    pdf_cff_slice data;
    pdf_cff_dict dict;
    if (!cff_slice(font->source, owner->private_offset, owner->private_size, &data) ||
        !cupidimage_pdf_cff_dict(data, &dict, 1))
        return 0;
    if (dict.subrs) {
        if (dict.subrs < owner->private_size ||
            dict.subrs > font->source.length - owner->private_offset)
            return 0;
        size_t pos = owner->private_offset + dict.subrs;
        if (!cupidimage_pdf_cff_index(font->source, &pos, &result->subrs))
            return 0;
    }
    result->hints = dict.hints;
    result->random_seed = dict.random_seed ? (uint32_t)dict.random_seed : 1;
    result->default_width = dict.default_width;
    result->nominal_width = dict.nominal_width;
    memcpy(result->matrix, owner->matrix, sizeof(result->matrix));
    return 1;
}

static int cff_fdselect(pdf_cff_font *font) {
    size_t pos = font->top.fdselect;
    if (!pos || pos >= font->source.length)
        return 0;
    unsigned format = font->source.data[pos++], glyphs = font->chars.count;
    if (format == 0) {
        if (glyphs > font->source.length - pos)
            return 0;
        for (unsigned i = 0; i < glyphs; i++) {
            unsigned fd = font->source.data[pos++];
            if (fd >= font->private_count)
                return 0;
            font->fdselect[i] = (unsigned char)fd;
        }
        return 1;
    }
    if (format != 3 || font->source.length - pos < 2)
        return 0;
    unsigned ranges = cff_uint(font->source.data + pos, 2);
    pos += 2;
    if (!ranges || (size_t)ranges * 3 + 2 > font->source.length - pos)
        return 0;
    for (unsigned i = 0; i < ranges; i++) {
        unsigned first = cff_uint(font->source.data + pos, 2), fd = font->source.data[pos + 2],
                 next = cff_uint(font->source.data + pos + 3, 2);
        pos += 3;
        if ((!i && first) || next <= first || next > glyphs || fd >= font->private_count ||
            (i == ranges - 1 && next != glyphs))
            return 0;
        memset(font->fdselect + first, fd, next - first);
    }
    return 1;
}

void cupidimage_pdf_cff_free(pdf_cff_font *font) {
    free(font->selectors);
    free(font->fdselect);
    free(font->private_dicts);
    memset(font, 0, sizeof(*font));
}

int cupidimage_pdf_cff_init(pdf_cff_font *font, const unsigned char *data, size_t length) {
    memset(font, 0, sizeof(*font));
    if (length < 4 || length > 16u * 1024u * 1024u || data[0] != 1 || data[1] != 0 || data[2] < 4 ||
        data[2] > length || data[3] < 1 || data[3] > 4)
        return 0;
    font->source = (pdf_cff_slice){data, length};
    size_t pos = data[2];
    pdf_cff_index names, top, fds;
    pdf_cff_slice dict;
    if (!cupidimage_pdf_cff_index(font->source, &pos, &names) || names.count != 1 ||
        !cupidimage_pdf_cff_index(font->source, &pos, &top) || top.count != 1 ||
        !cupidimage_pdf_cff_index(font->source, &pos, &font->strings) ||
        font->strings.count > 65000 - 391 ||
        !cupidimage_pdf_cff_index(font->source, &pos, &font->globals) ||
        !cupidimage_pdf_cff_item(&top, 0, &dict) || !cupidimage_pdf_cff_dict(dict, &font->top, 0) ||
        !font->top.charstrings)
        goto fail;
    pdf_cff_slice name;
    if (!cupidimage_pdf_cff_item(&names, 0, &name) || !name.length || name.length > 127)
        goto fail;
    pos = font->top.charstrings;
    if (!cupidimage_pdf_cff_index(font->source, &pos, &font->chars) || !font->chars.count)
        goto fail;
    font->selectors = calloc(65536, sizeof(*font->selectors));
    if (!font->selectors || !cff_charset(font) || !cff_encoding(font))
        goto fail;
    font->private_count = 1;
    if (font->top.cid) {
        pos = font->top.fdarray;
        if (!pos || !cupidimage_pdf_cff_index(font->source, &pos, &fds) || !fds.count ||
            fds.count > 256)
            goto fail;
        font->private_count = fds.count;
        font->fdselect = calloc(font->chars.count, 1);
        if (!font->fdselect || !cff_fdselect(font))
            goto fail;
    }
    font->private_dicts = calloc(font->private_count, sizeof(*font->private_dicts));
    if (!font->private_dicts)
        goto fail;
    for (unsigned i = 0; i < font->private_count; i++) {
        pdf_cff_dict owner = font->top;
        if (font->top.cid) {
            if (!cupidimage_pdf_cff_item(&fds, i, &dict) ||
                !cupidimage_pdf_cff_dict(dict, &owner, 0))
                goto fail;
            if (!owner.has_matrix)
                memcpy(owner.matrix, font->top.matrix, sizeof(owner.matrix));
            else if (font->top.has_matrix) {
                /* A subsidiary matrix stands alone when the CIDFont has no
                 * explicit top-level matrix. Applying the Type 1 default here
                 * would shrink glyphs by an extra factor of 1000. */
                double a[6];
                memcpy(a, owner.matrix, sizeof(a));
                const double *b = font->top.matrix;
                owner.matrix[0] = b[0] * a[0] + b[2] * a[1];
                owner.matrix[1] = b[1] * a[0] + b[3] * a[1];
                owner.matrix[2] = b[0] * a[2] + b[2] * a[3];
                owner.matrix[3] = b[1] * a[2] + b[3] * a[3];
                owner.matrix[4] = b[0] * a[4] + b[2] * a[5] + b[4];
                owner.matrix[5] = b[1] * a[4] + b[3] * a[5] + b[5];
            }
        }
        for (unsigned j = 0; j < 6; j++)
            if (!isfinite(owner.matrix[j]) || fabs(owner.matrix[j]) > 1e30)
                goto fail;
        if (!cff_private(font, &owner, font->private_dicts + i))
            goto fail;
    }
    return 1;
fail:
    cupidimage_pdf_cff_free(font);
    return 0;
}

unsigned cupidimage_pdf_cff_name(const pdf_cff_font *font, const char *name) {
    for (unsigned i = 0; i < 391; i++)
        if (!strcmp(name, cff_strings[i]))
            return font->selectors[i];
    size_t length = strlen(name);
    for (unsigned i = 0; i < font->strings.count; i++) {
        pdf_cff_slice value;
        if (cupidimage_pdf_cff_item(&font->strings, i, &value) && value.length == length &&
            !memcmp(name, value.data, length))
            return font->selectors[i + 391];
    }
    return 0;
}

unsigned cupidimage_pdf_cff_standard_glyph(const pdf_cff_font *font, unsigned code) {
    return code < 256 ? font->selectors[cff_standard_encoding[code]] : 0;
}
