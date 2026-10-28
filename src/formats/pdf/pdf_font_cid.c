#include "fonts/cmap.h"
#include "pdf_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* CID metrics are separate from Unicode mappings and glyph outlines. */
void cupidimage_pdf_cid_font_free(pdf_cid_font *font) {
    if (font) {
        cupidimage_pdf_cmap_free(font->encoding);
        free(font->metrics);
        free(font);
    }
}

size_t cupidimage_pdf_cid_font_bytes(const pdf_cid_font *font) {
    return font ? sizeof(*font) + (font->metrics ? 65536 * sizeof(pdf_cid_metric) : 0) +
                      cupidimage_pdf_cmap_bytes(font->encoding)
                : 0;
}

static int cid_skip_reference(const pdf_object_view *v, size_t *p) {
    double n;
    for (int i = 0; i < 2; i++) {
        cupidimage_pdf_skip_ws_and_comments(v->data, v->len, p);
        if (!cupidimage_pdf_parse_pdf_number(v->data, v->len, p, &n))
            return 0;
    }
    cupidimage_pdf_skip_ws_and_comments(v->data, v->len, p);
    return *p < v->len && v->data[(*p)++] == 'R';
}

static int cid_number(const unsigned char *data, size_t size, const pdf_object_view *v, size_t *p,
                      double *n, unsigned depth) {
    if (depth == 16)
        return 0;
    pdf_object_id object;
    cupidimage_pdf_skip_ws_and_comments(v->data, v->len, p);
    if (cupidimage_pdf_page_reference(v, *p, &object)) {
        pdf_object_view indirect = {0};
        size_t pos = 0;
        int ok = cupidimage_pdf_view_object(data, size, v, object, &indirect) &&
                 cid_number(data, size, &indirect, &pos, n, depth + 1) && cid_skip_reference(v, p);
        cupidimage_pdf_object_view_release(&indirect);
        return ok;
    }
    return cupidimage_pdf_parse_pdf_number(v->data, v->len, p, n) && isfinite(*n) &&
           fabs(*n) <= 1e6 &&
           (*p == v->len || cupidimage_pdf_is_ws(v->data[*p]) ||
            cupidimage_pdf_is_delim(v->data[*p]));
}

static int cid_resolve(const unsigned char *data, size_t size, const pdf_object_view *v, size_t *p,
                       pdf_object_view *out, unsigned depth) {
    if (depth == 16)
        return 0;
    cupidimage_pdf_skip_ws_and_comments(v->data, v->len, p);
    pdf_object_id object;
    if (cupidimage_pdf_page_reference(v, *p, &object)) {
        pdf_object_view indirect = {0};
        size_t pos = 0;
        int ok = cupidimage_pdf_view_object(data, size, v, object, &indirect) &&
                 cid_resolve(data, size, &indirect, &pos, out, depth + 1) &&
                 cid_skip_reference(v, p);
        if (ok && !out->owned) {
            out->owned = indirect.owned;
            indirect.owned = NULL;
        }
        cupidimage_pdf_object_view_release(&indirect);
        return ok;
    }
    out->data = v->data + *p;
    out->len = v->len - *p;
    out->xref = v->xref;
    return 1;
}

static int cid_hex(unsigned char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}

static int cid_value_name(const pdf_object_view *v, size_t p, const char *name) {
    cupidimage_pdf_skip_ws_and_comments(v->data, v->len, &p);
    if (p == v->len || v->data[p++] != '/')
        return 0;
    size_t at = 0, n = strlen(name);
    while (p < v->len && !cupidimage_pdf_is_ws(v->data[p]) &&
           !cupidimage_pdf_is_delim(v->data[p])) {
        unsigned char c = v->data[p++];
        if (c == '#') {
            if (v->len - p < 2)
                return 0;
            int h = cid_hex(v->data[p]), l = cid_hex(v->data[p + 1]);
            if (h < 0 || l < 0)
                return 0;
            c = (unsigned char)(h * 16 + l);
            p += 2;
        }
        if (at == n || c != (unsigned char)name[at++])
            return 0;
    }
    return at == n;
}

static int cid_width_array(const unsigned char *data, size_t size, const pdf_object_view *v,
                           size_t p, pdf_cid_font *font, int vertical) {
    pdf_object_view array = {0};
    if (!cid_resolve(data, size, v, &p, &array, 0))
        return 0;
    size_t pos = 0;
    int ok = array.len && array.data[pos++] == '[';
    unsigned operations = 0;
    while (ok) {
        cupidimage_pdf_skip_ws_and_comments(array.data, array.len, &pos);
        if (pos < array.len && array.data[pos] == ']')
            break;
        double first, last, values[3];
        if (!cid_number(data, size, &array, &pos, &first, 0) || first < 0 || first > 65535 ||
            floor(first) != first) {
            ok = 0;
            break;
        }
        size_t saved = pos;
        pdf_object_view item = {0};
        if (!cid_resolve(data, size, &array, &pos, &item, 0)) {
            ok = 0;
            break;
        }
        int list = item.len && item.data[0] == '[';
        size_t ip = 1;
        unsigned cid = (unsigned)first;
        if (!list) {
            pos = saved;
            ok = cid_number(data, size, &array, &pos, &last, 0) && last >= first && last <= 65535 &&
                 floor(last) == last;
            for (int k = 0; ok && k < (vertical ? 3 : 1); k++)
                ok = cid_number(data, size, &array, &pos, values + k, 0);
        } else
            last = 65535;
        while (ok) {
            if (list) {
                cupidimage_pdf_skip_ws_and_comments(item.data, item.len, &ip);
                if (ip < item.len && item.data[ip] == ']') {
                    ip++;
                    break;
                }
                for (int k = 0; ok && k < (vertical ? 3 : 1); k++)
                    ok = cid_number(data, size, &item, &ip, values + k, 0);
            }
            if (!ok || cid > 65535 || ++operations > 65536) {
                ok = 0;
                break;
            }
            pdf_cid_metric *m = font->metrics + cid;
            if (vertical) {
                m->advance_y = values[0] / 1000;
                m->origin_x = values[1] / 1000;
                m->origin_y = values[2] / 1000;
                m->flags |= 2;
            } else {
                m->width = values[0] / 1000;
                m->flags |= 1;
            }
            cid++;
            if (!list && cid > (unsigned)last)
                break;
        }
        if (list && item.data == array.data + pos)
            pos += ip;
        cupidimage_pdf_object_view_release(&item);
    }
    cupidimage_pdf_object_view_release(&array);
    return ok;
}

int cupidimage_pdf_parse_cid_font(const unsigned char *data, size_t size,
                                  const pdf_object_view *view, pdf_font_map *font, size_t *budget) {
    if (!cupidimage_pdf_entry_is_name(data, size, view, "Subtype", "Type0"))
        return 1;
    size_t pos;
    if (!cupidimage_pdf_page_value(view, "Encoding", &pos))
        return 1;
    pdf_object_view encoding = {0};
    if (!cid_resolve(data, size, view, &pos, &encoding, 0))
        return 0;
    int vertical = cid_value_name(&encoding, 0, "Identity-V");
    int identity = vertical || cid_value_name(&encoding, 0, "Identity-H");
    if (!cupidimage_pdf_page_value(view, "DescendantFonts", &pos)) {
        cupidimage_pdf_object_view_release(&encoding);
        return 0;
    }
    pdf_object_view array = {0}, descendant = {0}, default_value = {0}, widths = {0},
                    vertical_widths = {0};
    pdf_cid_font *cid = NULL;
    int ok = cid_resolve(data, size, view, &pos, &array, 0);
    pos = 0;
    if (!ok || !array.len || array.data[pos++] != '[')
        goto done;
    cupidimage_pdf_skip_ws_and_comments(array.data, array.len, &pos);
    if (!cupidimage_pdf_font_dictionary_at(data, size, &array, pos, &descendant))
        goto done;
    pdf_object_id object;
    if (cupidimage_pdf_page_reference(&array, pos, &object)) {
        if (!cid_skip_reference(&array, &pos))
            goto done;
    } else
        pos += descendant.len;
    cupidimage_pdf_skip_ws_and_comments(array.data, array.len, &pos);
    if (pos == array.len || array.data[pos] != ']' ||
        (!cupidimage_pdf_entry_is_name(data, size, &descendant, "Subtype", "CIDFontType0") &&
         !cupidimage_pdf_entry_is_name(data, size, &descendant, "Subtype", "CIDFontType2")))
        goto done;
    if (*budget > 64u * 1024u * 1024u - sizeof(*cid))
        goto done;
    cid = calloc(1, sizeof(*cid));
    if (!cid)
        goto done;
    if (!identity) {
        if (!cupidimage_pdf_cmap_load(data, size, &encoding, &cid->encoding))
            goto done;
        vertical = cupidimage_pdf_cmap_vertical(cid->encoding);
    }
    cid->vertical = vertical;
    size_t metadata_cost = cupidimage_pdf_cid_font_bytes(cid);
    if (metadata_cost > 64u * 1024u * 1024u - *budget)
        goto done;
    cid->default_width = 1;
    cid->default_origin_y = .88;
    cid->default_advance_y = -1;
    double value;
    int present = cupidimage_pdf_optional_value(data, size, &descendant, "DW", &default_value);
    if (present < 0)
        goto done;
    pos = 0;
    if (present) {
        if (!cid_number(data, size, &default_value, &pos, &value, 0))
            goto done;
        cid->default_width = value / 1000;
    }
    cupidimage_pdf_object_view_release(&default_value);
    present = cupidimage_pdf_optional_value(data, size, &descendant, "DW2", &default_value);
    if (present < 0)
        goto done;
    if (present) {
        pdf_object_view defaults = default_value;
        defaults.owned = NULL;
        int valid = 1;
        size_t p = 0;
        cupidimage_pdf_skip_ws_and_comments(defaults.data, defaults.len, &p);
        valid = p < defaults.len && defaults.data[p++] == '[';
        if (valid)
            valid = cid_number(data, size, &defaults, &p, &value, 0);
        if (valid)
            cid->default_origin_y = value / 1000;
        if (valid)
            valid = cid_number(data, size, &defaults, &p, &value, 0);
        if (valid)
            cid->default_advance_y = value / 1000;
        cupidimage_pdf_skip_ws_and_comments(defaults.data, defaults.len, &p);
        valid = valid && p < defaults.len && defaults.data[p] == ']';
        cupidimage_pdf_object_view_release(&defaults);
        if (!valid)
            goto done;
    }
    int w = cupidimage_pdf_optional_value(data, size, &descendant, "W", &widths),
        w2 = cupidimage_pdf_optional_value(data, size, &descendant, "W2", &vertical_widths);
    if (w < 0 || w2 < 0)
        goto done;
    if (w || w2) {
        if (metadata_cost > 64u * 1024u * 1024u - 65536 * sizeof(pdf_cid_metric) ||
            *budget > 64u * 1024u * 1024u - metadata_cost - 65536 * sizeof(pdf_cid_metric))
            goto done;
        cid->metrics = calloc(65536, sizeof(pdf_cid_metric));
        if (!cid->metrics)
            goto done;
        if (w && !cid_width_array(data, size, &widths, 0, cid, 0))
            goto done;
        if (w2 && !cid_width_array(data, size, &vertical_widths, 0, cid, 1))
            goto done;
    }
    font->cid = cid;
    *budget += cupidimage_pdf_cid_font_bytes(cid);
    cid = NULL;
done:
    cupidimage_pdf_object_view_release(&default_value);
    cupidimage_pdf_object_view_release(&widths);
    cupidimage_pdf_object_view_release(&vertical_widths);
    cupidimage_pdf_object_view_release(&encoding);
    cupidimage_pdf_cid_font_free(cid);
    cupidimage_pdf_object_view_release(&descendant);
    cupidimage_pdf_object_view_release(&array);
    return font->cid != NULL;
}

void cupidimage_pdf_cid_metrics(const pdf_cid_font *font, unsigned cid, double *width,
                                double *advance_y, double *origin_x, double *origin_y) {
    const pdf_cid_metric *m = font->metrics ? font->metrics + cid : NULL;
    *width = m && (m->flags & 1) ? m->width : font->default_width;
    *advance_y = m && (m->flags & 2) ? m->advance_y : font->default_advance_y;
    *origin_x = m && (m->flags & 2) ? m->origin_x : *width / 2;
    *origin_y = m && (m->flags & 2) ? m->origin_y : font->default_origin_y;
}
