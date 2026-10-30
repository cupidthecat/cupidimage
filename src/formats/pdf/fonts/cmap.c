#include "cmap.h"
#include "cmap_predefined.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t low, high;
    unsigned cid;
    unsigned char length, notdef;
} cmap_range;

struct pdf_cmap {
    cmap_range spaces[256], *ranges;
    unsigned space_count, count, capacity;
    int vertical, sorted;
    char name[128];
    pdf_cmap *base;
};

typedef struct {
    int type;
    char text[128];
    uint32_t code;
    unsigned length;
    double number;
} cmap_token;

enum { CM_END, CM_WORD, CM_NAME, CM_NUMBER, CM_HEX, CM_OTHER, CM_ERROR };

static int cmap_hex(unsigned char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}

static cmap_token cmap_lex(const unsigned char *data, size_t length, size_t *pos,
                           unsigned nesting) {
    cmap_token t = {0};
    if (nesting == 64) {
        t.type = CM_ERROR;
        return t;
    }
    cupidimage_pdf_skip_ws_and_comments(data, length, pos);
    if (*pos == length)
        return t;
    unsigned char c = data[(*pos)++];
    if (c == '(') {
        unsigned depth = 1;
        while (*pos < length && depth) {
            c = data[(*pos)++];
            if (c == '\\' && *pos < length)
                (*pos)++;
            else if (c == '(') {
                if (++depth > 64)
                    break;
            } else if (c == ')')
                depth--;
        }
        t.type = depth ? CM_ERROR : CM_OTHER;
        return t;
    }
    if (c == '[' || c == '{') {
        unsigned char close = c == '[' ? ']' : '}';
        while (*pos < length) {
            cupidimage_pdf_skip_ws_and_comments(data, length, pos);
            if (*pos < length && data[*pos] == close) {
                (*pos)++;
                t.type = CM_OTHER;
                return t;
            }
            cmap_token item = cmap_lex(data, length, pos, nesting + 1);
            if (item.type == CM_ERROR || item.type == CM_END)
                break;
        }
        t.type = CM_ERROR;
        return t;
    }
    if ((c == '<' || c == '>') && *pos < length && data[*pos] == c) {
        (*pos)++;
        t.type = CM_WORD;
        t.text[0] = t.text[1] = (char)c;
        return t;
    }
    if (c == '<') {
        unsigned nibbles = 0;
        while (*pos < length && data[*pos] != '>') {
            c = data[(*pos)++];
            if (cupidimage_pdf_is_ws(c))
                continue;
            int n = cmap_hex(c);
            if (n < 0) {
                t.type = CM_ERROR;
                return t;
            }
            if (nibbles < 8)
                t.code = t.code * 16 + (unsigned)n;
            nibbles++;
        }
        if (*pos == length) {
            t.type = CM_ERROR;
            return t;
        }
        (*pos)++;
        if (nibbles % 2) {
            t.code <<= 4;
            nibbles++;
        }
        t.length = nibbles / 2;
        t.type = t.length && t.length <= 4 ? CM_HEX : CM_OTHER;
        return t;
    }
    size_t start = *pos - 1, used = 0;
    int name = c == '/';
    if (name)
        start = *pos;
    else if (cupidimage_pdf_is_delim(c)) {
        t.type = CM_OTHER;
        return t;
    }
    *pos = start;
    while (*pos < length && !cupidimage_pdf_is_ws(data[*pos]) &&
           !cupidimage_pdf_is_delim(data[*pos])) {
        c = data[(*pos)++];
        if (name && c == '#') {
            if (length - *pos < 2) {
                t.type = CM_ERROR;
                return t;
            }
            int a = cmap_hex(data[*pos]), b = cmap_hex(data[*pos + 1]);
            if (a < 0 || b < 0) {
                t.type = CM_ERROR;
                return t;
            }
            c = (unsigned char)(a * 16 + b);
            *pos += 2;
        }
        if (!c || used == 127) {
            t.type = CM_ERROR;
            return t;
        }
        t.text[used++] = (char)c;
    }
    if (!used) {
        t.type = CM_ERROR;
        return t;
    }
    t.type = name ? CM_NAME : CM_WORD;
    if (!name) {
        char *end;
        double n = strtod(t.text, &end);
        if (end != t.text && !*end && isfinite(n)) {
            t.number = n;
            t.type = CM_NUMBER;
        }
    }
    return t;
}

void cupidimage_pdf_cmap_free(pdf_cmap *map) {
    if (map) {
        cupidimage_pdf_cmap_free(map->base);
        free(map->ranges);
        free(map);
    }
}

size_t cupidimage_pdf_cmap_bytes(const pdf_cmap *map) {
    return map ? sizeof(*map) + (size_t)map->capacity * sizeof(*map->ranges) +
                     cupidimage_pdf_cmap_bytes(map->base)
               : 0;
}

int cupidimage_pdf_cmap_vertical(const pdf_cmap *map) {
    return map->vertical;
}

static int cmap_append(pdf_cmap *map, cmap_range range) {
    if (map->count == 65536)
        return 0;
    if (map->count == map->capacity) {
        unsigned capacity = map->capacity ? map->capacity * 2 : 256;
        cmap_range *p = realloc(map->ranges, (size_t)capacity * sizeof(*p));
        if (!p)
            return 0;
        map->ranges = p;
        map->capacity = capacity;
    }
    map->ranges[map->count++] = range;
    return 1;
}

static int cmap_space(pdf_cmap *map, cmap_range range) {
    for (unsigned b = 0; b < range.length; b++) {
        unsigned shift = (range.length - b - 1) * 8;
        if (((range.low >> shift) & 255) > ((range.high >> shift) & 255))
            return 0;
    }
    for (unsigned i = 0; i < map->space_count; i++) {
        cmap_range *old = map->spaces + i;
        if (old->length == range.length && old->low == range.low && old->high == range.high)
            return 1;
        unsigned n = old->length < range.length ? old->length : range.length;
        int overlap = 1;
        for (unsigned b = 0; b < n; b++) {
            unsigned sa = (old->length - b - 1) * 8, sb = (range.length - b - 1) * 8;
            unsigned al = (old->low >> sa) & 255, ah = (old->high >> sa) & 255,
                     bl = (range.low >> sb) & 255, bh = (range.high >> sb) & 255;
            if (ah < bl || bh < al)
                overlap = 0;
        }
        if (overlap)
            return 0;
    }
    if (map->space_count == 256)
        return 0;
    map->spaces[map->space_count++] = range;
    return 1;
}

static int cmap_unambiguous(uint32_t low, uint32_t high, unsigned length) {
    int different = 0;
    for (unsigned b = 0; b < length; b++) {
        unsigned shift = (length - b - 1) * 8, lo = (low >> shift) & 255,
                 hi = (high >> shift) & 255;
        if (different && (lo != 0 || hi != 255))
            return 0;
        if (lo != hi)
            different = 1;
    }
    return low <= high;
}

static int cmap_compare(const void *a, const void *b) {
    const cmap_range *x = a, *y = b;
    unsigned xc = x->length * 2 + x->notdef, yc = y->length * 2 + y->notdef;
    if (xc != yc)
        return xc < yc ? -1 : 1;
    if (x->low != y->low)
        return x->low < y->low ? -1 : 1;
    return x->high < y->high ? -1 : x->high > y->high;
}

static int cmap_index(pdf_cmap *map) {
    if (!map->count) {
        map->sorted = 1;
        return 1;
    }
    cmap_range *sorted = malloc((size_t)map->count * sizeof(*sorted));
    if (!sorted)
        return 0;
    memcpy(sorted, map->ranges, (size_t)map->count * sizeof(*sorted));
    qsort(sorted, map->count, sizeof(*sorted), cmap_compare);
    int disjoint = 1;
    for (unsigned i = 1; i < map->count; i++)
        if (sorted[i].length == sorted[i - 1].length && sorted[i].notdef == sorted[i - 1].notdef &&
            sorted[i].low <= sorted[i - 1].high)
            disjoint = 0;
    if (disjoint) {
        free(map->ranges);
        map->ranges = sorted;
        map->capacity = map->count;
        map->sorted = 1;
    } else
        free(sorted);
    return 1;
}

static int cmap_block(pdf_cmap *map, const unsigned char *data, size_t length, size_t *pos,
                      const char *kind, unsigned count) {
    int space = !strcmp(kind, "begincodespacerange"),
        single = !strcmp(kind, "begincidchar") || !strcmp(kind, "beginnotdefchar"),
        notdef = strstr(kind, "notdef") != NULL;
    for (unsigned i = 0; i < count; i++) {
        cmap_token low = cmap_lex(data, length, pos, 0),
                   high = single ? low : cmap_lex(data, length, pos, 0), cid = {0};
        if (low.type != CM_HEX || high.type != CM_HEX || low.length != high.length ||
            low.code > high.code)
            return 0;
        cmap_range range = {low.code, high.code, 0, (unsigned char)low.length,
                            (unsigned char)notdef};
        if (space) {
            if (!cmap_space(map, range))
                return 0;
            continue;
        }
        cid = cmap_lex(data, length, pos, 0);
        if (cid.type != CM_NUMBER || cid.number < 0 || cid.number > 65535 ||
            floor(cid.number) != cid.number ||
            !cmap_unambiguous(range.low, range.high, range.length))
            return 0;
        range.cid = (unsigned)cid.number;
        if (!notdef && (uint64_t)range.cid + range.high - range.low > 65535)
            return 0;
        if (!cmap_append(map, range))
            return 0;
    }
    cmap_token end = cmap_lex(data, length, pos, 0);
    return end.type == CM_WORD && !strncmp(end.text, "end", 3) && !strcmp(end.text + 3, kind + 5);
}

static int cmap_program(pdf_cmap *map, const unsigned char *data, size_t length, int dict_mode) {
    size_t pos = 0;
    cmap_token older = {0}, previous = {0};
    int scope = 0, active = 0, literal = 0, local_mode = -1;
    while (1) {
        cmap_token t = cmap_lex(data, length, &pos, 0);
        if (t.type == CM_END)
            break;
        if (t.type == CM_ERROR)
            return 0;
        if (t.type == CM_WORD) {
            if (!strcmp(t.text, "<<")) {
                literal++;
                continue;
            }
            if (!strcmp(t.text, ">>")) {
                if (!literal)
                    return 0;
                literal--;
                continue;
            }
            if (!strcmp(t.text, "begin")) {
                if (++scope > 64)
                    return 0;
            } else if (!strcmp(t.text, "end")) {
                if (scope)
                    scope--;
            } else if (!strcmp(t.text, "begincmap"))
                active = scope;
            if (literal || scope != active) {
                older = previous;
                previous = t;
                continue;
            }
            if (!strcmp(t.text, "def") && older.type == CM_NAME) {
                if (!strcmp(older.text, "WMode")) {
                    if (previous.type != CM_NUMBER ||
                        (previous.number != 0 && previous.number != 1))
                        return 0;
                    local_mode = (int)previous.number;
                }
                if (!strcmp(older.text, "CMapType") &&
                    (previous.type != CM_NUMBER || previous.number != 1))
                    return 0;
                if (!strcmp(older.text, "CMapName") && previous.type == CM_NAME)
                    memcpy(map->name, previous.text, sizeof(map->name));
            }
            if (!strcmp(t.text, "begincodespacerange") || !strcmp(t.text, "begincidchar") ||
                !strcmp(t.text, "begincidrange") || !strcmp(t.text, "beginnotdefchar") ||
                !strcmp(t.text, "beginnotdefrange")) {
                if (previous.type != CM_NUMBER || previous.number < 0 || previous.number > 65536 ||
                    floor(previous.number) != previous.number ||
                    !cmap_block(map, data, length, &pos, t.text, (unsigned)previous.number))
                    return 0;
                memset(&previous, 0, sizeof(previous));
                memset(&older, 0, sizeof(older));
                continue;
            }
            if (!strcmp(t.text, "usefont") && (previous.type != CM_NUMBER || previous.number != 0))
                return 0;
            if (!strcmp(t.text, "usecmap")) {
                if (!map->base || map->count || previous.type != CM_NAME ||
                    strcmp(previous.text, map->base->name))
                    return 0;
            }
            if (!strcmp(t.text, "beginbfchar") || !strcmp(t.text, "beginbfrange") ||
                !strcmp(t.text, "beginrearrangedfont") || !strcmp(t.text, "beginusematrix"))
                return 0;
        }
        older = previous;
        previous = t;
    }
    if (literal)
        return 0;
    if (local_mode >= 0) {
        if (dict_mode >= 0 && dict_mode != local_mode)
            return 0;
        map->vertical = local_mode;
    } else if (dict_mode >= 0)
        map->vertical = dict_mode;
    return map->space_count && cmap_index(map);
}

static int cmap_load(const unsigned char *doc, size_t size, const pdf_object_view *view,
                     pdf_cmap **result, pdf_object_id chain[5], unsigned depth,
                     size_t *decoded_bytes) {
    if (depth == 5)
        return 0;
    pdf_cmap *map = calloc(1, sizeof(*map));
    if (!map)
        return 0;
    pdf_object_view base = {0};
    unsigned char *decoded = NULL;
    size_t p = 0, length = 0;
    int ok = 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &p);
    if (p < view->len && view->data[p] == '/') {
        cmap_token name = cmap_lex(view->data, view->len, &p, 0);
        if (name.type != CM_NAME)
            goto done;
        memcpy(map->name, name.text, sizeof(map->name));
        if (!strcmp(name.text, "Identity-H") || !strcmp(name.text, "Identity-V")) {
            map->vertical = !strcmp(name.text, "Identity-V");
            cmap_range identity = {0, 65535, 0, 2, 0};
            if (!cmap_space(map, identity) || !cmap_append(map, identity) || !cmap_index(map))
                goto done;
        } else {
            const pdf_predefined_cmap *resource = cupidimage_pdf_predefined_cmap(name.text);
            if (!resource || resource->length > 4u * 1024u * 1024u - *decoded_bytes)
                goto done;
            *decoded_bytes += resource->length;
            if (*resource->base) {
                unsigned char base_name[130];
                size_t n = strlen(resource->base);
                if (n > 127)
                    goto done;
                base_name[0] = '/';
                memcpy(base_name + 1, resource->base, n);
                pdf_object_view inherited = {0};
                inherited.data = base_name;
                inherited.len = n + 1;
                if (!cmap_load(doc, size, &inherited, &map->base, chain, depth + 1, decoded_bytes))
                    goto done;
                map->space_count = map->base->space_count;
                memcpy(map->spaces, map->base->spaces, map->space_count * sizeof(*map->spaces));
            }
            decoded = malloc(resource->length);
            if (!decoded ||
                !cupidimage_inflate_zlib(resource->data, resource->packed_length, decoded,
                                         resource->length, &length) ||
                length != resource->length || !cmap_program(map, decoded, length, -1))
                goto done;
        }
        ok = 1;
        goto done;
    }
    int present = cupidimage_pdf_optional_value(doc, size, view, "UseCMap", &base);
    if (present < 0)
        goto done;
    if (present) {
        pdf_object_id object = 0;
        if (cupidimage_pdf_page_value(view, "UseCMap", &p))
            cupidimage_pdf_page_reference(view, p, &object);
        for (unsigned i = 0; object && i < depth; i++)
            if (chain[i] == object)
                goto done;
        chain[depth] = object;
        if (!cmap_load(doc, size, &base, &map->base, chain, depth + 1, decoded_bytes))
            goto done;
        map->space_count = map->base->space_count;
        memcpy(map->spaces, map->base->spaces, map->space_count * sizeof(*map->spaces));
    }
    int dict_mode = -1;
    pdf_object_view mode = {0};
    present = cupidimage_pdf_optional_value(doc, size, view, "WMode", &mode);
    if (present < 0)
        goto done;
    if (present) {
        size_t at = 0;
        cmap_token token = cmap_lex(mode.data, mode.len, &at, 0);
        cupidimage_pdf_object_view_release(&mode);
        if (token.type != CM_NUMBER || (token.number != 0 && token.number != 1))
            goto done;
        dict_mode = (int)token.number;
    }
    if (cupidimage_pdf_page_value(view, "CMapName", &p)) {
        cmap_token name = cmap_lex(view->data, view->len, &p, 0);
        if (name.type != CM_NAME)
            goto done;
        memcpy(map->name, name.text, sizeof(map->name));
    }
    const unsigned char *stream;
    if (!cupidimage_pdf_object_stream(doc, size, view, &stream, &length) ||
        !cupidimage_pdf_decode_stream(view, stream, length, &decoded, &length) ||
        length > 4u * 1024u * 1024u - *decoded_bytes)
        goto done;
    *decoded_bytes += length;
    ok = cmap_program(map, decoded, length, dict_mode);
done:
    free(decoded);
    cupidimage_pdf_object_view_release(&base);
    if (ok) {
        *result = map;
        return 1;
    }
    cupidimage_pdf_cmap_free(map);
    return 0;
}

int cupidimage_pdf_cmap_load(const unsigned char *doc, size_t size, const pdf_object_view *view,
                             pdf_cmap **result) {
    pdf_object_id chain[5] = {0};
    size_t decoded = 0;
    *result = NULL;
    return cmap_load(doc, size, view, result, chain, 0, &decoded);
}

static int cmap_lookup(const pdf_cmap *map, uint32_t source, unsigned length, int notdef,
                       unsigned *cid, size_t *work) {
    for (; map; map = map->base) {
        if (map->sorted) {
            unsigned lo = 0, hi = map->count, category = length * 2 + (unsigned)notdef;
            while (lo < hi) {
                if (!*work)
                    return -1;
                (*work)--;
                unsigned mid = lo + (hi - lo) / 2;
                cmap_range *r = map->ranges + mid;
                unsigned key = r->length * 2 + r->notdef;
                if (key < category || (key == category && r->low <= source))
                    lo = mid + 1;
                else
                    hi = mid;
            }
            if (lo) {
                const cmap_range *r = map->ranges + lo - 1;
                if (r->length == length && r->notdef == notdef && source >= r->low &&
                    source <= r->high) {
                    *cid = r->cid + (notdef ? 0 : source - r->low);
                    return 1;
                }
            }
        } else
            for (unsigned i = map->count; i; i--) {
                if (!*work)
                    return -1;
                (*work)--;
                const cmap_range *r = map->ranges + i - 1;
                if (r->length == length && r->notdef == notdef && source >= r->low &&
                    source <= r->high) {
                    *cid = r->cid + (notdef ? 0 : source - r->low);
                    return 1;
                }
            }
    }
    return 0;
}

int cupidimage_pdf_cmap_next(const pdf_cmap *map, const unsigned char *text, size_t length,
                             size_t *used, uint32_t *source, unsigned *cid, unsigned *notdef,
                             int *valid, size_t *work) {
    if (!length)
        return 0;
    *source = 0;
    *cid = *notdef = 0;
    *valid = 0;
    if (!map) {
        *used = length >= 2 ? 2 : 1;
        for (size_t i = 0; i < *used; i++)
            *source = (*source << 8) | text[i];
        if (*used == 2) {
            *cid = *source;
            *valid = 1;
        }
        return 1;
    }
    unsigned chosen = 4, best = 0;
    int any = 0;
    for (unsigned i = 0; i < map->space_count; i++) {
        if (!*work)
            return 0;
        (*work)--;
        const cmap_range *r = map->spaces + i;
        unsigned matching = 0;
        while (matching < r->length && matching < length) {
            unsigned shift = (r->length - matching - 1) * 8, value = text[matching];
            if (value < ((r->low >> shift) & 255) || value > ((r->high >> shift) & 255))
                break;
            matching++;
        }
        if (matching == r->length) {
            chosen = r->length;
            *valid = 1;
            break;
        }
        if (!any || matching > best || (matching == best && r->length < chosen)) {
            chosen = r->length;
            best = matching;
            any = 1;
        }
    }
    *used = chosen < length ? chosen : length;
    for (size_t i = 0; i < *used; i++)
        *source = (*source << 8) | text[i];
    if (!*valid)
        return 1;
    int found = cmap_lookup(map, *source, (unsigned)*used, 0, cid, work),
        fallback = cmap_lookup(map, *source, (unsigned)*used, 1, notdef, work);
    if (found < 0 || fallback < 0)
        return 0;
    if (!found)
        *cid = *notdef;
    return 1;
}
