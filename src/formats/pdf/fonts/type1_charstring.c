#include "encoding.h"
#include "type1.h"
#include "type1/counter.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const pdf_type1_font *font;
    pdf_cff_outline *outline;
    pdf_type1_hint_trace *trace;
    size_t *work_left;
    double stack[24], other[24], flex_points[7][2], flex_start[2];
    double x, y, start_x, start_y, sbx, sby;
    unsigned count, other_count, flex_count, work;
    int started, open, flex, counter_closed;
    pdf_type1_counter counter;
} t1_state;

static int t1_cs_hint(t1_state *st, unsigned kind, unsigned vertical, const double *values,
                      unsigned count) {
    if (!st->trace)
        return 1;
    pdf_type1_hint_event event = {0};
    event.kind = kind;
    event.segment = st->outline->count;
    event.vertical = vertical;
    for (unsigned i = 0; i < count; i++) {
        double value = values[i];
        if ((kind == PDF_T1_HINT_STEM || kind == PDF_T1_HINT_STEM3) && !(i % 2))
            value += vertical ? st->sbx : st->sby;
        if (!isfinite(value) || fabs(value) > 1e30)
            return 0;
        event.values[i] = value;
    }
    if (kind == PDF_T1_HINT_STEM || kind == PDF_T1_HINT_STEM3)
        for (unsigned i = 1; i < count; i += 2)
            if (values[i] < 0 && (vertical || (values[i] != -20 && values[i] != -21)))
                st->trace->diagnostics |= PDF_T1_HINT_BAD_WIDTH;
    return cupidimage_pdf_type1_trace_append(st->trace, &event);
}

static int t1_cs_int(double n, int low, int high, int *out) {
    if (!isfinite(n) || n < low || n > high || floor(n) != n)
        return 0;
    *out = (int)n;
    return 1;
}

static int t1_cs_push(t1_state *st, double value) {
    if (st->count == 24 || !isfinite(value) || fabs(value) > 1e30)
        return 0;
    st->stack[st->count++] = value;
    return 1;
}

static int t1_cs_emit(t1_state *st, unsigned kind, const double points[6]) {
    pdf_cff_outline *out = st->outline;
    if (out->count == 65536)
        return 0;
    for (unsigned i = 0; i < 6; i++)
        if (!isfinite(points[i]) || fabs(points[i]) > 1e30)
            return 0;
    if (out->count == out->capacity) {
        unsigned capacity = out->capacity ? out->capacity * 2 : 64;
        pdf_cff_segment *next = realloc(out->segments, (size_t)capacity * sizeof(*next));
        if (!next)
            return 0;
        out->segments = next;
        out->capacity = capacity;
    }
    pdf_cff_segment *segment = out->segments + out->count++;
    segment->kind = kind;
    memcpy(segment->points, points, sizeof(segment->points));
    return 1;
}

static int t1_cs_close(t1_state *st, int explicit) {
    if (!st->open)
        return 1;
    double p[6] = {st->start_x, st->start_y};
    st->open = 0;
    return t1_cs_emit(st, explicit ? PDF_CFF_CLOSE : PDF_CFF_END, p);
}

static int t1_cs_move(t1_state *st, double x, double y) {
    if (!st->started)
        return 0;
    if (!st->flex && !t1_cs_close(st, 0))
        return 0;
    st->x += x;
    st->y += y;
    if (!isfinite(st->x) || !isfinite(st->y) || fabs(st->x) > 1e30 || fabs(st->y) > 1e30)
        return 0;
    if (st->flex)
        return 1;
    st->open = 1;
    st->start_x = st->x;
    st->start_y = st->y;
    double p[6] = {st->x, st->y};
    return t1_cs_emit(st, PDF_CFF_MOVE, p);
}

static int t1_cs_line(t1_state *st, double x, double y) {
    if (!st->open || st->flex)
        return 0;
    st->x += x;
    st->y += y;
    double p[6] = {st->x, st->y};
    return t1_cs_emit(st, PDF_CFF_LINE, p);
}

static int t1_cs_curve(t1_state *st, const double delta[6]) {
    if (!st->open || st->flex)
        return 0;
    double p[6];
    for (unsigned i = 0; i < 3; i++) {
        st->x += delta[i * 2];
        st->y += delta[i * 2 + 1];
        p[i * 2] = st->x;
        p[i * 2 + 1] = st->y;
    }
    return t1_cs_emit(st, PDF_CFF_CURVE, p);
}

static int t1_other(t1_state *st) {
    if (st->count < 2 || st->other_count)
        return 0;
    int id, args;
    if (!t1_cs_int(st->stack[st->count - 1], 0, 13, &id) ||
        !t1_cs_int(st->stack[st->count - 2], 0, 22, &args) || (unsigned)args > st->count - 2)
        return 0;
    double *values = st->stack + st->count - 2 - (unsigned)args;
    st->count -= 2 + (unsigned)args;
    if (id == 12 || id == 13) {
        if (st->count || st->counter_closed)
            return 0;
        size_t quota = *st->work_left < st->work ? *st->work_left : st->work;
        size_t before = quota;
        int ok = cupidimage_pdf_type1_counter_push(&st->counter, values, (unsigned)args, id == 13,
                                                   st->trace, st->outline->count, &quota);
        size_t spent = before - quota;
        *st->work_left -= spent;
        st->work -= (unsigned)spent;
        return ok;
    }
    if (id > 3 || st->counter.active)
        return 0;
    st->counter_closed = 1;
    if (id == 1) {
        if (args || st->flex || !st->open)
            return 0;
        st->flex = 1;
        st->flex_count = 0;
        st->flex_start[0] = st->x;
        st->flex_start[1] = st->y;
    } else if (id == 2) {
        if (args || !st->flex || st->flex_count == 7)
            return 0;
        st->flex_points[st->flex_count][0] = st->x;
        st->flex_points[st->flex_count++][1] = st->y;
    } else if (id == 0) {
        if (args != 3 || !st->flex || st->flex_count != 7)
            return 0;
        if (!t1_cs_hint(st, PDF_T1_HINT_FLEX, 0, values, 3))
            return 0;
        st->flex = 0;
        st->x = st->flex_start[0];
        st->y = st->flex_start[1];
        for (unsigned curve = 0; curve < 2; curve++) {
            double d[6], x = st->x, y = st->y;
            for (unsigned i = 0; i < 3; i++) {
                unsigned point = 1 + curve * 3 + i;
                d[i * 2] = st->flex_points[point][0] - x;
                d[i * 2 + 1] = st->flex_points[point][1] - y;
                x = st->flex_points[point][0];
                y = st->flex_points[point][1];
            }
            if (!t1_cs_curve(st, d))
                return 0;
        }
        st->other[0] = values[2];
        st->other[1] = values[1];
        st->other_count = 2;
    } else {
        if (args != 1 || !t1_cs_hint(st, PDF_T1_HINT_REPLACE, 0, values, 1))
            return 0;
        st->other[0] = values[0];
        st->other_count = 1;
    }
    return 1;
}

static int t1_cs_run(t1_state *st, pdf_cff_slice code, unsigned depth) {
    if (depth > 10 || code.length > 65535 || !code.data)
        return 0;
    size_t pos = 0;
    while (pos < code.length) {
        if (!st->work || !*st->work_left)
            return 0;
        st->work--;
        (*st->work_left)--;
        unsigned op = code.data[pos++];
        if (op >= 32) {
            double value;
            if (op <= 246)
                value = (int)op - 139;
            else if (op < 255) {
                if (pos == code.length)
                    return 0;
                unsigned b = code.data[pos++];
                value = op <= 250 ? (double)((op - 247) * 256 + b + 108)
                                  : -(double)((op - 251) * 256 + b + 108);
            } else {
                if (code.length - pos < 4)
                    return 0;
                uint32_t raw = 0;
                for (unsigned i = 0; i < 4; i++)
                    raw = raw * 256 + code.data[pos++];
                value = (double)raw - ((raw & 0x80000000u) ? 4294967296.0 : 0);
            }
            if (!t1_cs_push(st, value))
                return 0;
            continue;
        }
        if (op == 12) {
            if (pos == code.length)
                return 0;
            op = 256 + code.data[pos++];
        }
        if (op == 10) {
            int subr;
            if (!st->count || !t1_cs_int(st->stack[--st->count], 0, 65535, &subr) ||
                (unsigned)subr >= st->font->subr_count)
                return 0;
            int result = t1_cs_run(st, st->font->subrs[subr], depth + 1);
            if (result != 1)
                return result;
            continue;
        }
        if (op == 11)
            return depth ? 1 : 0;
        if (op == 268) {
            if (st->count < 2 || st->stack[st->count - 1] == 0)
                return 0;
            double b = st->stack[--st->count], a = st->stack[--st->count];
            if (!t1_cs_push(st, a / b))
                return 0;
            continue;
        }
        if (op == 272) {
            if (!st->started || !t1_other(st))
                return 0;
            continue;
        }
        if (op == 273) {
            if (!st->other_count || !t1_cs_push(st, st->other[--st->other_count]))
                return 0;
            continue;
        }
        double *s = st->stack;
        unsigned n = st->count;
        if (op == 13 || op == 263) {
            if (st->started || n != (op == 13 ? 2u : 4u))
                return 0;
            st->started = 1;
            st->x = st->sbx = s[0];
            st->y = st->sby = op == 13 ? 0 : s[1];
            st->outline->width = s[op == 13 ? 1 : 2];
            st->outline->width_y = op == 13 ? 0 : s[3];
        } else {
            if (!st->started || st->counter.active)
                return 0;
            st->counter_closed = 1;
            if (op == 14) {
                if (n || st->flex || st->other_count)
                    return 0;
                return t1_cs_close(st, 0) ? 2 : 0;
            } else if (op == 262) {
                int base, accent;
                if (n != 5 || st->outline->count || st->flex || !t1_cs_int(s[3], 0, 255, &base) ||
                    !t1_cs_int(s[4], 0, 255, &accent))
                    return 0;
                st->outline->composite = 1;
                st->outline->base = (unsigned)base;
                st->outline->accent = (unsigned)accent;
                st->outline->accent_x = st->sbx + s[1] - s[0];
                st->outline->accent_y = s[2];
                return 2;
            } else if (op == 1 || op == 3) {
                if (n != 2 || !t1_cs_hint(st, PDF_T1_HINT_STEM, op == 3, s, n))
                    return 0;
            } else if (op == 257 || op == 258) {
                if (n != 6 || !t1_cs_hint(st, PDF_T1_HINT_STEM3, op == 257, s, n))
                    return 0;
            } else if (op == 256) {
                if (n || !t1_cs_hint(st, PDF_T1_HINT_DOTSECTION, 0, NULL, 0))
                    return 0;
            } else if (op == 9) {
                if (n || st->flex || !t1_cs_close(st, 1))
                    return 0;
            } else if (op == 21) {
                if (n != 2 || !t1_cs_move(st, s[0], s[1]))
                    return 0;
            } else if (op == 4 || op == 22) {
                if (n != 1 || !t1_cs_move(st, op == 22 ? s[0] : 0, op == 4 ? s[0] : 0))
                    return 0;
            } else if (op == 5) {
                if (n != 2 || !t1_cs_line(st, s[0], s[1]))
                    return 0;
            } else if (op == 6 || op == 7) {
                if (n != 1 || !t1_cs_line(st, op == 6 ? s[0] : 0, op == 7 ? s[0] : 0))
                    return 0;
            } else if (op == 8) {
                if (n != 6 || !t1_cs_curve(st, s))
                    return 0;
            } else if (op == 30 || op == 31) {
                if (n != 4)
                    return 0;
                double d[6] = {op == 31 ? s[0] : 0, op == 30 ? s[0] : 0, s[1], s[2],
                               op == 30 ? s[3] : 0, op == 31 ? s[3] : 0};
                if (!t1_cs_curve(st, d))
                    return 0;
            } else if (op == 289) {
                if (n != 2 || st->flex)
                    return 0;
                if (st->x != s[0] || st->y != s[1]) {
                    st->x = s[0];
                    st->y = s[1];
                    double p[6] = {st->x, st->y};
                    if (!t1_cs_emit(st, PDF_CFF_SETPOINT, p))
                        return 0;
                }
            } else
                return 0;
        }
        st->count = 0;
    }
    return 0;
}

int cupidimage_pdf_type1_outline_budget(const pdf_type1_font *font, unsigned glyph,
                                        pdf_cff_outline *outline, pdf_type1_hint_trace *trace,
                                        size_t *work_left) {
    if (trace)
        memset(trace, 0, sizeof(*trace));
    if (!outline)
        return 0;
    memset(outline, 0, sizeof(*outline));
    if (!font || !font->glyphs || glyph >= font->glyph_count || !work_left)
        return 0;
    t1_state st = {0};
    st.font = font;
    st.outline = outline;
    st.trace = trace;
    st.work_left = work_left;
    st.work = 65536;
    int ok = t1_cs_run(&st, font->glyphs[glyph].code, 0) == 2 && !st.counter.active;
    cupidimage_pdf_type1_counter_free(&st.counter);
    if (ok)
        return 1;
    cupidimage_pdf_type2_free(outline);
    cupidimage_pdf_type1_trace_free(trace);
    return 0;
}

int cupidimage_pdf_type1_outline_hints(const pdf_type1_font *font, unsigned glyph,
                                       pdf_cff_outline *outline, pdf_type1_hint_trace *trace) {
    size_t work = 65536;
    return cupidimage_pdf_type1_outline_budget(font, glyph, outline, trace, &work);
}

static int t1_glyph(const pdf_type1_font *font, unsigned glyph, pdf_cff_outline *outline,
                    unsigned depth) {
    memset(outline, 0, sizeof(*outline));
    if (depth > 1 || !cupidimage_pdf_type1_outline_hints(font, glyph, outline, NULL))
        return 0;
    if (!outline->composite)
        return 1;
    if (depth)
        goto fail;
    unsigned b = cupidimage_pdf_type1_name(
                 font, cupidimage_pdf_encoding_name("StandardEncoding", outline->base)),
             a = cupidimage_pdf_type1_name(
                 font, cupidimage_pdf_encoding_name("StandardEncoding", outline->accent));
    pdf_cff_outline base = {0}, accent = {0};
    int ok = 0;
    if (b == glyph || a == glyph || !t1_glyph(font, b, &base, depth + 1) ||
        !t1_glyph(font, a, &accent, depth + 1) || base.count > 65536 - accent.count)
        goto composite_done;
    unsigned count = base.count + accent.count;
    if (count) {
        outline->segments = malloc((size_t)count * sizeof(*outline->segments));
        if (!outline->segments)
            goto composite_done;
        if (base.count)
            memcpy(outline->segments, base.segments,
                   (size_t)base.count * sizeof(*outline->segments));
        if (accent.count)
            memcpy(outline->segments + base.count, accent.segments,
                   (size_t)accent.count * sizeof(*outline->segments));
        for (unsigned i = base.count; i < count; i++) {
            pdf_cff_segment *segment = outline->segments + i;
            unsigned points = segment->kind == PDF_CFF_CURVE ? 3 : 1;
            for (unsigned j = 0; j < points; j++) {
                segment->points[j * 2] += outline->accent_x;
                segment->points[j * 2 + 1] += outline->accent_y;
                if (!isfinite(segment->points[j * 2]) || !isfinite(segment->points[j * 2 + 1]))
                    goto composite_done;
            }
        }
    }
    outline->count = outline->capacity = count;
    outline->composite = 0;
    ok = 1;
composite_done:
    cupidimage_pdf_type2_free(&base);
    cupidimage_pdf_type2_free(&accent);
    if (ok)
        return 1;
fail:
    cupidimage_pdf_type2_free(outline);
    return 0;
}

int cupidimage_pdf_type1_outline(const pdf_type1_font *font, unsigned glyph,
                                 pdf_cff_outline *outline) {
    return t1_glyph(font, glyph, outline, 0);
}
