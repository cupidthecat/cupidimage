#include "type2.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const pdf_cff_font *font;
    const pdf_cff_private *private_dict;
    pdf_cff_outline *outline;
    pdf_ps_hint_program *hint_program;
    unsigned vertical_stems, mask_seen;
    double stack[48], transient[32], x, y, start_x, start_y;
    uint32_t initialized, random;
    unsigned count, hints, work;
    int width_seen, open;
} type2_state;

static int t2_capture_hints(type2_state *st, unsigned op, const unsigned char *mask) {
    pdf_ps_hint_program *hints = st->hint_program;
    if (!hints)
        return 1;
    unsigned vertical = op != 1 && op != 18;
    if (st->count && (st->outline->count || st->mask_seen))
        hints->diagnostics |= PDF_PS_HINT_LATE_STEMS;
    if (st->count && !vertical && st->vertical_stems)
        hints->diagnostics |= PDF_PS_HINT_STEM_ORDER;
    if (st->count && vertical)
        st->vertical_stems = 1;
    double end = 0;
    for (unsigned i = 0; i < st->count; i += 2) {
        double start = end + st->stack[i], width = st->stack[i + 1];
        end = start + width;
        if (!isfinite(start) || !isfinite(end) || fabs(start) > 1e30 || fabs(end) > 1e30)
            return 0;
        if (width < 0 && width != -20 && width != -21)
            hints->diagnostics |= PDF_PS_HINT_BAD_WIDTH;
        hints->stems[hints->stem_count++] = (pdf_ps_stem){start, width, vertical};
    }
    if (op != 19 && op != 20)
        return 1;
    st->mask_seen = 1;
    if (!hints->stem_count)
        hints->diagnostics |= PDF_PS_HINT_EMPTY_MASK;
    unsigned bytes = (hints->stem_count + 7) / 8, tail = hints->stem_count % 8;
    if (tail && (mask[bytes - 1] & ((1u << (8 - tail)) - 1)))
        hints->diagnostics |= PDF_PS_HINT_MASK_PADDING;
    if (hints->event_count == 65536)
        return 0;
    if (hints->event_count == hints->event_capacity) {
        unsigned capacity = hints->event_capacity ? hints->event_capacity * 2 : 16;
        pdf_ps_hint_event *events = realloc(hints->events, (size_t)capacity * sizeof(*events));
        if (!events)
            return 0;
        hints->events = events;
        hints->event_capacity = capacity;
    }
    pdf_ps_hint_event *event = hints->events + hints->event_count++;
    memset(event, 0, sizeof(*event));
    event->segment = st->outline->count;
    event->stem_count = hints->stem_count;
    event->counter = op == 20;
    if (bytes)
        memcpy(event->mask, mask, bytes);
    return 1;
}

static int t2_integer(double x, int *n) {
    if (!isfinite(x) || x < -2147483648.0 || x > 2147483647.0 || floor(x) != x)
        return 0;
    *n = (int)x;
    return 1;
}

static int t2_push(type2_state *st, double x) {
    if (st->count == 48 || !isfinite(x) || fabs(x) > 1e30)
        return 0;
    st->stack[st->count++] = x;
    return 1;
}

static int t2_emit(type2_state *st, unsigned kind, const double points[6]) {
    pdf_cff_outline *out = st->outline;
    if (out->count == 65536)
        return 0;
    if (out->count == out->capacity) {
        unsigned cap = out->capacity ? out->capacity * 2 : 64;
        pdf_cff_segment *next = realloc(out->segments, (size_t)cap * sizeof(*next));
        if (!next)
            return 0;
        out->segments = next;
        out->capacity = cap;
    }
    for (unsigned i = 0; i < 6; i++)
        if (!isfinite(points[i]) || fabs(points[i]) > 1e30)
            return 0;
    pdf_cff_segment *seg = out->segments + out->count++;
    seg->kind = kind;
    memcpy(seg->points, points, sizeof(seg->points));
    return 1;
}

static int t2_close(type2_state *st) {
    if (!st->open)
        return 1;
    double p[6] = {st->start_x, st->start_y};
    st->open = 0;
    return t2_emit(st, PDF_CFF_CLOSE, p);
}

static int t2_move(type2_state *st, double x, double y) {
    if (!t2_close(st))
        return 0;
    st->x += x;
    st->y += y;
    st->start_x = st->x;
    st->start_y = st->y;
    st->open = 1;
    double p[6] = {st->x, st->y};
    return t2_emit(st, PDF_CFF_MOVE, p);
}

static int t2_line(type2_state *st, double x, double y) {
    if (!st->open)
        return 0;
    st->x += x;
    st->y += y;
    double p[6] = {st->x, st->y};
    return t2_emit(st, PDF_CFF_LINE, p);
}

static int t2_curve(type2_state *st, const double d[6]) {
    if (!st->open)
        return 0;
    double p[6];
    for (unsigned i = 0; i < 3; i++) {
        st->x += d[i * 2];
        st->y += d[i * 2 + 1];
        p[i * 2] = st->x;
        p[i * 2 + 1] = st->y;
    }
    return t2_emit(st, PDF_CFF_CURVE, p);
}

static int t2_width(type2_state *st, int present) {
    if (st->width_seen)
        return !present;
    st->width_seen = 1;
    st->outline->width = st->private_dict->default_width;
    if (present) {
        if (!st->count)
            return 0;
        st->outline->width = st->private_dict->nominal_width + st->stack[0];
        memmove(st->stack, st->stack + 1, --st->count * sizeof(*st->stack));
    }
    return isfinite(st->outline->width) && fabs(st->outline->width) <= 1e30;
}

static int t2_escape(type2_state *st, unsigned op) {
    double *s = st->stack;
    unsigned n = st->count;
    if (op == 0)
        return st->count == 0;
    if (op == 23) {
        st->random = st->random * 1664525u + 1013904223u;
        return t2_push(st, ((double)st->random + 1) / 4294967296.0);
    }
    if (op == 34 || op == 35 || op == 36 || op == 37) {
        double d[12] = {0};
        if (op == 34 && n == 7) {
            d[0] = s[0];
            d[2] = s[1];
            d[3] = s[2];
            d[4] = s[3];
            d[6] = s[4];
            d[8] = s[5];
            d[9] = -s[2];
            d[10] = s[6];
        } else if (op == 35 && n == 13)
            memcpy(d, s, 12 * sizeof(*d));
        else if (op == 36 && n == 9) {
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = s[3];
            d[4] = s[4];
            d[6] = s[5];
            d[8] = s[6];
            d[9] = s[7];
            d[10] = s[8];
            d[11] = -s[1] - s[3] - s[7];
        } else if (op == 37 && n == 11) {
            memcpy(d, s, 10 * sizeof(*d));
            double dx = s[0] + s[2] + s[4] + s[6] + s[8], dy = s[1] + s[3] + s[5] + s[7] + s[9];
            d[10] = fabs(dx) > fabs(dy) ? s[10] : -dx;
            d[11] = fabs(dx) > fabs(dy) ? -dy : s[10];
        } else
            return 0;
        st->count = 0;
        return t2_curve(st, d) && t2_curve(st, d + 6);
    }
    unsigned need = op == 22 ? 4
                    : op == 3 || op == 4 || op == 10 || op == 11 || op == 12 || op == 15 ||
                            op == 20 || op == 24 || op == 28 || op == 30
                        ? 2
                        : 1;
    if (n < need)
        return 0;
    double a = s[n - 1], b = need >= 2 ? s[n - 2] : 0, result = 0;
    int i, j;
    switch (op) {
        case 3:
            result = b != 0 && a != 0;
            break;
        case 4:
            result = b != 0 || a != 0;
            break;
        case 5:
            result = a == 0;
            break;
        case 9:
            result = fabs(a);
            break;
        case 10:
            result = b + a;
            break;
        case 11:
            result = b - a;
            break;
        case 12:
            if (!a)
                return 0;
            result = b / a;
            break;
        case 14:
            result = -a;
            break;
        case 15:
            result = b == a;
            break;
        case 18:
            st->count--;
            return 1;
        case 20:
            if (!t2_integer(a, &i) || i < 0 || i >= 32)
                return 0;
            st->transient[i] = b;
            st->initialized |= (uint32_t)1 << i;
            st->count -= 2;
            return 1;
        case 21:
            if (!t2_integer(a, &i) || i < 0 || i >= 32 || !(st->initialized & ((uint32_t)1 << i)))
                return 0;
            result = st->transient[i];
            break;
        case 22:
            result = s[n - 2] <= s[n - 1] ? s[n - 4] : s[n - 3];
            break;
        case 24:
            result = b * a;
            break;
        case 26:
            if (a < 0)
                return 0;
            result = sqrt(a);
            break;
        case 27:
            return t2_push(st, a);
        case 28:
            s[n - 1] = b;
            s[n - 2] = a;
            return 1;
        case 29:
            if (!t2_integer(a, &i))
                return 0;
            if (i < 0)
                i = 0;
            if ((unsigned)i >= n - 1)
                return 0;
            result = s[n - 2 - (unsigned)i];
            break;
        case 30: {
            if (!t2_integer(b, &i) || !t2_integer(a, &j) || i < 0 || (unsigned)i > n - 2)
                return 0;
            st->count -= 2;
            if (!i)
                return 1;
            j %= i;
            if (j < 0)
                j += i;
            double tmp[48];
            for (unsigned k = 0; k < (unsigned)i; k++)
                tmp[(k + (unsigned)j) % (unsigned)i] = s[n - 2 - (unsigned)i + k];
            memcpy(s + n - 2 - (unsigned)i, tmp, (unsigned)i * sizeof(*s));
            return 1;
        }
        default:
            return 0;
    }
    st->count -= need;
    return t2_push(st, result);
}

static int t2_run(type2_state *st, pdf_cff_slice code, unsigned depth) {
    if (depth > 10 || code.length > 65535)
        return 0;
    size_t pos = 0;
    while (pos < code.length) {
        if (!st->work)
            return 0;
        st->work--;
        unsigned op = code.data[pos++];
        if (op >= 32 || op == 28) {
            double value;
            if (op <= 246 && op != 28)
                value = (int)op - 139;
            else if (op >= 247 && op <= 254) {
                if (pos == code.length)
                    return 0;
                unsigned b = code.data[pos++];
                value = op <= 250 ? (double)((op - 247) * 256 + b + 108)
                                  : -(double)((op - 251) * 256 + b + 108);
            } else {
                unsigned bytes = op == 28 ? 2 : 4;
                if (code.length - pos < bytes)
                    return 0;
                uint32_t raw = 0;
                for (unsigned i = 0; i < bytes; i++)
                    raw = raw * 256 + code.data[pos++];
                value = (double)raw - ((raw & (bytes == 2 ? 0x8000u : 0x80000000u))
                                           ? (bytes == 2 ? 65536.0 : 4294967296.0)
                                           : 0);
                if (bytes == 4)
                    value /= 65536;
            }
            if (!t2_push(st, value))
                return 0;
            continue;
        }
        if (op == 12) {
            if (pos == code.length || !t2_escape(st, code.data[pos++]))
                return 0;
            continue;
        }
        if (op == 10 || op == 29) {
            if (!st->count)
                return 0;
            int number;
            if (!t2_integer(st->stack[--st->count], &number))
                return 0;
            const pdf_cff_index *subrs = op == 10 ? &st->private_dict->subrs : &st->font->globals;
            int bias = subrs->count < 1240 ? 107 : subrs->count < 33900 ? 1131 : 32768;
            int64_t target = (int64_t)number + bias;
            pdf_cff_slice sub;
            if (target < 0 || target >= subrs->count ||
                !cupidimage_pdf_cff_item(subrs, (unsigned)target, &sub))
                return 0;
            int result = t2_run(st, sub, depth + 1);
            if (result != 1)
                return result;
            continue;
        }
        if (op == 11)
            return depth ? 1 : 0;
        if (op == 1 || op == 3 || op == 18 || op == 23 || op == 19 || op == 20) {
            if (!t2_width(st, st->count % 2))
                return 0;
            st->hints += st->count / 2;
            if (st->hints > 96)
                return 0;
            size_t mask = op == 19 || op == 20 ? (st->hints + 7) / 8 : 0;
            if (mask > code.length - pos || !t2_capture_hints(st, op, code.data + pos))
                return 0;
            st->count = 0;
            pos += mask;
            continue;
        }
        double *s = st->stack;
        unsigned n = st->count;
        if (op == 14) {
            if (!t2_width(st, n == 1 || n == 5))
                return 0;
            n = st->count;
            if (n == 4) {
                int base, accent;
                if (st->font->top.cid || !t2_integer(s[2], &base) || !t2_integer(s[3], &accent) ||
                    base < 0 || base > 255 || accent < 0 || accent > 255 || st->outline->count)
                    return 0;
                st->outline->composite = 1;
                st->outline->base = (unsigned)base;
                st->outline->accent = (unsigned)accent;
                st->outline->accent_x = s[0];
                st->outline->accent_y = s[1];
            } else if (n)
                return 0;
            st->count = 0;
            return t2_close(st) ? 2 : 0;
        }
        if (op == 4 || op == 21 || op == 22) {
            unsigned args = op == 21 ? 2 : 1;
            if (!t2_width(st, n == args + 1) || st->count != args)
                return 0;
            if (!t2_move(st, op == 4 ? 0 : s[0], op == 22 ? 0 : s[op == 21 ? 1 : 0]))
                return 0;
        } else if (op == 5) {
            if (!n || n % 2)
                return 0;
            for (unsigned i = 0; i < n; i += 2)
                if (!t2_line(st, s[i], s[i + 1]))
                    return 0;
        } else if (op == 6 || op == 7) {
            if (!n)
                return 0;
            for (unsigned i = 0; i < n; i++) {
                int vertical = (op == 7) ^ (i % 2);
                if (!t2_line(st, vertical ? 0 : s[i], vertical ? s[i] : 0))
                    return 0;
            }
        } else if (op == 8 || op == 24 || op == 25) {
            unsigned curves = op == 25 ? 6 : n - (op == 24 ? 2 : 0), lines = op == 25 ? n - 6 : 0;
            if ((op == 8 && (n < 6 || n % 6)) || (op == 24 && (n < 8 || (n - 2) % 6)) ||
                (op == 25 && (n < 8 || (n - 6) % 2)))
                return 0;
            for (unsigned i = 0; i < lines; i += 2)
                if (!t2_line(st, s[i], s[i + 1]))
                    return 0;
            for (unsigned i = lines; i < lines + curves; i += 6)
                if (!t2_curve(st, s + i))
                    return 0;
            if (op == 24 && !t2_line(st, s[n - 2], s[n - 1]))
                return 0;
        } else if (op == 26 || op == 27 || op == 30 || op == 31) {
            if (n < 4 || (n % 4 != 0 && n % 4 != 1))
                return 0;
            unsigned i = 0;
            double extra = 0;
            if ((op == 26 || op == 27) && n % 4)
                extra = s[i++];
            int vertical = op == 26 || op == 30;
            while (i < n) {
                double d[6] = {0};
                if (n - i < 4)
                    return 0;
                if (vertical) {
                    d[0] = extra;
                    d[1] = s[i];
                    d[2] = s[i + 1];
                    d[3] = s[i + 2];
                    d[4] = (op == 26 ? 0 : s[i + 3]);
                    d[5] = (op == 26 ? s[i + 3] : 0);
                } else {
                    d[0] = s[i];
                    d[1] = extra;
                    d[2] = s[i + 1];
                    d[3] = s[i + 2];
                    d[4] = (op == 27 ? s[i + 3] : 0);
                    d[5] = (op == 27 ? 0 : s[i + 3]);
                }
                i += 4;
                extra = 0;
                if ((op == 30 || op == 31) && n - i == 1) {
                    d[vertical ? 5 : 4] = s[i++];
                }
                if (!t2_curve(st, d))
                    return 0;
                if (op == 30 || op == 31)
                    vertical = !vertical;
            }
        } else
            return 0;
        st->count = 0;
    }
    return 0;
}

void cupidimage_pdf_type2_free(pdf_cff_outline *outline) {
    free(outline->segments);
    memset(outline, 0, sizeof(*outline));
}

int cupidimage_pdf_type2_outline_budget(const pdf_cff_font *font, unsigned glyph,
                                        pdf_cff_outline *outline, pdf_ps_hint_program *hints,
                                        size_t *work_left) {
    if (!outline)
        return 0;
    memset(outline, 0, sizeof(*outline));
    if (hints)
        memset(hints, 0, sizeof(*hints));
    pdf_cff_slice code;
    if (!font || !work_left || !*work_left || !cupidimage_pdf_cff_item(&font->chars, glyph, &code))
        return 0;
    unsigned fd = font->fdselect ? font->fdselect[glyph] : 0;
    if (fd >= font->private_count)
        return 0;
    type2_state st = {0};
    st.font = font;
    st.private_dict = font->private_dicts + fd;
    st.outline = outline;
    st.hint_program = hints;
    st.random = st.private_dict->random_seed;
    unsigned admitted = *work_left < 65536 ? (unsigned)*work_left : 65536;
    st.work = admitted;
    int result = t2_run(&st, code, 0);
    *work_left -= admitted - st.work;
    if (result == 2)
        return 1;
    cupidimage_pdf_type2_free(outline);
    if (hints)
        cupidimage_pdf_ps_hint_program_free(hints);
    return 0;
}

int cupidimage_pdf_type2_outline_hints(const pdf_cff_font *font, unsigned glyph,
                                       pdf_cff_outline *outline, pdf_ps_hint_program *hints) {
    size_t work = 65536;
    return cupidimage_pdf_type2_outline_budget(font, glyph, outline, hints, &work);
}

int cupidimage_pdf_type2_outline(const pdf_cff_font *font, unsigned glyph,
                                 pdf_cff_outline *outline) {
    return cupidimage_pdf_type2_outline_hints(font, glyph, outline, NULL);
}

static int t2_composite(const pdf_cff_font *font, unsigned glyph, pdf_cff_outline *out,
                        unsigned depth, unsigned *calls) {
    memset(out, 0, sizeof(*out));
    if (depth == 8 || !*calls)
        return 0;
    (*calls)--;
    if (!cupidimage_pdf_type2_outline(font, glyph, out))
        return 0;
    if (!out->composite)
        return 1;
    if (depth) {
        cupidimage_pdf_type2_free(out);
        return 0;
    }
    pdf_cff_outline base = {0}, accent = {0};
    unsigned b = cupidimage_pdf_cff_standard_glyph(font, out->base),
             a = cupidimage_pdf_cff_standard_glyph(font, out->accent);
    int ok = 0;
    if (b == glyph || a == glyph || !t2_composite(font, b, &base, depth + 1, calls) ||
        !t2_composite(font, a, &accent, depth + 1, calls) || base.count > 65536 - accent.count)
        goto done;
    unsigned count = base.count + accent.count;
    if (count) {
        out->segments = malloc((size_t)count * sizeof(*out->segments));
        if (!out->segments)
            goto done;
        if (base.count)
            memcpy(out->segments, base.segments, (size_t)base.count * sizeof(*out->segments));
        if (accent.count)
            memcpy(out->segments + base.count, accent.segments,
                   (size_t)accent.count * sizeof(*out->segments));
        for (unsigned i = base.count; i < count; i++) {
            pdf_cff_segment *s = out->segments + i;
            unsigned n = s->kind == PDF_CFF_CURVE ? 3 : 1;
            for (unsigned j = 0; j < n; j++) {
                s->points[j * 2] += out->accent_x;
                s->points[j * 2 + 1] += out->accent_y;
                if (!isfinite(s->points[j * 2]) || !isfinite(s->points[j * 2 + 1]))
                    goto done;
            }
        }
    }
    out->count = out->capacity = count;
    out->composite = 0;
    ok = 1;
done:
    cupidimage_pdf_type2_free(&base);
    cupidimage_pdf_type2_free(&accent);
    if (!ok)
        cupidimage_pdf_type2_free(out);
    return ok;
}

int cupidimage_pdf_cff_glyph_outline(const pdf_cff_font *font, unsigned glyph,
                                     pdf_cff_outline *outline) {
    unsigned calls = 64;
    return t2_composite(font, glyph, outline, 0, &calls);
}
