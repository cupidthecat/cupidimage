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

static int svg_find_filter_result(const char *name, char **names, int name_count);
static svg_filter_ref svg_parse_filter_ref(const char *s, char **names, int name_count);
static void svg_filter_op_init(svg_filter_op *op);
static void svg_color_matrix_identity(float *m);
static void svg_color_matrix_saturate(float s, float *m);
static void svg_color_matrix_huerotate(float angle_deg, float *m);
static void svg_color_matrix_luminance_to_alpha(float *m);
static void svg_filter_assign_result(const svg_tag *tag, svg_filter_op *op, char ***names,
                                     int *name_count, int *name_cap);
static void svg_filter_parse_region(const svg_tag *tag, svg_filter_op *op,
                                    svg_gradient_units units);
static void svg_transfer_func_init(svg_transfer_func *func);
static void svg_parse_transfer_func(const svg_tag *tag, svg_transfer_func *func);

static int svg_find_filter_result(const char *name, char **names, int name_count) {
    if (!name || !*name || !names) {
        return -1;
    }
    for (int i = 0; i < name_count; i++) {
        if (names[i] && strcmp(names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

svg_filter_ref cupidimage_svg_make_filter_ref(int type, int index) {
    svg_filter_ref ref;
    ref.type = type;
    ref.index = index;
    return ref;
}

static svg_filter_ref svg_parse_filter_ref(const char *s, char **names, int name_count) {
    if (!s || !*s) {
        return cupidimage_svg_make_filter_ref(SVG_FILTER_IN_CURRENT, -1);
    }
    if (svg_strcasecmp(s, "sourcegraphic") == 0) {
        return cupidimage_svg_make_filter_ref(SVG_FILTER_IN_SOURCE_GRAPHIC, -1);
    }
    if (svg_strcasecmp(s, "sourcealpha") == 0) {
        return cupidimage_svg_make_filter_ref(SVG_FILTER_IN_SOURCE_ALPHA, -1);
    }
    if (svg_strcasecmp(s, "backgroundimage") == 0) {
        return cupidimage_svg_make_filter_ref(SVG_FILTER_IN_BACKGROUND_IMAGE, -1);
    }
    if (svg_strcasecmp(s, "backgroundalpha") == 0) {
        return cupidimage_svg_make_filter_ref(SVG_FILTER_IN_BACKGROUND_ALPHA, -1);
    }
    if (svg_strcasecmp(s, "fillpaint") == 0) {
        return cupidimage_svg_make_filter_ref(SVG_FILTER_IN_FILL_PAINT, -1);
    }
    if (svg_strcasecmp(s, "strokepaint") == 0) {
        return cupidimage_svg_make_filter_ref(SVG_FILTER_IN_STROKE_PAINT, -1);
    }
    int idx = svg_find_filter_result(s, names, name_count);
    if (idx >= 0) {
        return cupidimage_svg_make_filter_ref(SVG_FILTER_IN_RESULT, idx);
    }
    return cupidimage_svg_make_filter_ref(SVG_FILTER_IN_CURRENT, -1);
}

static void svg_filter_op_init(svg_filter_op *op) {
    if (!op) {
        return;
    }
    memset(op, 0, sizeof(*op));
    op->result_id = -1;
    op->kernel = NULL;
    op->kernel_count = 0;
    op->has_region = 0;
    op->rx.value = 0.0f;
    op->ry.value = 0.0f;
    op->rwidth.value = 0.0f;
    op->rheight.value = 0.0f;
    op->rx.is_percent = 0;
    op->ry.is_percent = 0;
    op->rwidth.is_percent = 0;
    op->rheight.is_percent = 0;
    op->order_x = 0;
    op->order_y = 0;
    op->target_x = 0;
    op->target_y = 0;
    op->divisor = 0.0f;
    op->bias = 0.0f;
    op->preserve_alpha = 0;
    op->turb_freq_x = 0.0f;
    op->turb_freq_y = 0.0f;
    op->turb_octaves = 1;
    op->turb_seed = 0;
    op->turb_fractal = 0;
    op->displace_x_channel = SVG_DISPLACE_R;
    op->displace_y_channel = SVG_DISPLACE_G;
    op->surface_scale = 1.0f;
    op->diffuse_constant = 1.0f;
    op->specular_constant = 1.0f;
    op->specular_exponent = 1.0f;
    op->light_x = 0.0f;
    op->light_y = 0.0f;
    op->light_z = 1.0f;
    op->image_href = NULL;
    op->img_x = 0.0f;
    op->img_y = 0.0f;
    op->img_w = 0.0f;
    op->img_h = 0.0f;
    op->has_image_geom = 0;
}

void cupidimage_svg_filter_ops_cleanup(svg_filter_op *ops, int count) {
    if (!ops || count <= 0) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(ops[i].kernel);
        ops[i].kernel = NULL;
        ops[i].kernel_count = 0;
        free(ops[i].image_href);
        ops[i].image_href = NULL;
    }
}

static void svg_color_matrix_identity(float *m) {
    for (int i = 0; i < 20; i++) {
        m[i] = 0.0f;
    }
    m[0] = 1.0f;
    m[6] = 1.0f;
    m[12] = 1.0f;
    m[18] = 1.0f;
}

static void svg_color_matrix_saturate(float s, float *m) {
    float ir = 0.213f;
    float ig = 0.715f;
    float ib = 0.072f;
    svg_color_matrix_identity(m);
    m[0] = ir + s * (1.0f - ir);
    m[1] = ig - s * ig;
    m[2] = ib - s * ib;
    m[5] = ir - s * ir;
    m[6] = ig + s * (1.0f - ig);
    m[7] = ib - s * ib;
    m[10] = ir - s * ir;
    m[11] = ig - s * ig;
    m[12] = ib + s * (1.0f - ib);
}

static void svg_color_matrix_huerotate(float angle_deg, float *m) {
    float ir = 0.213f;
    float ig = 0.715f;
    float ib = 0.072f;
    float a = angle_deg * (float)M_PI / 180.0f;
    float c = cosf(a);
    float s = sinf(a);
    svg_color_matrix_identity(m);
    m[0] = ir + c * (1.0f - ir) + s * (-ir);
    m[1] = ig + c * (-ig) + s * (-ig);
    m[2] = ib + c * (-ib) + s * (1.0f - ib);
    m[5] = ir + c * (-ir) + s * 0.143f;
    m[6] = ig + c * (1.0f - ig) + s * 0.140f;
    m[7] = ib + c * (-ib) + s * (-0.283f);
    m[10] = ir + c * (-ir) + s * (-(1.0f - ir));
    m[11] = ig + c * (-ig) + s * ig;
    m[12] = ib + c * (1.0f - ib) + s * ib;
}

static void svg_color_matrix_luminance_to_alpha(float *m) {
    for (int i = 0; i < 20; i++) {
        m[i] = 0.0f;
    }
    m[15] = 0.2126f;
    m[16] = 0.7152f;
    m[17] = 0.0722f;
}

static void svg_filter_assign_result(const svg_tag *tag, svg_filter_op *op, char ***names,
                                     int *name_count, int *name_cap) {
    if (!op) {
        return;
    }
    op->result_id = -1;
    if (!tag || !names || !name_count || !name_cap) {
        return;
    }
    const char *res = cupidimage_svg_get_attr(tag, "result");
    if (!res || !*res) {
        return;
    }
    int idx = svg_find_filter_result(res, *names, *name_count);
    if (idx < 0) {
        if (*name_count >= *name_cap) {
            int new_cap = *name_cap ? *name_cap * 2 : 8;
            char **n = (char **)realloc(*names, (size_t)new_cap * sizeof(char *));
            if (!n) {
                return;
            }
            *names = n;
            *name_cap = new_cap;
        }
        (*names)[*name_count] = svg_strdup(res);
        if (!(*names)[*name_count]) {
            return;
        }
        idx = *name_count;
        (*name_count)++;
    }
    op->result_id = idx;
}

static void svg_filter_parse_region(const svg_tag *tag, svg_filter_op *op,
                                    svg_gradient_units units) {
    if (!tag || !op) {
        return;
    }
    int has = 0;
    const char *x = cupidimage_svg_get_attr(tag, "x");
    const char *y = cupidimage_svg_get_attr(tag, "y");
    const char *w = cupidimage_svg_get_attr(tag, "width");
    const char *h = cupidimage_svg_get_attr(tag, "height");
    if (x && cupidimage_svg_parse_grad_coord(x, &op->rx, units))
        has = 1;
    if (y && cupidimage_svg_parse_grad_coord(y, &op->ry, units))
        has = 1;
    if (w && cupidimage_svg_parse_grad_coord(w, &op->rwidth, units))
        has = 1;
    if (h && cupidimage_svg_parse_grad_coord(h, &op->rheight, units))
        has = 1;
    if (has) {
        op->has_region = 1;
    }
}

static void svg_transfer_func_init(svg_transfer_func *func) {
    if (!func) {
        return;
    }
    func->type = SVG_TRANSFER_IDENTITY;
    func->table_count = 0;
    func->slope = 1.0f;
    func->intercept = 0.0f;
    func->amplitude = 1.0f;
    func->exponent = 1.0f;
    func->offset = 0.0f;
    for (int i = 0; i < SVG_MAX_TRANSFER_TABLE; i++) {
        func->table[i] = 0.0f;
    }
}

float cupidimage_svg_transfer_apply(const svg_transfer_func *func, float v) {
    if (!func) {
        return v;
    }
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;
    float out = v;
    switch (func->type) {
        case SVG_TRANSFER_TABLE:
            if (func->table_count > 0) {
                float t = v * (float)(func->table_count - 1);
                int i0 = (int)floorf(t);
                int i1 = i0 + 1;
                if (i0 < 0)
                    i0 = 0;
                if (i1 >= func->table_count)
                    i1 = func->table_count - 1;
                float frac = t - (float)i0;
                float v0 = func->table[i0];
                float v1 = func->table[i1];
                out = v0 + (v1 - v0) * frac;
            }
            break;
        case SVG_TRANSFER_DISCRETE:
            if (func->table_count > 0) {
                int idx = (int)floorf(v * (float)func->table_count);
                if (idx < 0)
                    idx = 0;
                if (idx >= func->table_count)
                    idx = func->table_count - 1;
                out = func->table[idx];
            }
            break;
        case SVG_TRANSFER_LINEAR:
            out = func->slope * v + func->intercept;
            break;
        case SVG_TRANSFER_GAMMA:
            out = func->amplitude * powf(v, func->exponent) + func->offset;
            break;
        case SVG_TRANSFER_IDENTITY:
        default:
            out = v;
            break;
    }
    if (out < 0.0f)
        out = 0.0f;
    if (out > 1.0f)
        out = 1.0f;
    return out;
}

static void svg_parse_transfer_func(const svg_tag *tag, svg_transfer_func *func) {
    if (!tag || !func) {
        return;
    }
    svg_transfer_func_init(func);
    const char *type = cupidimage_svg_get_attr(tag, "type");
    if (!type || svg_strcasecmp(type, "identity") == 0) {
        func->type = SVG_TRANSFER_IDENTITY;
        return;
    }
    if (svg_strcasecmp(type, "table") == 0) {
        func->type = SVG_TRANSFER_TABLE;
        const char *vals = cupidimage_svg_get_attr(tag, "tableValues");
        if (vals) {
            const char *p = vals;
            while (*p && func->table_count < SVG_MAX_TRANSFER_TABLE) {
                float v = 0.0f;
                if (!cupidimage_svg_parse_number(&p, &v)) {
                    break;
                }
                if (v < 0.0f)
                    v = 0.0f;
                if (v > 1.0f)
                    v = 1.0f;
                func->table[func->table_count++] = v;
            }
        }
        return;
    }
    if (svg_strcasecmp(type, "discrete") == 0) {
        func->type = SVG_TRANSFER_DISCRETE;
        const char *vals = cupidimage_svg_get_attr(tag, "tableValues");
        if (vals) {
            const char *p = vals;
            while (*p && func->table_count < SVG_MAX_TRANSFER_TABLE) {
                float v = 0.0f;
                if (!cupidimage_svg_parse_number(&p, &v)) {
                    break;
                }
                if (v < 0.0f)
                    v = 0.0f;
                if (v > 1.0f)
                    v = 1.0f;
                func->table[func->table_count++] = v;
            }
        }
        return;
    }
    if (svg_strcasecmp(type, "linear") == 0) {
        func->type = SVG_TRANSFER_LINEAR;
        const char *slope = cupidimage_svg_get_attr(tag, "slope");
        const char *intercept = cupidimage_svg_get_attr(tag, "intercept");
        if (slope)
            func->slope = (float)strtod(slope, NULL);
        if (intercept)
            func->intercept = (float)strtod(intercept, NULL);
        return;
    }
    if (svg_strcasecmp(type, "gamma") == 0) {
        func->type = SVG_TRANSFER_GAMMA;
        const char *amplitude = cupidimage_svg_get_attr(tag, "amplitude");
        const char *exponent = cupidimage_svg_get_attr(tag, "exponent");
        const char *offset = cupidimage_svg_get_attr(tag, "offset");
        if (amplitude)
            func->amplitude = (float)strtod(amplitude, NULL);
        if (exponent)
            func->exponent = (float)strtod(exponent, NULL);
        if (offset)
            func->offset = (float)strtod(offset, NULL);
        return;
    }
    func->type = SVG_TRANSFER_IDENTITY;
}

int cupidimage_svg_parse_filter_ops(const char *content, svg_filter_op **out_ops, int *out_count,
                                    int *out_result_count, float base_len, float dpi,
                                    svg_gradient_units units) {
    if (!content || !out_ops || !out_count) {
        return 0;
    }
    *out_ops = NULL;
    *out_count = 0;
    if (out_result_count) {
        *out_result_count = 0;
    }
    char *buf = svg_strdup(content);
    if (!buf) {
        return 0;
    }
    char *p = buf;
    svg_tag tag;
    int cap = 0;
    int merge_index = -1;
    int transfer_index = -1;
    int lighting_index = -1;
    char **names = NULL;
    int name_count = 0;
    int name_cap = 0;
    while (cupidimage_svg_next_tag(&p, &tag)) {
        if (tag.is_end) {
            const char *local_end = svg_local_name(tag.name);
            if (merge_index >= 0 && strcmp(local_end, "femerge") == 0) {
                merge_index = -1;
            }
            if (transfer_index >= 0 && strcmp(local_end, "fecomponenttransfer") == 0) {
                transfer_index = -1;
            }
            if (lighting_index >= 0 && (strcmp(local_end, "fediffuselighting") == 0 ||
                                        strcmp(local_end, "fespecularlighting") == 0)) {
                lighting_index = -1;
            }
            continue;
        }
        const char *local = svg_local_name(tag.name);
        if (strcmp(local, "femergenode") == 0 && merge_index >= 0) {
            svg_filter_op *op = &(*out_ops)[merge_index];
            if (op->merge_count < (int)(sizeof(op->merge_inputs) / sizeof(op->merge_inputs[0]))) {
                op->merge_inputs[op->merge_count++] =
                    svg_parse_filter_ref(cupidimage_svg_get_attr(&tag, "in"), names, name_count);
            }
            continue;
        }
        if (strcmp(local, "fefuncr") == 0 && transfer_index >= 0) {
            svg_filter_op *op = &(*out_ops)[transfer_index];
            svg_parse_transfer_func(&tag, &op->tr);
            continue;
        }
        if (strcmp(local, "fefuncg") == 0 && transfer_index >= 0) {
            svg_filter_op *op = &(*out_ops)[transfer_index];
            svg_parse_transfer_func(&tag, &op->tg);
            continue;
        }
        if (strcmp(local, "fefuncb") == 0 && transfer_index >= 0) {
            svg_filter_op *op = &(*out_ops)[transfer_index];
            svg_parse_transfer_func(&tag, &op->tb);
            continue;
        }
        if (strcmp(local, "fefunca") == 0 && transfer_index >= 0) {
            svg_filter_op *op = &(*out_ops)[transfer_index];
            svg_parse_transfer_func(&tag, &op->ta);
            continue;
        }
        if (strcmp(local, "fedistantlight") == 0 && lighting_index >= 0) {
            svg_filter_op *op = &(*out_ops)[lighting_index];
            float az = 0.0f;
            float el = 0.0f;
            const char *azs = cupidimage_svg_get_attr(&tag, "azimuth");
            const char *els = cupidimage_svg_get_attr(&tag, "elevation");
            if (azs)
                az = (float)strtod(azs, NULL);
            if (els)
                el = (float)strtod(els, NULL);
            float azr = az * (float)M_PI / 180.0f;
            float elr = el * (float)M_PI / 180.0f;
            op->light_x = cosf(elr) * cosf(azr);
            op->light_y = cosf(elr) * sinf(azr);
            op->light_z = sinf(elr);
            continue;
        }
        if (strcmp(local, "femerge") == 0) {
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n =
                    (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_MERGE;
            op->merge_count = 0;
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
            merge_index = *out_count - 1;
            continue;
        }
        if (strcmp(local, "fetile") == 0) {
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n =
                    (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_TILE;
            op->in1 = svg_parse_filter_ref(cupidimage_svg_get_attr(&tag, "in"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
            continue;
        }
        if (strcmp(local, "feimage") == 0) {
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n =
                    (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_IMAGE;
            const char *href = cupidimage_svg_get_attr(&tag, "href");
            if (!href) {
                href = cupidimage_svg_get_attr(&tag, "xlink:href");
            }
            if (href && *href) {
                op->image_href = svg_strdup(href);
            }
            int ok = 0;
            const char *xs = cupidimage_svg_get_attr(&tag, "x");
            if (xs) {
                float v = cupidimage_svg_parse_length(xs, base_len, dpi, &ok);
                if (ok) {
                    op->img_x = v;
                    op->has_image_geom = 1;
                }
            }
            const char *ys = cupidimage_svg_get_attr(&tag, "y");
            if (ys) {
                float v = cupidimage_svg_parse_length(ys, base_len, dpi, &ok);
                if (ok) {
                    op->img_y = v;
                    op->has_image_geom = 1;
                }
            }
            const char *ws = cupidimage_svg_get_attr(&tag, "width");
            if (ws) {
                float v = cupidimage_svg_parse_length(ws, base_len, dpi, &ok);
                if (ok) {
                    op->img_w = v;
                    op->has_image_geom = 1;
                }
            }
            const char *hs = cupidimage_svg_get_attr(&tag, "height");
            if (hs) {
                float v = cupidimage_svg_parse_length(hs, base_len, dpi, &ok);
                if (ok) {
                    op->img_h = v;
                    op->has_image_geom = 1;
                }
            }
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
            continue;
        }
        if (strcmp(local, "fecomponenttransfer") == 0) {
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n =
                    (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_COMPONENT_TRANSFER;
            op->in1 = svg_parse_filter_ref(cupidimage_svg_get_attr(&tag, "in"), names, name_count);
            svg_transfer_func_init(&op->tr);
            svg_transfer_func_init(&op->tg);
            svg_transfer_func_init(&op->tb);
            svg_transfer_func_init(&op->ta);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
            if (!tag.is_self_closing) {
                transfer_index = *out_count - 1;
            }
            continue;
        }
        if (strcmp(local, "feconvolvematrix") == 0) {
            const char *order = cupidimage_svg_get_attr(&tag, "order");
            const char *order_x = cupidimage_svg_get_attr(&tag, "orderX");
            const char *order_y = cupidimage_svg_get_attr(&tag, "orderY");
            int ox = 0, oy = 0;
            if (order) {
                const char *op = order;
                float v = 0.0f;
                if (cupidimage_svg_parse_number(&op, &v)) {
                    ox = (int)lroundf(v);
                    if (!cupidimage_svg_parse_number(&op, &v)) {
                        oy = ox;
                    } else {
                        oy = (int)lroundf(v);
                    }
                }
            }
            if (order_x) {
                ox = (int)lroundf(strtod(order_x, NULL));
            }
            if (order_y) {
                oy = (int)lroundf(strtod(order_y, NULL));
            }
            if (ox <= 0)
                ox = 3;
            if (oy <= 0)
                oy = 3;
            int kcount = ox * oy;
            const char *km = cupidimage_svg_get_attr(&tag, "kernelMatrix");
            if (!km) {
                continue;
            }
            float *kernel = (float *)malloc((size_t)kcount * sizeof(float));
            if (!kernel) {
                continue;
            }
            const char *kp = km;
            int filled = 0;
            for (int i = 0; i < kcount; i++) {
                float v = 0.0f;
                if (!cupidimage_svg_parse_number(&kp, &v)) {
                    break;
                }
                kernel[i] = v;
                filled++;
            }
            if (filled < kcount) {
                free(kernel);
                continue;
            }
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n =
                    (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    free(kernel);
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_CONVOLVE;
            op->kernel = kernel;
            op->kernel_count = kcount;
            op->order_x = ox;
            op->order_y = oy;
            const char *txs = cupidimage_svg_get_attr(&tag, "targetX");
            const char *tys = cupidimage_svg_get_attr(&tag, "targetY");
            op->target_x = txs ? (int)lroundf(strtod(txs, NULL)) : ox / 2;
            op->target_y = tys ? (int)lroundf(strtod(tys, NULL)) : oy / 2;
            const char *divs = cupidimage_svg_get_attr(&tag, "divisor");
            if (divs) {
                op->divisor = (float)strtod(divs, NULL);
            } else {
                float sum = 0.0f;
                for (int i = 0; i < kcount; i++) {
                    sum += kernel[i];
                }
                op->divisor = sum;
            }
            if (fabsf(op->divisor) < 1e-6f) {
                op->divisor = 1.0f;
            }
            const char *bias = cupidimage_svg_get_attr(&tag, "bias");
            if (bias) {
                op->bias = (float)strtod(bias, NULL);
            }
            const char *edge = cupidimage_svg_get_attr(&tag, "edgeMode");
            if (edge) {
                if (svg_strcasecmp(edge, "duplicate") == 0)
                    op->mode = SVG_EDGE_DUPLICATE;
                else if (svg_strcasecmp(edge, "wrap") == 0)
                    op->mode = SVG_EDGE_WRAP;
                else
                    op->mode = SVG_EDGE_NONE;
            } else {
                op->mode = SVG_EDGE_NONE;
            }
            const char *pres = cupidimage_svg_get_attr(&tag, "preserveAlpha");
            if (pres && (svg_strcasecmp(pres, "true") == 0 || strcmp(pres, "1") == 0)) {
                op->preserve_alpha = 1;
            }
            op->in1 = svg_parse_filter_ref(cupidimage_svg_get_attr(&tag, "in"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
            continue;
        }
        if (strcmp(local, "feturbulence") == 0) {
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n =
                    (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_TURBULENCE;
            const char *basef = cupidimage_svg_get_attr(&tag, "baseFrequency");
            if (basef) {
                const char *bp = basef;
                float fx = 0.0f, fy = 0.0f;
                if (cupidimage_svg_parse_number(&bp, &fx)) {
                    if (!cupidimage_svg_parse_number(&bp, &fy)) {
                        fy = fx;
                    }
                }
                op->turb_freq_x = fx;
                op->turb_freq_y = fy;
            }
            const char *oct = cupidimage_svg_get_attr(&tag, "numOctaves");
            if (oct) {
                int v = (int)lroundf(strtod(oct, NULL));
                if (v < 1)
                    v = 1;
                if (v > 8)
                    v = 8;
                op->turb_octaves = v;
            }
            const char *seed = cupidimage_svg_get_attr(&tag, "seed");
            if (seed) {
                op->turb_seed = (int)lroundf(strtod(seed, NULL));
            }
            const char *type = cupidimage_svg_get_attr(&tag, "type");
            if (type && svg_strcasecmp(type, "fractalnoise") == 0) {
                op->turb_fractal = 1;
            }
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
            continue;
        }
        if (strcmp(local, "fedisplacementmap") == 0) {
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n =
                    (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_DISPLACEMENT;
            const char *scale = cupidimage_svg_get_attr(&tag, "scale");
            if (scale) {
                op->a = (float)strtod(scale, NULL);
            } else {
                op->a = 0.0f;
            }
            const char *xch = cupidimage_svg_get_attr(&tag, "xChannelSelector");
            const char *ych = cupidimage_svg_get_attr(&tag, "yChannelSelector");
            if (xch) {
                if (svg_strcasecmp(xch, "g") == 0)
                    op->displace_x_channel = SVG_DISPLACE_G;
                else if (svg_strcasecmp(xch, "b") == 0)
                    op->displace_x_channel = SVG_DISPLACE_B;
                else if (svg_strcasecmp(xch, "a") == 0)
                    op->displace_x_channel = SVG_DISPLACE_A;
                else
                    op->displace_x_channel = SVG_DISPLACE_R;
            }
            if (ych) {
                if (svg_strcasecmp(ych, "g") == 0)
                    op->displace_y_channel = SVG_DISPLACE_G;
                else if (svg_strcasecmp(ych, "b") == 0)
                    op->displace_y_channel = SVG_DISPLACE_B;
                else if (svg_strcasecmp(ych, "a") == 0)
                    op->displace_y_channel = SVG_DISPLACE_A;
                else
                    op->displace_y_channel = SVG_DISPLACE_R;
            }
            op->in1 = svg_parse_filter_ref(cupidimage_svg_get_attr(&tag, "in"), names, name_count);
            op->in2 = svg_parse_filter_ref(cupidimage_svg_get_attr(&tag, "in2"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
            continue;
        }
        if (strcmp(local, "fediffuselighting") == 0 || strcmp(local, "fespecularlighting") == 0) {
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n =
                    (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            if (strcmp(local, "fediffuselighting") == 0) {
                op->type = SVG_FILTER_OP_DIFFUSE_LIGHTING;
            } else {
                op->type = SVG_FILTER_OP_SPECULAR_LIGHTING;
            }
            op->in1 = svg_parse_filter_ref(cupidimage_svg_get_attr(&tag, "in"), names, name_count);
            const char *surface = cupidimage_svg_get_attr(&tag, "surfaceScale");
            if (surface)
                op->surface_scale = (float)strtod(surface, NULL);
            const char *diff = cupidimage_svg_get_attr(&tag, "diffuseConstant");
            if (diff)
                op->diffuse_constant = (float)strtod(diff, NULL);
            const char *spec = cupidimage_svg_get_attr(&tag, "specularConstant");
            if (spec)
                op->specular_constant = (float)strtod(spec, NULL);
            const char *exp = cupidimage_svg_get_attr(&tag, "specularExponent");
            if (exp)
                op->specular_exponent = (float)strtod(exp, NULL);
            uint32_t color = 0xFFFFFFFFu;
            int is_none = 0;
            const char *lc = cupidimage_svg_get_attr(&tag, "lighting-color");
            if (lc && cupidimage_svg_parse_color(lc, &color, &is_none) && !is_none) {
                op->color = color;
            } else {
                op->color = 0xFFFFFFFFu;
            }
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
            if (!tag.is_self_closing) {
                lighting_index = *out_count - 1;
            }
            continue;
        }
        if (strcmp(local, "fegaussianblur") == 0) {
            const char *sd = cupidimage_svg_get_attr(&tag, "stdDeviation");
            if (!sd) {
                continue;
            }
            float sx = 0.0f;
            float sy = 0.0f;
            const char *sp = sd;
            if (!cupidimage_svg_parse_number(&sp, &sx)) {
                continue;
            }
            if (!cupidimage_svg_parse_number(&sp, &sy)) {
                sy = sx;
            }
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n =
                    (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_BLUR;
            op->a = sx;
            op->b = sy;
            op->in1 = svg_parse_filter_ref(cupidimage_svg_get_attr(&tag, "in"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
        } else if (strcmp(local, "feoffset") == 0) {
            const char *dxs = cupidimage_svg_get_attr(&tag, "dx");
            const char *dys = cupidimage_svg_get_attr(&tag, "dy");
            int okx = 0, oky = 0;
            float dx = cupidimage_svg_parse_length(dxs, base_len, dpi, &okx);
            float dy = cupidimage_svg_parse_length(dys, base_len, dpi, &oky);
            if (!okx)
                dx = 0.0f;
            if (!oky)
                dy = 0.0f;
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n =
                    (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_OFFSET;
            op->a = dx;
            op->b = dy;
            op->in1 = svg_parse_filter_ref(cupidimage_svg_get_attr(&tag, "in"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
        } else if (strcmp(local, "fecolormatrix") == 0) {
            const char *type = cupidimage_svg_get_attr(&tag, "type");
            const char *vals = cupidimage_svg_get_attr(&tag, "values");
            float mat[20];
            int has_matrix = 0;
            if (!type || svg_strcasecmp(type, "matrix") == 0) {
                if (vals) {
                    const char *vp = vals;
                    for (int i = 0; i < 20; i++) {
                        if (!cupidimage_svg_parse_number(&vp, &mat[i])) {
                            break;
                        }
                        if (i == 19) {
                            has_matrix = 1;
                        }
                    }
                }
            } else if (svg_strcasecmp(type, "saturate") == 0) {
                float s = 1.0f;
                if (vals) {
                    const char *vp = vals;
                    if (!cupidimage_svg_parse_number(&vp, &s)) {
                        s = 1.0f;
                    }
                }
                svg_color_matrix_saturate(s, mat);
                has_matrix = 1;
            } else if (svg_strcasecmp(type, "huerotate") == 0) {
                float a = 0.0f;
                if (vals) {
                    const char *vp = vals;
                    if (!cupidimage_svg_parse_number(&vp, &a)) {
                        a = 0.0f;
                    }
                }
                svg_color_matrix_huerotate(a, mat);
                has_matrix = 1;
            } else if (svg_strcasecmp(type, "luminancetoalpha") == 0) {
                svg_color_matrix_luminance_to_alpha(mat);
                has_matrix = 1;
            }
            if (!has_matrix) {
                continue;
            }
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n =
                    (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_COLOR_MATRIX;
            op->has_matrix = 1;
            memcpy(op->matrix, mat, sizeof(mat));
            op->in1 = svg_parse_filter_ref(cupidimage_svg_get_attr(&tag, "in"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
        } else if (strcmp(local, "feflood") == 0) {
            uint32_t color = 0x000000FFu;
            int is_none = 0;
            const char *col = cupidimage_svg_get_attr(&tag, "flood-color");
            if (col) {
                if (!cupidimage_svg_parse_color(col, &color, &is_none) || is_none) {
                    color = 0x00000000u;
                }
            }
            float opacity = 1.0f;
            const char *fo = cupidimage_svg_get_attr(&tag, "flood-opacity");
            if (fo) {
                cupidimage_svg_parse_opacity_value(fo, &opacity);
            }
            uint8_t a = (uint8_t)(color & 0xFFu);
            float alpha = (a / 255.0f) * opacity;
            if (alpha < 0.0f)
                alpha = 0.0f;
            if (alpha > 1.0f)
                alpha = 1.0f;
            a = (uint8_t)lroundf(alpha * 255.0f);
            color = (color & 0xFFFFFF00u) | a;
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n =
                    (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_FLOOD;
            op->color = color;
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
        } else if (strcmp(local, "feblend") == 0) {
            const char *mode = cupidimage_svg_get_attr(&tag, "mode");
            int blend_mode = SVG_BLEND_NORMAL;
            if (mode) {
                if (svg_strcasecmp(mode, "multiply") == 0)
                    blend_mode = SVG_BLEND_MULTIPLY;
                else if (svg_strcasecmp(mode, "screen") == 0)
                    blend_mode = SVG_BLEND_SCREEN;
                else if (svg_strcasecmp(mode, "darken") == 0)
                    blend_mode = SVG_BLEND_DARKEN;
                else if (svg_strcasecmp(mode, "lighten") == 0)
                    blend_mode = SVG_BLEND_LIGHTEN;
                else if (svg_strcasecmp(mode, "overlay") == 0)
                    blend_mode = SVG_BLEND_OVERLAY;
                else if (svg_strcasecmp(mode, "hard-light") == 0)
                    blend_mode = SVG_BLEND_HARDLIGHT;
                else if (svg_strcasecmp(mode, "soft-light") == 0)
                    blend_mode = SVG_BLEND_SOFTLIGHT;
            }
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n =
                    (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_BLEND;
            op->mode = blend_mode;
            op->in1 = svg_parse_filter_ref(cupidimage_svg_get_attr(&tag, "in"), names, name_count);
            op->in2 = svg_parse_filter_ref(cupidimage_svg_get_attr(&tag, "in2"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
        } else if (strcmp(local, "fecomposite") == 0) {
            const char *opname = cupidimage_svg_get_attr(&tag, "operator");
            int comp = SVG_COMPOSITE_OVER;
            if (opname) {
                if (svg_strcasecmp(opname, "in") == 0)
                    comp = SVG_COMPOSITE_IN;
                else if (svg_strcasecmp(opname, "out") == 0)
                    comp = SVG_COMPOSITE_OUT;
                else if (svg_strcasecmp(opname, "atop") == 0)
                    comp = SVG_COMPOSITE_ATOP;
                else if (svg_strcasecmp(opname, "xor") == 0)
                    comp = SVG_COMPOSITE_XOR;
                else if (svg_strcasecmp(opname, "arithmetic") == 0)
                    comp = SVG_COMPOSITE_ARITHMETIC;
            }
            float k1 = 0.0f, k2 = 0.0f, k3 = 0.0f, k4 = 0.0f;
            const char *k1s = cupidimage_svg_get_attr(&tag, "k1");
            const char *k2s = cupidimage_svg_get_attr(&tag, "k2");
            const char *k3s = cupidimage_svg_get_attr(&tag, "k3");
            const char *k4s = cupidimage_svg_get_attr(&tag, "k4");
            if (k1s)
                k1 = (float)strtod(k1s, NULL);
            if (k2s)
                k2 = (float)strtod(k2s, NULL);
            if (k3s)
                k3 = (float)strtod(k3s, NULL);
            if (k4s)
                k4 = (float)strtod(k4s, NULL);
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n =
                    (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_COMPOSITE;
            op->mode = comp;
            op->a = k1;
            op->b = k2;
            op->c = k3;
            op->d = k4;
            op->in1 = svg_parse_filter_ref(cupidimage_svg_get_attr(&tag, "in"), names, name_count);
            op->in2 = svg_parse_filter_ref(cupidimage_svg_get_attr(&tag, "in2"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
        } else if (strcmp(local, "femorphology") == 0) {
            const char *opname = cupidimage_svg_get_attr(&tag, "operator");
            int morph = SVG_MORPHOLOGY_ERODE;
            if (opname && svg_strcasecmp(opname, "dilate") == 0) {
                morph = SVG_MORPHOLOGY_DILATE;
            }
            float rx = 0.0f, ry = 0.0f;
            const char *rad = cupidimage_svg_get_attr(&tag, "radius");
            if (rad) {
                const char *rp = rad;
                if (cupidimage_svg_parse_length_value(&rp, base_len, dpi, &rx)) {
                    if (!cupidimage_svg_parse_length_value(&rp, base_len, dpi, &ry)) {
                        ry = rx;
                    }
                }
            }
            if (*out_count >= cap) {
                int new_cap = cap ? cap * 2 : 8;
                svg_filter_op *n =
                    (svg_filter_op *)realloc(*out_ops, (size_t)new_cap * sizeof(svg_filter_op));
                if (!n) {
                    break;
                }
                *out_ops = n;
                cap = new_cap;
            }
            svg_filter_op *op = &(*out_ops)[*out_count];
            svg_filter_op_init(op);
            op->type = SVG_FILTER_OP_MORPHOLOGY;
            op->mode = morph;
            op->a = rx;
            op->b = ry;
            op->in1 = svg_parse_filter_ref(cupidimage_svg_get_attr(&tag, "in"), names, name_count);
            op->result_id = -1;
            svg_filter_assign_result(&tag, op, &names, &name_count, &name_cap);
            svg_filter_parse_region(&tag, op, units);
            (*out_count)++;
        }
    }
    for (int i = 0; i < name_count; i++) {
        free(names[i]);
    }
    free(names);
    free(buf);
    if (out_result_count) {
        *out_result_count = name_count;
    }
    return *out_count > 0;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
