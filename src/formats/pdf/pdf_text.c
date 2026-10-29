#include "fonts/cmap.h"
#include "pdf_render_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

void cupidimage_pdf_text_begin(pdf_render_state *st) {
    if (st->in_text) {
        st->image_failed = 1;
        cupidimage_set_err(st->error, st->error_capacity, "pdf: nested text object");
        return;
    }
    memset(st->text_matrix, 0, sizeof(st->text_matrix));
    memset(st->text_line_matrix, 0, sizeof(st->text_line_matrix));
    st->text_matrix[0] = st->text_matrix[3] = 1;
    st->text_line_matrix[0] = st->text_line_matrix[3] = 1;
    st->in_text = 1;
}

static int text_position_valid(pdf_render_state *st, const double *matrix) {
    for (int i = 0; i < 6; i++)
        if (!isfinite(matrix[i]) || fabs(matrix[i]) > 1e100) {
            st->image_failed = 1;
            cupidimage_set_err(st->error, st->error_capacity, "pdf: text transform exceeds limits");
            return 0;
        }
    return 1;
}

void cupidimage_pdf_text_move(pdf_render_state *st, double x, double y) {
    double *m = st->text_line_matrix;
    m[4] += x * m[0] + y * m[2];
    m[5] += x * m[1] + y * m[3];
    if (text_position_valid(st, m))
        memcpy(st->text_matrix, m, sizeof(st->text_matrix));
}

void cupidimage_pdf_text_advance(pdf_render_state *st, double distance) {
    double *m = st->text_matrix;
    int vertical =
        st->gs.current_font && st->gs.current_font->cid && st->gs.current_font->cid->vertical;
    m[4] += distance * m[vertical ? 2 : 0];
    m[5] += distance * m[vertical ? 3 : 1];
    text_position_valid(st, m);
}

/* The fallback glyph remains an 8x8 bitmap. Positioning and text-state
   transforms are applied independently of that temporary glyph representation. */
static void pdf_draw_glyph(pdf_render_state *st, unsigned char c) {
    if (!st->img || !st->img->rgba || st->gs.text_mode == 3)
        return;
    int mode = st->gs.text_mode & 3, clipping = st->gs.text_mode >= 4;
    const uint8_t *glyph = cupidimage_pdf_font8x8_basic[c < 128 ? c : '?'];
    unsigned ink = 0;
    for (int row = 0; row < 8; row++)
        ink |= glyph[row];
    if (!ink)
        return;
    if (clipping && !cupidimage_pdf_text_clip_prepare(st)) {
        st->image_failed = 1;
        return;
    }
    if (st->gs.font_size_pts == 0)
        return;

    const double ratio = 96.0 / 72.0;
    double size = fmax(6, fabs(st->gs.font_size_pts) * ratio);
    double pixel_height = floor(size + .5), step_x = fmax(1, floor(size * .6 / 8 + .5)) / ratio;
    double step_y = fmax(1, floor(size / 8 + .5)) / ratio;
    double sign = st->gs.font_size_pts < 0 ? -1 : 1;
    const double *t = st->text_matrix;
    const float *cmt = st->gs.ctm;
    double m[6] = {cmt[0] * t[0] + cmt[2] * t[1],          cmt[1] * t[0] + cmt[3] * t[1],
                   cmt[0] * t[2] + cmt[2] * t[3],          cmt[1] * t[2] + cmt[3] * t[3],
                   cmt[0] * t[4] + cmt[2] * t[5] + cmt[4], cmt[1] * t[4] + cmt[3] * t[5] + cmt[5]};
    double top = sign * pixel_height / ratio + st->gs.text_rise;
    m[4] += m[2] * top;
    m[5] += m[3] * top;
    m[0] *= sign * st->gs.horizontal_scale * step_x;
    m[1] *= sign * st->gs.horizontal_scale * step_x;
    m[2] *= -sign * step_y;
    m[3] *= -sign * step_y;
    for (int i = 0; i < 6; i++)
        if (!isfinite(m[i])) {
            st->image_failed = 1;
            return;
        }
    double det = m[0] * m[3] - m[1] * m[2];
    if (!isfinite(det)) {
        st->image_failed = 1;
        return;
    }
    if (det == 0)
        return;
    double min_x = st->img->width, max_x = 0, min_y = st->img->height, max_y = 0;
    for (int corner = 0; corner < 4; corner++) {
        double gx = (corner & 1) ? 8 : 0, gy = (corner & 2) ? 8 : 0;
        double x = (m[0] * gx + m[2] * gy + m[4] - st->page_x0_pts) * ratio;
        double y = (st->page_top_pts - m[1] * gx - m[3] * gy - m[5]) * ratio;
        if (!isfinite(x) || !isfinite(y)) {
            st->image_failed = 1;
            return;
        }
        min_x = fmin(min_x, x);
        max_x = fmax(max_x, x);
        min_y = fmin(min_y, y);
        max_y = fmax(max_y, y);
    }
    unsigned char *stroke = NULL;
    if ((mode == 1 || mode == 2) &&
        !cupidimage_pdf_empty_color(&st->gs.stroke_space, st->gs.stroke_pattern)) {
        pdf_path contour = {0};
        static const int directions[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
        static const int ends[4][4] = {{0, 0, 0, 1}, {1, 0, 1, 1}, {0, 0, 1, 0}, {0, 1, 1, 1}};
        for (int gy = 0; gy < 8; gy++)
            for (int gx = 0; gx < 8; gx++) {
                if (!(glyph[gy] & (1u << gx)))
                    continue;
                for (int edge = 0; edge < 4; edge++) {
                    int nx = gx + directions[edge][0], ny = gy + directions[edge][1];
                    if (nx >= 0 && nx < 8 && ny >= 0 && ny < 8 && (glyph[ny] & (1u << nx)))
                        continue;
                    pdf_seg *seg = &contour.segs[contour.seg_count++];
                    double ax = gx + ends[edge][0], ay = gy + ends[edge][1];
                    double bx = gx + ends[edge][2], by = gy + ends[edge][3];
                    double points[4] = {(m[0] * ax + m[2] * ay + m[4] - st->page_x0_pts) * ratio,
                                        (st->page_top_pts - m[1] * ax - m[3] * ay - m[5]) * ratio,
                                        (m[0] * bx + m[2] * by + m[4] - st->page_x0_pts) * ratio,
                                        (st->page_top_pts - m[1] * bx - m[3] * by - m[5]) * ratio};
                    for (int i = 0; i < 4; i++)
                        if (fabs(points[i]) > 1e30) {
                            st->image_failed = 1;
                            return;
                        }
                    *seg = (pdf_seg){.x0 = (float)points[0],
                                     .y0 = (float)points[1],
                                     .x1 = (float)points[2],
                                     .y1 = (float)points[3],
                                     .starts_subpath = 1};
                }
            }
        size_t bytes = ((size_t)st->img->width * st->img->height + 7) / 8;
        stroke = calloc(bytes, 1);
        if (!stroke) {
            st->image_failed = 1;
            return;
        }
        cupidimage_pdf_stroke_segments(st, &contour, stroke);
        double scale =
            fmax(hypot(st->gs.ctm[0], st->gs.ctm[2]), hypot(st->gs.ctm[1], st->gs.ctm[3]));
        double margin = fmax(.5, st->gs.line_width_pts * ratio * .5 * scale) + 2;
        min_x -= margin;
        min_y -= margin;
        max_x += margin;
        max_y += margin;
    }
    int x0 = (int)fmax(0, fmin(st->img->width, floor(min_x)));
    int x1 = (int)fmax(0, fmin(st->img->width, ceil(max_x)));
    int y0 = (int)fmax(0, fmin(st->img->height, floor(min_y)));
    int y1 = (int)fmax(0, fmin(st->img->height, ceil(max_y)));
    size_t area = (size_t)(x1 - x0) * (size_t)(y1 - y0);
    if (area > st->clip_budget->tests_left) {
        free(stroke);
        st->image_failed = 1;
        return;
    }
    st->clip_budget->tests_left -= area;
    if (area && mode != 3 && !cupidimage_pdf_text_group_begin(st)) {
        free(stroke);
        st->image_failed = 1;
        return;
    }
    pdf_paint_group combined = {0};
    if (area && mode == 2 && !cupidimage_pdf_fill_stroke_begin(st, &combined)) {
        free(stroke);
        return;
    }
    for (int y = y0; y < y1 && !st->image_failed; y++)
        for (int x = x0; x < x1 && !st->image_failed; x++) {
            double tx = st->page_x0_pts + (x + .5) / ratio - m[4];
            double ty = st->page_top_pts - (y + .5) / ratio - m[5];
            double gx = (m[3] * tx - m[2] * ty) / det, gy = (m[0] * ty - m[1] * tx) / det;
            int filled =
                gx >= 0 && gx < 8 && gy >= 0 && gy < 8 && (glyph[(int)gy] & (1u << (int)gx));
            size_t pixel = (size_t)y * st->img->width + x;
            unsigned bit = 1u << (pixel % 8);
            if (clipping && filled) {
                int delta = det < 0 ? 1 : -1;
                int32_t value = st->text_winding[pixel];
                if ((delta > 0 && value == INT32_MAX) || (delta < 0 && value == INT32_MIN)) {
                    st->image_failed = 1;
                    goto done;
                }
                value += delta;
                st->text_winding[pixel] = value;
                if (value)
                    st->text_clip->bits[pixel / 8] |= (unsigned char)bit;
                else
                    st->text_clip->bits[pixel / 8] &= (unsigned char)~bit;
            }
            int stroked = stroke && (stroke[pixel / 8] & bit);
            if (filled && (mode == 0 || mode == 2) && (!stroked || combined.surface.image.rgba))
                cupidimage_pdf_paint_pixel(st, x, y, 0, 1);
            if (stroked)
                cupidimage_pdf_paint_pixel(st, x, y, 1, 1);
        }
done:
    if (!cupidimage_pdf_fill_stroke_end(st, &combined, !st->image_failed))
        st->image_failed = 1;
    free(stroke);
}

static double pdf_char_advance_em(unsigned char c) {
    if (c == ' ')
        return 0.278;
    if (c >= '0' && c <= '9')
        return 0.556;
    if (c >= 'A' && c <= 'Z') {
        if (c == 'M' || c == 'W')
            return 0.889;
        if (c == 'I')
            return 0.333;
        return 0.667;
    }
    if (c >= 'a' && c <= 'z') {
        if (c == 'm')
            return 0.833;
        if (c == 'w')
            return 0.722;
        if (c == 'i' || c == 'l')
            return 0.278;
        return 0.5;
    }
    if (c == '.' || c == ',' || c == ':' || c == ';' || c == '!' || c == '\'')
        return 0.278;
    if (c == '(' || c == ')' || c == '[' || c == ']')
        return 0.333;
    if (c == '-')
        return 0.333;
    if (c == '/')
        return 0.278;
    if (c == '?')
        return 0.556;
    return 0.5;
}

static int pdf_font_lookup_codepoint(const pdf_font_map *font, uint32_t src_code, uint8_t src_len,
                                     uint32_t *dst_codepoint) {
    if (!font || !dst_codepoint || src_len == 0) {
        return 0;
    }
    int low = 0, high = font->count;
    while (low < high) {
        int mid = low + (high - low) / 2;
        const pdf_tounicode_entry *entry = &font->entries[mid];
        if (entry->src_len == src_len && entry->src_code == src_code) {
            *dst_codepoint = entry->dst_codepoint;
            return 1;
        }
        if (entry->src_len < src_len || (entry->src_len == src_len && entry->src_code < src_code))
            low = mid + 1;
        else
            high = mid;
    }
    return 0;
}

static uint32_t pdf_decode_next_codepoint(const pdf_render_state *st, const unsigned char *bytes,
                                          size_t len, size_t *pos) {
    if (!bytes || !pos || *pos >= len) {
        return 0;
    }

    const pdf_font_map *font = st ? st->gs.current_font : NULL;
    if (font && font->count > 0) {
        int max_len = font->max_src_len;
        if (max_len > 4)
            max_len = 4;
        for (int take = max_len; take >= 1; take--) {
            if (*pos + (size_t)take > len) {
                continue;
            }
            uint32_t src = 0;
            for (int i = 0; i < take; i++) {
                src = (src << 8u) | bytes[*pos + (size_t)i];
            }
            uint32_t dst = 0;
            if (pdf_font_lookup_codepoint(font, src, (uint8_t)take, &dst)) {
                *pos += (size_t)take;
                return dst;
            }
        }
    }

    uint32_t raw = bytes[*pos];
    (*pos)++;
    return raw;
}

static unsigned char pdf_codepoint_to_ascii(uint32_t cp) {
    if (cp < 128u) {
        return (unsigned char)cp;
    }
    if (cp == 0x2018u || cp == 0x2019u || cp == 0x2032u)
        return '\'';
    if (cp == 0x201Cu || cp == 0x201Du || cp == 0x2033u)
        return '"';
    if (cp == 0x2013u || cp == 0x2014u || cp == 0x2212u)
        return '-';
    if (cp == 0x00A0u)
        return ' ';
    if (cp == 0x2022u)
        return '*';
    return '?';
}

void cupidimage_pdf_render_text_string(pdf_render_state *st, const char *text, size_t length) {
    if (!text || !st->in_text)
        return;
    const unsigned char *bytes = (const unsigned char *)text;
    size_t pos = 0;
    while (pos < length && !st->image_failed) {
        st->paint_serial = ++st->form_budget->paint_serial;
        size_t before = pos;
        const pdf_font_map *font = st->gs.current_font;
        double width, advance_y = 0, origin_x = 0, origin_y = 0;
        uint32_t cp;
        unsigned encoded = bytes[before], glyph = 0;
        if (font && font->cid) {
            size_t used;
            uint32_t source;
            unsigned cid, notdef;
            int valid;
            if (!cupidimage_pdf_cmap_next(font->cid->encoding, bytes + pos, length - pos, &used,
                                          &source, &cid, &notdef, &valid,
                                          &st->form_budget->cmap_steps_left)) {
                st->image_failed = 1;
                cupidimage_set_err(st->error, st->error_capacity, "pdf: CMap work limit exceeded");
                break;
            }
            if (font->program && !cupidimage_pdf_font_program_has_glyph(font->program, cid)) {
                cid = notdef;
                if (!cupidimage_pdf_font_program_has_glyph(font->program, cid))
                    cid = 0;
            }
            encoded = cid;
            pos += used;
            cp = '?';
            if (valid)
                pdf_font_lookup_codepoint(font, source, (uint8_t)used, &cp);
            cupidimage_pdf_cid_metrics(font->cid, cid, &width, &advance_y, &origin_x, &origin_y);
        } else {
            if (font && font->program) {
                cp = bytes[pos++];
                pdf_font_lookup_codepoint(font, encoded, 1, &cp);
            } else
                cp = font && font->type3 ? bytes[pos++]
                                         : pdf_decode_next_codepoint(st, bytes, length, &pos);
            unsigned char draw = pdf_codepoint_to_ascii(cp);
            width = cp < 32 ? .5 : draw == '?' && cp >= 128 ? .5 : pdf_char_advance_em(draw);
            if (font && pos - before == 1) {
                unsigned code = bytes[before];
                if (font->has_widths)
                    width = code >= (unsigned)font->first_char && code <= (unsigned)font->last_char
                                ? font->widths[code]
                                : font->missing_width;
                else if (font->has_missing_width)
                    width = font->missing_width;
            }
        }
        if (font && font->program) {
            glyph = cupidimage_pdf_font_program_glyph(font->program, encoded);
            if (!font->cid && !font->has_widths && !font->has_missing_width) {
                if (!st->form_budget->glyph_calls_left ||
                    !cupidimage_pdf_font_program_advance(
                        font->program, glyph, &st->form_budget->outline_cache_bytes_left, &width)) {
                    st->image_failed = 1;
                    cupidimage_set_err(st->error, st->error_capacity,
                                       "pdf: font advance decoding failed or exceeded limits");
                    break;
                }
                st->form_budget->glyph_calls_left--;
            }
        }
        unsigned char draw = pdf_codepoint_to_ascii(cp);
        if (font && font->type3) {
            int paints = st->gs.text_mode != 3 && st->gs.font_size_pts != 0 &&
                         st->gs.horizontal_scale != 0 && font->type3->glyphs[bytes[before]];
            if ((paints && !cupidimage_pdf_text_group_begin(st)) ||
                !cupidimage_pdf_draw_type3_glyph(st, font, bytes[before]))
                st->image_failed = 1;
        } else if ((font && font->program) || cp >= 32) {
            if (font && font->cid && font->cid->vertical) {
                double saved[6];
                memcpy(saved, st->text_matrix, sizeof(saved));
                double dx = -origin_x * st->gs.font_size_pts * st->gs.horizontal_scale,
                       dy = -origin_y * st->gs.font_size_pts;
                st->text_matrix[4] += dx * saved[0] + dy * saved[2];
                st->text_matrix[5] += dx * saved[1] + dy * saved[3];
                if (font->program) {
                    if (!cupidimage_pdf_draw_font_program_glyph(st, font->program, glyph))
                        st->image_failed = 1;
                } else
                    pdf_draw_glyph(st, draw);
                memcpy(st->text_matrix, saved, sizeof(saved));
            } else if (font && font->program) {
                if (!cupidimage_pdf_draw_font_program_glyph(st, font->program, glyph))
                    st->image_failed = 1;
            } else
                pdf_draw_glyph(st, draw);
        }
        cupidimage_pdf_patterns_end_paint(st);
        double spacing = st->gs.char_space;
        /* Word spacing applies to a single-byte character code 32, not to a
           Unicode mapping or one byte within a multibyte character code. */
        if (pos - before == 1 && bytes[before] == 32)
            spacing += st->gs.word_space;
        if (font && font->cid && font->cid->vertical)
            cupidimage_pdf_text_advance(st, advance_y * st->gs.font_size_pts + spacing);
        else
            cupidimage_pdf_text_advance(st, (width * st->gs.font_size_pts + spacing) *
                                                st->gs.horizontal_scale);
    }
}
