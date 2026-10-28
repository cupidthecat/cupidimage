#include "core/internal.h"
#include "cupidimage.h"
#include "formats/svg/cupidimage_svg_base.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wshadow"
#endif

#include "svg_internal.h"

static int svg_hex_value(int c);
static int svg_base64_value(int c);
static int svg_decode_data_payload(const char *payload, int is_base64, unsigned char **out,
                                   size_t *out_size);
static int svg_parse_repeat_count(const char *s, float *out, int *indefinite);
static int svg_get_animation_progress(const svg_tag *anim, float time_s, float *out_progress);
static int svg_parse_number_list(const char *s, float *vals, int max_vals, int *out_count);
static int svg_parse_number_values_list(const char *s, float *vals, int max_vals, int *out_count);
static int svg_tag_set_attr(svg_tag *tag, const char *name, const char *value);
static void svg_format_number(char *buf, size_t cap, float v);
static int svg_animate_numeric(const svg_tag *anim, const char *attr_name, svg_tag *target,
                               float progress);
static int svg_animate_transform(const svg_tag *anim, svg_tag *target, float progress);
static const svg_css_keyframes *svg_css_find_keyframes(const svg_css *css, const char *name);
static float svg_css_apply_anim_direction(int direction, int cycle, float phase);
static int svg_css_anim_progress(const svg_css_animation_binding *anim, float time_s,
                                 float *out_progress);
static int svg_css_keyframe_range(const svg_css_keyframes *kf, float progress,
                                  const svg_css_keyframe_step **a, const svg_css_keyframe_step **b,
                                  float *u);

int cupidimage_svg_dimension(float value, uint32_t *out) {
    if (!isfinite(value) || value <= 0)
        return 0;
    double rounded = fmax(1, floor((double)value + 0.5));
    if (rounded > UINT32_MAX)
        return 0;
    *out = (uint32_t)rounded;
    return 1;
}

/* Clip floating bounds before converting them to signed raster coordinates. */
int cupidimage_svg_raster_bound(float value, int upper) {
    double rounded = upper ? ceil((double)value) : floor((double)value);
    if (isnan(rounded))
        return 0;
    if (rounded <= INT_MIN)
        return INT_MIN;
    if (rounded >= INT_MAX)
        return INT_MAX;
    return (int)rounded;
}

static int svg_hex_value(int c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int svg_base64_value(int c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

static int svg_decode_data_payload(const char *payload, int is_base64, unsigned char **out,
                                   size_t *out_size) {
    if (!payload || !out || !out_size) {
        return 0;
    }
    *out = NULL;
    *out_size = 0;
    size_t len = strlen(payload);
    if (!is_base64) {
        unsigned char *buf = (unsigned char *)malloc(len + 1u);
        if (!buf) {
            return 0;
        }
        size_t w = 0;
        for (size_t i = 0; i < len; i++) {
            unsigned char c = (unsigned char)payload[i];
            if (c == '%' && i + 2 < len) {
                int hi = svg_hex_value((unsigned char)payload[i + 1]);
                int lo = svg_hex_value((unsigned char)payload[i + 2]);
                if (hi >= 0 && lo >= 0) {
                    c = (unsigned char)((hi << 4) | lo);
                    i += 2;
                }
            }
            buf[w++] = c;
        }
        *out = buf;
        *out_size = w;
        return 1;
    }

    size_t cap = (len / 4u) * 3u + 4u;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) {
        return 0;
    }
    int vals[4];
    int vcount = 0;
    size_t w = 0;
    int saw_padding = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)payload[i];
        if (isspace(c)) {
            continue;
        }
        if (c == '=') {
            vals[vcount++] = -2;
            saw_padding = 1;
        } else {
            int v = svg_base64_value(c);
            if (v < 0 || saw_padding) {
                free(buf);
                return 0;
            }
            vals[vcount++] = v;
        }
        if (vcount == 4) {
            if (vals[0] < 0 || vals[1] < 0) {
                free(buf);
                return 0;
            }
            if (w + 3u > cap) {
                size_t new_cap = cap * 2u;
                unsigned char *tmp = (unsigned char *)realloc(buf, new_cap);
                if (!tmp) {
                    free(buf);
                    return 0;
                }
                buf = tmp;
                cap = new_cap;
            }
            buf[w++] = (unsigned char)((vals[0] << 2) | (vals[1] >> 4));
            if (vals[2] != -2) {
                if (vals[2] < 0) {
                    free(buf);
                    return 0;
                }
                buf[w++] = (unsigned char)(((vals[1] & 0x0F) << 4) | (vals[2] >> 2));
                if (vals[3] != -2) {
                    if (vals[3] < 0) {
                        free(buf);
                        return 0;
                    }
                    buf[w++] = (unsigned char)(((vals[2] & 0x03) << 6) | vals[3]);
                }
            }
            vcount = 0;
        }
    }
    if (vcount != 0) {
        free(buf);
        return 0;
    }
    *out = buf;
    *out_size = w;
    return 1;
}

int cupidimage_svg_load_href_image(const char *href, cupidimage_image *img) {
    if (!href || !img) {
        return 0;
    }
    memset(img, 0, sizeof(*img));
    if (*href == '#') {
        return 0;
    }
    if (svg_strncasecmp(href, "http://", 7) == 0 || svg_strncasecmp(href, "https://", 8) == 0) {
        return 0;
    }
    if (svg_strncasecmp(href, "data:", 5) == 0) {
        const char *comma = strchr(href, ',');
        if (!comma || comma <= href + 5) {
            return 0;
        }
        size_t meta_len = (size_t)(comma - (href + 5));
        char *meta = svg_strndup(href + 5, meta_len);
        if (!meta) {
            return 0;
        }
        int is_base64 = svg_strcasestr_simple(meta, ";base64");
        free(meta);
        unsigned char *payload = NULL;
        size_t payload_size = 0;
        if (!svg_decode_data_payload(comma + 1, is_base64, &payload, &payload_size)) {
            return 0;
        }
        char errbuf[128];
        int ok = cupidimage_load_image(payload, payload_size, img, errbuf, sizeof(errbuf));
        free(payload);
        return ok;
    }
    char errbuf[128];
    return cupidimage_load_image_file(href, img, errbuf, sizeof(errbuf));
}

const char *cupidimage_svg_get_attr(const svg_tag *tag, const char *name) {
    if (!tag || !name) {
        return NULL;
    }
    for (int i = 0; i < tag->attr_count; i++) {
        const char *attr_name = svg_local_name(tag->attrs[i].name);
        if (attr_name && svg_strcasecmp(attr_name, name) == 0) {
            return tag->attrs[i].value;
        }
    }
    return NULL;
}

const char *cupidimage_svg_get_attr_exact(const svg_tag *tag, const char *name) {
    if (!tag || !name) {
        return NULL;
    }
    for (int i = 0; i < tag->attr_count; i++) {
        if (tag->attrs[i].name && strcmp(tag->attrs[i].name, name) == 0) {
            return tag->attrs[i].value;
        }
    }
    return NULL;
}

void cupidimage_svg_elem_info_from_tag(svg_elem_info *info, const char *local, const svg_tag *tag) {
    if (!info) {
        return;
    }
    memset(info, 0, sizeof(*info));
    info->tag = local;
    if (!tag) {
        return;
    }
    info->id = cupidimage_svg_get_attr(tag, "id");
    info->class_attr = cupidimage_svg_get_attr(tag, "class");
    int count = tag->attr_count;
    if (count > SVG_MAX_INFO_ATTR) {
        count = SVG_MAX_INFO_ATTR;
    }
    info->attr_count = count;
    for (int i = 0; i < count; i++) {
        info->attr_names[i] = tag->attrs[i].name;
        info->attr_values[i] = tag->attrs[i].value;
    }
}

void cupidimage_svg_sibling_frames_free(svg_sibling_frame *frames, int depth_cap) {
    if (!frames || depth_cap <= 0) {
        return;
    }
    for (int i = 0; i < depth_cap; i++) {
        free(frames[i].items);
        frames[i].items = NULL;
        frames[i].count = 0;
        frames[i].cap = 0;
    }
}

int cupidimage_svg_sibling_frame_push(svg_sibling_frame *frame, const svg_elem_info *info) {
    if (!frame || !info) {
        return 0;
    }
    if (frame->count >= frame->cap) {
        int new_cap = frame->cap ? frame->cap * 2 : 16;
        svg_elem_info *n =
            (svg_elem_info *)realloc(frame->items, (size_t)new_cap * sizeof(svg_elem_info));
        if (!n) {
            return 0;
        }
        frame->items = n;
        frame->cap = new_cap;
    }
    frame->items[frame->count++] = *info;
    return 1;
}

void cupidimage_svg_skip_separators(const char **p) {
    while (**p && (isspace((unsigned char)**p) || **p == ',')) {
        (*p)++;
    }
}

int cupidimage_svg_parse_number(const char **p, float *out) {
    cupidimage_svg_skip_separators(p);
    if (!**p) {
        return 0;
    }
    char *end = NULL;
    double v = strtod(*p, &end);
    if (end == *p) {
        return 0;
    }
    *out = (float)v;
    *p = end;
    return 1;
}

int cupidimage_svg_parse_transform_number(const char **p, float *out) {
    if (!cupidimage_svg_parse_number(p, out)) {
        return 0;
    }
    while (**p && isspace((unsigned char)**p)) {
        (*p)++;
    }
    if (**p == '%') {
        (*p)++;
        return 1;
    }
    while (**p && isalpha((unsigned char)**p)) {
        (*p)++;
    }
    return 1;
}

float cupidimage_svg_parse_length(const char *s, float base, float dpi, int *ok) {
    if (ok) {
        *ok = 0;
    }
    if (!s) {
        return 0.0f;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s) {
        return 0.0f;
    }
    char *end = NULL;
    double val = strtod(s, &end);
    if (end == s) {
        return 0.0f;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    double out = val;
    if (*end == '%') {
        if (base <= 0.0f) {
            if (ok) {
                *ok = 0;
            }
            return 0.0f;
        }
        out = base * val / 100.0;
        end++;
    } else if (strncmp(end, "px", 2) == 0) {
        end += 2;
    } else if (strncmp(end, "pt", 2) == 0) {
        out = val * (dpi / 72.0);
        end += 2;
    } else if (strncmp(end, "pc", 2) == 0) {
        out = val * (dpi / 6.0);
        end += 2;
    } else if (strncmp(end, "in", 2) == 0) {
        out = val * dpi;
        end += 2;
    } else if (strncmp(end, "cm", 2) == 0) {
        out = val * (dpi / 2.54);
        end += 2;
    } else if (strncmp(end, "mm", 2) == 0) {
        out = val * (dpi / 25.4);
        end += 2;
    }
    if (ok) {
        *ok = 1;
    }
    return (float)out;
}

int cupidimage_svg_parse_length_value(const char **p, float base, float dpi, float *out) {
    if (!p || !*p || !out) {
        return 0;
    }
    cupidimage_svg_skip_separators(p);
    if (!**p) {
        return 0;
    }
    char *end = NULL;
    double val = strtod(*p, &end);
    if (end == *p) {
        return 0;
    }
    const char *q = end;
    while (isspace((unsigned char)*q)) {
        q++;
    }
    double outv = val;
    if (*q == '%') {
        if (base > 0.0f) {
            outv = base * val / 100.0;
        }
        q++;
    } else if (strncmp(q, "px", 2) == 0) {
        q += 2;
    } else if (strncmp(q, "pt", 2) == 0) {
        outv = val * (dpi / 72.0);
        q += 2;
    } else if (strncmp(q, "pc", 2) == 0) {
        outv = val * (dpi / 6.0);
        q += 2;
    } else if (strncmp(q, "in", 2) == 0) {
        outv = val * dpi;
        q += 2;
    } else if (strncmp(q, "cm", 2) == 0) {
        outv = val * (dpi / 2.54);
        q += 2;
    } else if (strncmp(q, "mm", 2) == 0) {
        outv = val * (dpi / 25.4);
        q += 2;
    }
    *out = (float)outv;
    *p = q;
    return 1;
}

int cupidimage_svg_parse_opacity_value(const char *s, float *out) {
    if (!s || !out) {
        return 0;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s) {
        return 0;
    }
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s) {
        return 0;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end == '%') {
        v /= 100.0;
        end++;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end) {
        return 0;
    }
    if (v < 0.0) {
        v = 0.0;
    } else if (v > 1.0) {
        v = 1.0;
    }
    *out = (float)v;
    return 1;
}

int cupidimage_svg_parse_viewbox(const char *s, float *x, float *y, float *w, float *h) {
    if (!s) {
        return 0;
    }
    const char *p = s;
    float vals[4];
    for (int i = 0; i < 4; i++) {
        if (!cupidimage_svg_parse_number(&p, &vals[i])) {
            return 0;
        }
    }
    *x = vals[0];
    *y = vals[1];
    *w = vals[2];
    *h = vals[3];
    return 1;
}

void cupidimage_svg_parse_preserve_aspect_ratio(const char *s, int *preserve_none, float *align_x,
                                                float *align_y) {
    if (preserve_none) {
        *preserve_none = 0;
    }
    if (align_x) {
        *align_x = 0.5f;
    }
    if (align_y) {
        *align_y = 0.5f;
    }
    if (!s) {
        return;
    }
    const char *tok = s;
    while (*tok && isspace((unsigned char)*tok)) {
        tok++;
    }
    if (strncmp(tok, "none", 4) == 0) {
        if (preserve_none) {
            *preserve_none = 1;
        }
        return;
    }
    if (strncmp(tok, "xMinYMin", 8) == 0) {
        if (align_x)
            *align_x = 0.0f;
        if (align_y)
            *align_y = 0.0f;
    } else if (strncmp(tok, "xMidYMin", 8) == 0) {
        if (align_x)
            *align_x = 0.5f;
        if (align_y)
            *align_y = 0.0f;
    } else if (strncmp(tok, "xMaxYMin", 8) == 0) {
        if (align_x)
            *align_x = 1.0f;
        if (align_y)
            *align_y = 0.0f;
    } else if (strncmp(tok, "xMinYMid", 8) == 0) {
        if (align_x)
            *align_x = 0.0f;
        if (align_y)
            *align_y = 0.5f;
    } else if (strncmp(tok, "xMidYMid", 8) == 0) {
        if (align_x)
            *align_x = 0.5f;
        if (align_y)
            *align_y = 0.5f;
    } else if (strncmp(tok, "xMaxYMid", 8) == 0) {
        if (align_x)
            *align_x = 1.0f;
        if (align_y)
            *align_y = 0.5f;
    } else if (strncmp(tok, "xMinYMax", 8) == 0) {
        if (align_x)
            *align_x = 0.0f;
        if (align_y)
            *align_y = 1.0f;
    } else if (strncmp(tok, "xMidYMax", 8) == 0) {
        if (align_x)
            *align_x = 0.5f;
        if (align_y)
            *align_y = 1.0f;
    } else if (strncmp(tok, "xMaxYMax", 8) == 0) {
        if (align_x)
            *align_x = 1.0f;
        if (align_y)
            *align_y = 1.0f;
    }
}

int cupidimage_svg_parse_clock_seconds(const char *s, float *out) {
    if (!s || !out) {
        return 0;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s) {
        return 0;
    }
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s) {
        return 0;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (svg_strncasecmp(end, "ms", 2) == 0) {
        v /= 1000.0;
        end += 2;
    } else if (*end == 's') {
        end++;
    } else if (*end == 'm') {
        v *= 60.0;
        end++;
    } else if (*end == 'h') {
        v *= 3600.0;
        end++;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end) {
        return 0;
    }
    *out = (float)v;
    return 1;
}

static int svg_parse_repeat_count(const char *s, float *out, int *indefinite) {
    if (!out || !indefinite) {
        return 0;
    }
    *out = 1.0f;
    *indefinite = 0;
    if (!s || !*s) {
        return 1;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (svg_strcasecmp(s, "indefinite") == 0) {
        *indefinite = 1;
        return 1;
    }
    char *end = NULL;
    double v = strtod(s, &end);
    if (end == s) {
        return 0;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end) {
        return 0;
    }
    if (v <= 0.0) {
        return 0;
    }
    *out = (float)v;
    return 1;
}

static int svg_get_animation_progress(const svg_tag *anim, float time_s, float *out_progress) {
    if (!anim || !out_progress) {
        return 0;
    }
    float begin = 0.0f;
    float dur = 0.0f;
    if (!cupidimage_svg_parse_clock_seconds(cupidimage_svg_get_attr(anim, "dur"), &dur) ||
        dur <= 0.0f) {
        return 0;
    }
    const char *begin_attr = cupidimage_svg_get_attr(anim, "begin");
    if (begin_attr && *begin_attr) {
        const char *semi = strchr(begin_attr, ';');
        if (semi) {
            begin_attr = svg_strndup(begin_attr, (size_t)(semi - begin_attr));
        }
        if (!cupidimage_svg_parse_clock_seconds(begin_attr, &begin)) {
            begin = 0.0f;
        }
        if (semi) {
            free((char *)begin_attr);
        }
    }
    if (time_s < begin) {
        return 0;
    }
    float local_t = time_s - begin;
    float repeat = 1.0f;
    int indefinite = 0;
    svg_parse_repeat_count(cupidimage_svg_get_attr(anim, "repeatCount"), &repeat, &indefinite);
    if (indefinite) {
        float t = fmodf(local_t, dur);
        if (t < 0.0f)
            t += dur;
        *out_progress = dur > 0.0f ? (t / dur) : 0.0f;
        return 1;
    }
    float total = dur * repeat;
    if (local_t >= total) {
        const char *fill = cupidimage_svg_get_attr(anim, "fill");
        if (fill && svg_strcasecmp(fill, "freeze") == 0) {
            *out_progress = 1.0f;
            return 1;
        }
        return 0;
    }
    float t = fmodf(local_t, dur);
    if (t < 0.0f)
        t += dur;
    *out_progress = dur > 0.0f ? (t / dur) : 0.0f;
    return 1;
}

static int svg_parse_number_list(const char *s, float *vals, int max_vals, int *out_count) {
    if (!s || !vals || max_vals <= 0 || !out_count) {
        return 0;
    }
    const char *p = s;
    int n = 0;
    while (n < max_vals) {
        float v = 0.0f;
        if (!cupidimage_svg_parse_number(&p, &v)) {
            break;
        }
        vals[n++] = v;
    }
    *out_count = n;
    return n > 0;
}

static int svg_parse_number_values_list(const char *s, float *vals, int max_vals, int *out_count) {
    if (!s || !vals || max_vals <= 0 || !out_count) {
        return 0;
    }
    int n = 0;
    const char *p = s;
    while (*p && n < max_vals) {
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        const char *start = p;
        while (*p && *p != ';') {
            p++;
        }
        const char *end = p;
        while (end > start && isspace((unsigned char)end[-1])) {
            end--;
        }
        if (end > start) {
            char *tok = svg_strndup(start, (size_t)(end - start));
            if (!tok) {
                return 0;
            }
            char *tokp = tok;
            float v = 0.0f;
            if (cupidimage_svg_parse_number((const char **)&tokp, &v)) {
                vals[n++] = v;
            }
            free(tok);
        }
        if (*p == ';') {
            p++;
        }
    }
    *out_count = n;
    return n > 0;
}

static int svg_tag_set_attr(svg_tag *tag, const char *name, const char *value) {
    if (!tag || !name || !value) {
        return 0;
    }
    for (int i = 0; i < tag->attr_count; i++) {
        const char *local = svg_local_name(tag->attrs[i].name);
        if (local && svg_strcasecmp(local, name) == 0) {
            snprintf(tag->override_values[i], sizeof(tag->override_values[i]), "%s", value);
            tag->attrs[i].value = tag->override_values[i];
            tag->has_override_value[i] = 1;
            return 1;
        }
    }
    if (tag->attr_count >= SVG_MAX_ATTR) {
        return 0;
    }
    snprintf(tag->override_names[tag->attr_count], sizeof(tag->override_names[tag->attr_count]),
             "%s", name);
    tag->attrs[tag->attr_count].name = tag->override_names[tag->attr_count];
    tag->has_override_name[tag->attr_count] = 1;
    snprintf(tag->override_values[tag->attr_count], sizeof(tag->override_values[tag->attr_count]),
             "%s", value);
    tag->attrs[tag->attr_count].value = tag->override_values[tag->attr_count];
    tag->has_override_value[tag->attr_count] = 1;
    tag->attr_count++;
    return 1;
}

static void svg_format_number(char *buf, size_t cap, float v) {
    if (!buf || cap == 0) {
        return;
    }
    snprintf(buf, cap, "%.6g", (double)v);
}

static int svg_animate_numeric(const svg_tag *anim, const char *attr_name, svg_tag *target,
                               float progress) {
    const char *from_s = cupidimage_svg_get_attr(anim, "from");
    const char *to_s = cupidimage_svg_get_attr(anim, "to");
    const char *by_s = cupidimage_svg_get_attr(anim, "by");
    const char *values_s = cupidimage_svg_get_attr(anim, "values");
    float out = 0.0f;
    int ok = 0;
    if (values_s && *values_s) {
        float vals[32];
        int count = 0;
        if (svg_parse_number_values_list(values_s, vals, 32, &count) && count > 0) {
            if (count == 1) {
                out = vals[0];
            } else {
                float segf = progress * (float)(count - 1);
                int seg = (int)floorf(segf);
                if (seg < 0)
                    seg = 0;
                if (seg >= count - 1) {
                    seg = count - 2;
                }
                float t = segf - (float)seg;
                out = vals[seg] + (vals[seg + 1] - vals[seg]) * t;
            }
            ok = 1;
        }
    }
    if (!ok && to_s && *to_s) {
        float to_v = 0.0f;
        const char *to_p = to_s;
        if (cupidimage_svg_parse_number(&to_p, &to_v)) {
            float from_v = 0.0f;
            int have_from = 0;
            if (from_s && *from_s) {
                const char *from_p = from_s;
                if (cupidimage_svg_parse_number(&from_p, &from_v)) {
                    have_from = 1;
                }
            }
            if (!have_from) {
                const char *base_s = cupidimage_svg_get_attr(target, attr_name);
                if (base_s) {
                    const char *base_p = base_s;
                    if (cupidimage_svg_parse_number(&base_p, &from_v)) {
                        have_from = 1;
                    }
                }
            }
            if (!have_from) {
                from_v = 0.0f;
            }
            out = from_v + (to_v - from_v) * progress;
            ok = 1;
        }
    }
    if (!ok && by_s && *by_s) {
        float by_v = 0.0f;
        const char *by_p = by_s;
        if (cupidimage_svg_parse_number(&by_p, &by_v)) {
            float from_v = 0.0f;
            if (from_s && *from_s) {
                const char *from_p = from_s;
                cupidimage_svg_parse_number(&from_p, &from_v);
            } else {
                const char *base_s = cupidimage_svg_get_attr(target, attr_name);
                if (base_s) {
                    const char *base_p = base_s;
                    cupidimage_svg_parse_number(&base_p, &from_v);
                }
            }
            out = from_v + by_v * progress;
            ok = 1;
        }
    }
    if (!ok) {
        return 0;
    }
    char num[64];
    svg_format_number(num, sizeof(num), out);
    return svg_tag_set_attr(target, attr_name, num);
}

static int svg_animate_transform(const svg_tag *anim, svg_tag *target, float progress) {
    const char *type = cupidimage_svg_get_attr(anim, "type");
    if (!type || !*type) {
        return 0;
    }
    const char *from_s = cupidimage_svg_get_attr(anim, "from");
    const char *to_s = cupidimage_svg_get_attr(anim, "to");
    const char *by_s = cupidimage_svg_get_attr(anim, "by");
    float from_v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float to_v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float by_v[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int from_n = 0, to_n = 0, by_n = 0;
    if (from_s)
        svg_parse_number_list(from_s, from_v, 4, &from_n);
    if (to_s)
        svg_parse_number_list(to_s, to_v, 4, &to_n);
    if (by_s)
        svg_parse_number_list(by_s, by_v, 4, &by_n);
    char out[128];
    out[0] = '\0';
    if (svg_strcasecmp(type, "translate") == 0) {
        float fx = from_n > 0 ? from_v[0] : 0.0f;
        float fy = from_n > 1 ? from_v[1] : 0.0f;
        float x = fx;
        float y = fy;
        if (to_n > 0) {
            float tx = to_v[0];
            float ty = to_n > 1 ? to_v[1] : 0.0f;
            x = fx + (tx - fx) * progress;
            y = fy + (ty - fy) * progress;
        } else if (by_n > 0) {
            float bx = by_v[0];
            float by = by_n > 1 ? by_v[1] : 0.0f;
            x = fx + bx * progress;
            y = fy + by * progress;
        } else {
            return 0;
        }
        snprintf(out, sizeof(out), "translate(%.6g %.6g)", (double)x, (double)y);
    } else if (svg_strcasecmp(type, "scale") == 0) {
        float fx = from_n > 0 ? from_v[0] : 1.0f;
        float fy = from_n > 1 ? from_v[1] : fx;
        float x = fx;
        float y = fy;
        if (to_n > 0) {
            float tx = to_v[0];
            float ty = to_n > 1 ? to_v[1] : tx;
            x = fx + (tx - fx) * progress;
            y = fy + (ty - fy) * progress;
        } else if (by_n > 0) {
            float bx = by_v[0];
            float by = by_n > 1 ? by_v[1] : bx;
            x = fx + bx * progress;
            y = fy + by * progress;
        } else {
            return 0;
        }
        snprintf(out, sizeof(out), "scale(%.6g %.6g)", (double)x, (double)y);
    } else if (svg_strcasecmp(type, "rotate") == 0) {
        float fa = from_n > 0 ? from_v[0] : 0.0f;
        float a = fa;
        float cx = from_n > 1 ? from_v[1] : 0.0f;
        float cy = from_n > 2 ? from_v[2] : 0.0f;
        if (to_n > 0) {
            float ta = to_v[0];
            a = fa + (ta - fa) * progress;
            if (from_n > 2 && to_n > 2) {
                cx = from_v[1] + (to_v[1] - from_v[1]) * progress;
                cy = from_v[2] + (to_v[2] - from_v[2]) * progress;
            }
        } else if (by_n > 0) {
            a = fa + by_v[0] * progress;
        } else {
            return 0;
        }
        if (from_n > 2 || to_n > 2) {
            snprintf(out, sizeof(out), "rotate(%.6g %.6g %.6g)", (double)a, (double)cx, (double)cy);
        } else {
            snprintf(out, sizeof(out), "rotate(%.6g)", (double)a);
        }
    } else if (svg_strcasecmp(type, "skewx") == 0 || svg_strcasecmp(type, "skewy") == 0) {
        float fa = from_n > 0 ? from_v[0] : 0.0f;
        float a = fa;
        if (to_n > 0) {
            a = fa + (to_v[0] - fa) * progress;
        } else if (by_n > 0) {
            a = fa + by_v[0] * progress;
        } else {
            return 0;
        }
        snprintf(out, sizeof(out), "%s(%.6g)",
                 svg_strcasecmp(type, "skewx") == 0 ? "skewX" : "skewY", (double)a);
    } else {
        return 0;
    }
    return svg_tag_set_attr(target, "transform", out);
}

void cupidimage_svg_apply_animation_children(svg_tag *target, const char *target_local,
                                             char *child_start, float time_s) {
    if (!target || !target_local || !child_start || time_s < 0.0f) {
        return;
    }
    if (!svg_strcasestr_simple(child_start, "<animate") &&
        !svg_strcasestr_simple(child_start, "<set")) {
        return;
    }
    char *scan = svg_strdup(child_start);
    if (!scan) {
        return;
    }
    char *q = scan;
    svg_tag tag;
    int nested = 0;
    while (cupidimage_svg_next_tag(&q, &tag)) {
        const char *local = svg_local_name(tag.name);
        if (tag.is_end) {
            if (svg_strcasecmp(local, target_local) == 0) {
                if (nested == 0) {
                    break;
                }
                nested--;
            }
            continue;
        }
        if (svg_strcasecmp(local, target_local) == 0 && !tag.is_self_closing) {
            nested++;
            continue;
        }
        if (nested > 0) {
            continue;
        }
        if (svg_strcasecmp(local, "set") == 0) {
            float prog = 0.0f;
            if (!svg_get_animation_progress(&tag, time_s, &prog)) {
                continue;
            }
            const char *attr_name = cupidimage_svg_get_attr(&tag, "attributeName");
            const char *to = cupidimage_svg_get_attr(&tag, "to");
            if (attr_name && to && *attr_name && *to) {
                svg_tag_set_attr(target, attr_name, to);
            }
            continue;
        }
        if (svg_strcasecmp(local, "animate") == 0) {
            float prog = 0.0f;
            if (!svg_get_animation_progress(&tag, time_s, &prog)) {
                continue;
            }
            const char *attr_name = cupidimage_svg_get_attr(&tag, "attributeName");
            if (!attr_name || !*attr_name) {
                continue;
            }
            if (!svg_animate_numeric(&tag, attr_name, target, prog)) {
                const char *values = cupidimage_svg_get_attr(&tag, "values");
                if (values) {
                    const char *start = values;
                    const char *end = values;
                    const char *chosen = values;
                    int count = 0;
                    while (*end) {
                        if (*end == ';') {
                            count++;
                        }
                        end++;
                    }
                    count++;
                    int idx = (int)floorf(prog * (float)count);
                    if (idx >= count)
                        idx = count - 1;
                    int cur = 0;
                    start = values;
                    end = values;
                    while (*end) {
                        if (*end == ';') {
                            if (cur == idx) {
                                break;
                            }
                            cur++;
                            start = end + 1;
                        }
                        end++;
                    }
                    if (cur == idx) {
                        chosen = start;
                    }
                    if (chosen) {
                        size_t n = 0;
                        while (chosen[n] && chosen[n] != ';')
                            n++;
                        while (n > 0 && isspace((unsigned char)chosen[n - 1]))
                            n--;
                        while (*chosen && isspace((unsigned char)*chosen))
                            chosen++;
                        char *val = svg_strndup(chosen, n);
                        if (val) {
                            svg_tag_set_attr(target, attr_name, val);
                            free(val);
                        }
                    }
                }
            }
            continue;
        }
        if (svg_strcasecmp(local, "animatetransform") == 0) {
            float prog = 0.0f;
            if (!svg_get_animation_progress(&tag, time_s, &prog)) {
                continue;
            }
            svg_animate_transform(&tag, target, prog);
            continue;
        }
    }
    free(scan);
}

static const svg_css_keyframes *svg_css_find_keyframes(const svg_css *css, const char *name) {
    if (!css || !name || !*name) {
        return NULL;
    }
    for (int i = 0; i < css->keyframe_count; i++) {
        if (css->keyframes[i].name && strcmp(css->keyframes[i].name, name) == 0) {
            return &css->keyframes[i];
        }
    }
    return NULL;
}

static float svg_css_apply_anim_direction(int direction, int cycle, float phase) {
    int reverse = 0;
    if (direction == SVG_CSS_ANIM_DIR_REVERSE) {
        reverse = 1;
    } else if (direction == SVG_CSS_ANIM_DIR_ALTERNATE) {
        reverse = (cycle & 1) != 0;
    } else if (direction == SVG_CSS_ANIM_DIR_ALTERNATE_REVERSE) {
        reverse = (cycle & 1) == 0;
    }
    return reverse ? (1.0f - phase) : phase;
}

static int svg_css_anim_progress(const svg_css_animation_binding *anim, float time_s,
                                 float *out_progress) {
    if (!anim || !out_progress || anim->duration <= 0.0f) {
        return 0;
    }
    float t = time_s - anim->delay;
    if (t < 0.0f) {
        if (anim->fill_mode == SVG_CSS_ANIM_FILL_BACKWARDS ||
            anim->fill_mode == SVG_CSS_ANIM_FILL_BOTH) {
            *out_progress = svg_css_apply_anim_direction(anim->direction, 0, 0.0f);
            return 1;
        }
        return 0;
    }
    if (anim->repeat_indefinite) {
        float cyclef = floorf(t / anim->duration);
        int cycle = (int)cyclef;
        float phase = (t - cyclef * anim->duration) / anim->duration;
        if (phase < 0.0f)
            phase = 0.0f;
        if (phase > 1.0f)
            phase = 1.0f;
        *out_progress = svg_css_apply_anim_direction(anim->direction, cycle, phase);
        return 1;
    }
    float repeats = anim->repeat_count > 0.0f ? anim->repeat_count : 1.0f;
    float total = anim->duration * repeats;
    if (t >= total) {
        if (anim->fill_mode != SVG_CSS_ANIM_FILL_FORWARDS &&
            anim->fill_mode != SVG_CSS_ANIM_FILL_BOTH) {
            return 0;
        }
        float wholef = floorf(repeats);
        int cycle = 0;
        float phase = 1.0f;
        if (fabsf(repeats - wholef) < 1e-6f) {
            cycle = (int)(wholef > 1.0f ? wholef - 1.0f : 0.0f);
            phase = 1.0f;
        } else {
            cycle = (int)wholef;
            phase = repeats - wholef;
        }
        *out_progress = svg_css_apply_anim_direction(anim->direction, cycle, phase);
        return 1;
    }
    float cyclef = floorf(t / anim->duration);
    int cycle = (int)cyclef;
    float phase = (t - cyclef * anim->duration) / anim->duration;
    if (phase < 0.0f)
        phase = 0.0f;
    if (phase > 1.0f)
        phase = 1.0f;
    *out_progress = svg_css_apply_anim_direction(anim->direction, cycle, phase);
    return 1;
}

static int svg_css_keyframe_range(const svg_css_keyframes *kf, float progress,
                                  const svg_css_keyframe_step **a, const svg_css_keyframe_step **b,
                                  float *u) {
    if (!kf || kf->step_count <= 0 || !a || !b || !u) {
        return 0;
    }
    const svg_css_keyframe_step *steps = kf->steps;
    int n = kf->step_count;
    if (progress <= steps[0].offset) {
        *a = &steps[0];
        *b = &steps[0];
        *u = 0.0f;
        return 1;
    }
    if (progress >= steps[n - 1].offset) {
        *a = &steps[n - 1];
        *b = &steps[n - 1];
        *u = 0.0f;
        return 1;
    }
    for (int i = 0; i < n - 1; i++) {
        if (progress >= steps[i].offset && progress <= steps[i + 1].offset) {
            *a = &steps[i];
            *b = &steps[i + 1];
            float span = steps[i + 1].offset - steps[i].offset;
            *u = (span > 0.0f) ? (progress - steps[i].offset) / span : 0.0f;
            if (*u < 0.0f)
                *u = 0.0f;
            if (*u > 1.0f)
                *u = 1.0f;
            return 1;
        }
    }
    *a = &steps[n - 1];
    *b = &steps[n - 1];
    *u = 0.0f;
    return 1;
}

void cupidimage_svg_css_apply_animations_to_tag(svg_tag *tag, const svg_css *css, float time_s) {
    if (!tag || !css || css->animation_count <= 0) {
        return;
    }
    const char *id = cupidimage_svg_get_attr(tag, "id");
    if (!id || !*id) {
        return;
    }
    for (int i = 0; i < css->animation_count; i++) {
        const svg_css_animation_binding *anim = &css->animations[i];
        if (!anim->target_id || strcmp(anim->target_id, id) != 0) {
            continue;
        }
        float progress = 0.0f;
        if (!svg_css_anim_progress(anim, time_s, &progress)) {
            continue;
        }
        const svg_css_keyframes *kf = svg_css_find_keyframes(css, anim->name);
        if (!kf) {
            continue;
        }
        const svg_css_keyframe_step *a = NULL;
        const svg_css_keyframe_step *b = NULL;
        float u = 0.0f;
        if (!svg_css_keyframe_range(kf, progress, &a, &b, &u) || !a || !b) {
            continue;
        }
        if (kf->prop == SVG_CSS_ANIM_PROP_TRANSFORM) {
            const char *av = a->transform;
            const char *bv = b->transform;
            if (av && bv) {
                svg_matrix ma;
                svg_matrix mb;
                if (cupidimage_svg_parse_transform(av, &ma) &&
                    cupidimage_svg_parse_transform(bv, &mb)) {
                    svg_matrix m;
                    m.a = ma.a + (mb.a - ma.a) * u;
                    m.b = ma.b + (mb.b - ma.b) * u;
                    m.c = ma.c + (mb.c - ma.c) * u;
                    m.d = ma.d + (mb.d - ma.d) * u;
                    m.e = ma.e + (mb.e - ma.e) * u;
                    m.f = ma.f + (mb.f - ma.f) * u;
                    char buf[160];
                    snprintf(buf, sizeof(buf), "matrix(%.6g %.6g %.6g %.6g %.6g %.6g)", (double)m.a,
                             (double)m.b, (double)m.c, (double)m.d, (double)m.e, (double)m.f);
                    svg_tag_set_attr(tag, "transform", buf);
                } else {
                    svg_tag_set_attr(tag, "transform", u < 0.5f ? av : bv);
                }
            }
        } else if (kf->prop == SVG_CSS_ANIM_PROP_STROKE_DASHOFFSET) {
            float v = a->number + (b->number - a->number) * u;
            char buf[64];
            snprintf(buf, sizeof(buf), "%.6g", (double)v);
            svg_tag_set_attr(tag, "stroke-dashoffset", buf);
        } else if (kf->prop == SVG_CSS_ANIM_PROP_STOP_COLOR) {
            uint32_t ca = a->color;
            uint32_t cb = b->color;
            uint8_t ar = (uint8_t)(ca >> 24);
            uint8_t ag = (uint8_t)(ca >> 16);
            uint8_t ab = (uint8_t)(ca >> 8);
            uint8_t aa = (uint8_t)(ca & 0xFFu);
            uint8_t br = (uint8_t)(cb >> 24);
            uint8_t bg = (uint8_t)(cb >> 16);
            uint8_t bb = (uint8_t)(cb >> 8);
            uint8_t ba = (uint8_t)(cb & 0xFFu);
            uint8_t r = (uint8_t)lroundf(ar + (br - ar) * u);
            uint8_t g = (uint8_t)lroundf(ag + (bg - ag) * u);
            uint8_t bch = (uint8_t)lroundf(ab + (bb - ab) * u);
            uint8_t a8 = (uint8_t)lroundf(aa + (ba - aa) * u);
            char buf[96];
            if (a8 == 255) {
                snprintf(buf, sizeof(buf), "#%02x%02x%02x", r, g, bch);
            } else {
                snprintf(buf, sizeof(buf), "rgba(%u,%u,%u,%.6g)", r, g, bch, (double)(a8 / 255.0f));
            }
            svg_tag_set_attr(tag, "stop-color", buf);
        }
    }
}

char *cupidimage_svg_find_close_tag(char *p, const char *tag_name, char **after) {
    if (!p || !tag_name) {
        return NULL;
    }
    size_t name_len = strlen(tag_name);
    while (*p) {
        char *lt = strchr(p, '<');
        if (!lt) {
            return NULL;
        }
        if (lt[1] == '/') {
            char *q = lt + 2;
            size_t i = 0;
            for (; i < name_len && q[i]; i++) {
                if (tolower((unsigned char)q[i]) != tolower((unsigned char)tag_name[i])) {
                    break;
                }
            }
            if (i == name_len) {
                char *gt = strchr(lt, '>');
                if (gt) {
                    if (after) {
                        *after = gt + 1;
                    }
                    return lt;
                }
                return lt;
            }
        }
        p = lt + 1;
    }
    return NULL;
}

int cupidimage_svg_parse_preamble(const unsigned char *data, size_t size, svg_preamble *pre,
                                  svg_defs *defs, svg_css *css, float dpi, float anim_time,
                                  char *err, size_t errcap) {
    if (!data || !pre) {
        cupidimage_set_err(err, errcap, "invalid arguments");
        return 0;
    }
    memset(pre, 0, sizeof(*pre));
    pre->align_x = 0.5f;
    pre->align_y = 0.5f;
    cupidimage_svg_defs_init(defs);
    cupidimage_svg_css_init(css);
    char *buf = (char *)malloc(size + 1);
    if (!buf) {
        cupidimage_set_err(err, errcap, "out of memory");
        return 0;
    }
    memcpy(buf, data, size);
    buf[size] = '\0';
    char *p = buf;
    svg_tag tag;
    svg_gradient *current_grad = NULL;
    while (cupidimage_svg_next_tag(&p, &tag)) {
        const char *local = svg_local_name(tag.name);
        if (!tag.is_end) {
            cupidimage_svg_defs_add_use(defs, &tag);
        }
        if (!tag.is_end && !pre->found_svg && strcmp(local, "svg") == 0) {
            pre->found_svg = 1;
            const char *w = cupidimage_svg_get_attr(&tag, "width");
            const char *h = cupidimage_svg_get_attr(&tag, "height");
            pre->width_attr = cupidimage_svg_parse_length(w, 0.0f, dpi, &pre->width_ok);
            pre->height_attr = cupidimage_svg_parse_length(h, 0.0f, dpi, &pre->height_ok);
            pre->vb_ok =
                cupidimage_svg_parse_viewbox(cupidimage_svg_get_attr(&tag, "viewBox"), &pre->vb_x,
                                             &pre->vb_y, &pre->vb_w, &pre->vb_h);
            cupidimage_svg_parse_preserve_aspect_ratio(
                cupidimage_svg_get_attr(&tag, "preserveAspectRatio"), &pre->preserve_none,
                &pre->align_x, &pre->align_y);
        }
        if (!tag.is_end &&
            (strcmp(local, "lineargradient") == 0 || strcmp(local, "radialgradient") == 0)) {
            const char *id = cupidimage_svg_get_attr(&tag, "id");
            svg_paint_type type = strcmp(local, "lineargradient") == 0 ? SVG_PAINT_LINEAR_GRADIENT
                                                                       : SVG_PAINT_RADIAL_GRADIENT;
            svg_gradient *g = cupidimage_svg_defs_add_gradient(defs, id, type);
            if (g) {
                cupidimage_svg_parse_gradient_tag(&tag, g);
                current_grad = g;
            } else {
                current_grad = NULL;
            }
            if (tag.is_self_closing) {
                current_grad = NULL;
            }
            continue;
        }
        if (!tag.is_end && strcmp(local, "path") == 0) {
            const char *id = cupidimage_svg_get_attr(&tag, "id");
            const char *d = cupidimage_svg_get_attr(&tag, "d");
            if (id && d) {
                cupidimage_svg_defs_add_path(defs, id, d);
            }
        }
        if (!tag.is_end && strcmp(local, "pattern") == 0) {
            const char *id = cupidimage_svg_get_attr(&tag, "id");
            svg_pattern *pat = cupidimage_svg_defs_add_pattern(defs, id);
            if (pat) {
                cupidimage_svg_parse_pattern_tag(&tag, pat);
            }
            if (!tag.is_self_closing) {
                char *after = NULL;
                char *close = cupidimage_svg_find_close_tag(p, "pattern", &after);
                if (close) {
                    *close = '\0';
                    if (pat) {
                        free(pat->content);
                        pat->content = svg_strdup(p);
                    }
                    if (after) {
                        p = after;
                    }
                }
            }
            continue;
        }
        if (!tag.is_end && strcmp(local, "symbol") == 0) {
            const char *id = cupidimage_svg_get_attr(&tag, "id");
            svg_symbol *sym = cupidimage_svg_defs_add_symbol(defs, id);
            if (sym) {
                cupidimage_svg_parse_symbol_tag(&tag, sym);
            }
            if (!tag.is_self_closing) {
                char *after = NULL;
                char *close = cupidimage_svg_find_close_tag(p, "symbol", &after);
                if (close) {
                    *close = '\0';
                    if (sym) {
                        free(sym->content);
                        sym->content = svg_strdup(p);
                    }
                    if (after) {
                        p = after;
                    }
                }
            }
            continue;
        }
        if (!tag.is_end && strcmp(local, "marker") == 0) {
            const char *id = cupidimage_svg_get_attr(&tag, "id");
            svg_marker *mk = cupidimage_svg_defs_add_marker(defs, id);
            if (mk) {
                float base_len = pre->vb_ok ? ((pre->vb_w + pre->vb_h) * 0.5f) : 512.0f;
                cupidimage_svg_parse_marker_tag(&tag, mk, base_len, dpi);
            }
            if (!tag.is_self_closing) {
                char *after = NULL;
                char *close = cupidimage_svg_find_close_tag(p, "marker", &after);
                if (close) {
                    *close = '\0';
                    if (mk) {
                        free(mk->content);
                        mk->content = svg_strdup(p);
                    }
                    if (after) {
                        p = after;
                    }
                }
            }
            continue;
        }
        if (!tag.is_end && strcmp(local, "clippath") == 0) {
            const char *id = cupidimage_svg_get_attr(&tag, "id");
            svg_clip_path *clip = cupidimage_svg_defs_add_clip(defs, id);
            if (clip) {
                cupidimage_svg_parse_clip_tag(&tag, clip);
            }
            if (!tag.is_self_closing) {
                char *after = NULL;
                char *close = cupidimage_svg_find_close_tag(p, "clipPath", &after);
                if (close) {
                    *close = '\0';
                    if (clip) {
                        free(clip->content);
                        clip->content = svg_strdup(p);
                    }
                    if (after) {
                        p = after;
                    }
                }
            }
            continue;
        }
        if (!tag.is_end && strcmp(local, "mask") == 0) {
            const char *id = cupidimage_svg_get_attr(&tag, "id");
            svg_mask *mask = cupidimage_svg_defs_add_mask(defs, id);
            if (mask) {
                cupidimage_svg_parse_mask_tag(&tag, mask);
            }
            if (!tag.is_self_closing) {
                char *after = NULL;
                char *close = cupidimage_svg_find_close_tag(p, "mask", &after);
                if (close) {
                    *close = '\0';
                    if (mask) {
                        free(mask->content);
                        mask->content = svg_strdup(p);
                    }
                    if (after) {
                        p = after;
                    }
                }
            }
            continue;
        }
        if (!tag.is_end && strcmp(local, "filter") == 0) {
            const char *id = cupidimage_svg_get_attr(&tag, "id");
            svg_filter *filter = cupidimage_svg_defs_add_filter(defs, id);
            if (filter) {
                cupidimage_svg_parse_filter_tag(&tag, filter);
            }
            if (!tag.is_self_closing) {
                char *after = NULL;
                char *close = cupidimage_svg_find_close_tag(p, "filter", &after);
                if (close) {
                    *close = '\0';
                    if (filter) {
                        free(filter->content);
                        filter->content = svg_strdup(p);
                    }
                    if (after) {
                        p = after;
                    }
                }
            }
            continue;
        }
        if (tag.is_end &&
            (strcmp(local, "lineargradient") == 0 || strcmp(local, "radialgradient") == 0)) {
            current_grad = NULL;
            continue;
        }
        if (current_grad && !tag.is_end && strcmp(local, "stop") == 0) {
            float off = 0.0f;
            uint32_t color = 0;
            svg_tag stop_tag = tag;
            if (anim_time >= 0.0f) {
                cupidimage_svg_css_apply_animations_to_tag(&stop_tag, css, anim_time);
            }
            if (cupidimage_svg_stop_parse(&stop_tag, &off, &color)) {
                cupidimage_svg_gradient_add_stop(current_grad, off, color);
            }
            continue;
        }
        if (!tag.is_end && strcmp(local, "style") == 0 && !tag.is_self_closing) {
            char *after = NULL;
            char *close = cupidimage_svg_find_close_tag(p, "style", &after);
            if (close) {
                *close = '\0';
                cupidimage_svg_css_parse(css, p);
                if (after) {
                    p = after;
                }
            }
            continue;
        }
    }
    for (int i = 0; i < defs->gradient_count; i++) {
        cupidimage_svg_gradient_inherit(&defs->gradients[i], defs);
        cupidimage_svg_gradient_finalize(&defs->gradients[i]);
    }
    for (int i = 0; i < defs->pattern_count; i++) {
        cupidimage_svg_pattern_inherit(&defs->patterns[i], defs);
        cupidimage_svg_pattern_finalize(&defs->patterns[i]);
    }
    for (int i = 0; i < defs->clip_count; i++) {
        cupidimage_svg_clip_inherit(&defs->clips[i], defs);
        cupidimage_svg_clip_finalize(&defs->clips[i]);
    }
    for (int i = 0; i < defs->mask_count; i++) {
        cupidimage_svg_mask_inherit(&defs->masks[i], defs);
        cupidimage_svg_mask_finalize(&defs->masks[i]);
    }
    free(buf);
    if (!pre->found_svg) {
        cupidimage_set_err(err, errcap, "not an SVG");
        return 0;
    }
    return 1;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
