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

static int svg_tag_is_use_def_shape(const char *local);
static int svg_parse_stop_offset(const char *s, float *out);
static void svg_stop_apply_style(const char *style, uint32_t *color, int *color_set, float *opacity,
                                 int *opacity_set);
static int svg_stop_offset_cmp(const void *a, const void *b);
static void svg_gradient_sort_stops(svg_gradient *g);

void cupidimage_svg_defs_init(svg_defs *defs) {
    if (!defs) {
        return;
    }
    defs->gradients = NULL;
    defs->gradient_count = 0;
    defs->gradient_cap = 0;
    defs->patterns = NULL;
    defs->pattern_count = 0;
    defs->pattern_cap = 0;
    defs->paths = NULL;
    defs->path_count = 0;
    defs->path_cap = 0;
    defs->uses = NULL;
    defs->use_count = 0;
    defs->use_cap = 0;
    defs->symbols = NULL;
    defs->symbol_count = 0;
    defs->symbol_cap = 0;
    defs->markers = NULL;
    defs->marker_count = 0;
    defs->marker_cap = 0;
    defs->clips = NULL;
    defs->clip_count = 0;
    defs->clip_cap = 0;
    defs->masks = NULL;
    defs->mask_count = 0;
    defs->mask_cap = 0;
    defs->filters = NULL;
    defs->filter_count = 0;
    defs->filter_cap = 0;
}

void cupidimage_svg_defs_free(svg_defs *defs) {
    if (!defs) {
        return;
    }
    for (int i = 0; i < defs->gradient_count; i++) {
        free(defs->gradients[i].id);
        free(defs->gradients[i].href);
        free(defs->gradients[i].stops);
    }
    for (int i = 0; i < defs->pattern_count; i++) {
        free(defs->patterns[i].id);
        free(defs->patterns[i].href);
        free(defs->patterns[i].content);
        free(defs->patterns[i].rgba);
    }
    for (int i = 0; i < defs->path_count; i++) {
        free(defs->paths[i].id);
        free(defs->paths[i].d);
    }
    for (int i = 0; i < defs->use_count; i++) {
        free(defs->uses[i].id);
        free(defs->uses[i].tag_name);
        for (int j = 0; j < defs->uses[i].attr_count; j++) {
            free(defs->uses[i].attrs[j].name);
            free(defs->uses[i].attrs[j].value);
        }
    }
    for (int i = 0; i < defs->symbol_count; i++) {
        free(defs->symbols[i].id);
        free(defs->symbols[i].content);
    }
    for (int i = 0; i < defs->marker_count; i++) {
        free(defs->markers[i].id);
        free(defs->markers[i].content);
    }
    for (int i = 0; i < defs->clip_count; i++) {
        free(defs->clips[i].id);
        free(defs->clips[i].href);
        free(defs->clips[i].content);
        free(defs->clips[i].rgba);
    }
    for (int i = 0; i < defs->mask_count; i++) {
        free(defs->masks[i].id);
        free(defs->masks[i].href);
        free(defs->masks[i].content);
        free(defs->masks[i].rgba);
    }
    for (int i = 0; i < defs->filter_count; i++) {
        free(defs->filters[i].id);
        free(defs->filters[i].content);
    }
    free(defs->gradients);
    defs->gradients = NULL;
    defs->gradient_count = 0;
    defs->gradient_cap = 0;
    free(defs->patterns);
    defs->patterns = NULL;
    defs->pattern_count = 0;
    defs->pattern_cap = 0;
    free(defs->paths);
    defs->paths = NULL;
    defs->path_count = 0;
    defs->path_cap = 0;
    free(defs->uses);
    defs->uses = NULL;
    defs->use_count = 0;
    defs->use_cap = 0;
    free(defs->symbols);
    defs->symbols = NULL;
    defs->symbol_count = 0;
    defs->symbol_cap = 0;
    free(defs->markers);
    defs->markers = NULL;
    defs->marker_count = 0;
    defs->marker_cap = 0;
    free(defs->clips);
    defs->clips = NULL;
    defs->clip_count = 0;
    defs->clip_cap = 0;
    free(defs->masks);
    defs->masks = NULL;
    defs->mask_count = 0;
    defs->mask_cap = 0;
    free(defs->filters);
    defs->filters = NULL;
    defs->filter_count = 0;
    defs->filter_cap = 0;
}

svg_gradient *cupidimage_svg_defs_add_gradient(svg_defs *defs, const char *id,
                                               svg_paint_type type) {
    if (!defs || !id || !*id) {
        return NULL;
    }
    if (defs->gradient_count >= defs->gradient_cap) {
        int new_cap = defs->gradient_cap ? defs->gradient_cap * 2 : 16;
        svg_gradient *n =
            (svg_gradient *)realloc(defs->gradients, (size_t)new_cap * sizeof(svg_gradient));
        if (!n) {
            return NULL;
        }
        defs->gradients = n;
        defs->gradient_cap = new_cap;
    }
    svg_gradient *g = &defs->gradients[defs->gradient_count++];
    memset(g, 0, sizeof(*g));
    g->id = svg_strdup(id);
    g->type = type;
    g->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
    g->spread = SVG_SPREAD_PAD;
    svg_matrix_identity(&g->transform);
    svg_matrix_identity(&g->inv_transform);
    g->has_transform = 0;
    g->has_units = 0;
    g->has_spread = 0;
    g->has_coords = 0;
    g->has_focal = 0;
    if (type == SVG_PAINT_LINEAR_GRADIENT) {
        g->x1.value = 0.0f;
        g->y1.value = 0.0f;
        g->x2.value = 1.0f;
        g->y2.value = 0.0f;
        g->x1.is_percent = 1;
        g->y1.is_percent = 1;
        g->x2.is_percent = 1;
        g->y2.is_percent = 1;
    } else {
        g->cx.value = 0.5f;
        g->cy.value = 0.5f;
        g->r.value = 0.5f;
        g->fx.value = 0.5f;
        g->fy.value = 0.5f;
        g->cx.is_percent = 1;
        g->cy.is_percent = 1;
        g->r.is_percent = 1;
        g->fx.is_percent = 1;
        g->fy.is_percent = 1;
    }
    return g;
}

const svg_gradient *cupidimage_svg_defs_find_gradient(const svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->gradient_count; i++) {
        if (defs->gradients[i].id && strcmp(defs->gradients[i].id, id) == 0) {
            return &defs->gradients[i];
        }
    }
    return NULL;
}

svg_pattern *cupidimage_svg_defs_add_pattern(svg_defs *defs, const char *id) {
    if (!defs || !id || !*id) {
        return NULL;
    }
    if (defs->pattern_count >= defs->pattern_cap) {
        int new_cap = defs->pattern_cap ? defs->pattern_cap * 2 : 16;
        svg_pattern *n =
            (svg_pattern *)realloc(defs->patterns, (size_t)new_cap * sizeof(svg_pattern));
        if (!n) {
            return NULL;
        }
        defs->patterns = n;
        defs->pattern_cap = new_cap;
    }
    svg_pattern *p = &defs->patterns[defs->pattern_count++];
    memset(p, 0, sizeof(*p));
    p->id = svg_strdup(id);
    p->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
    p->content_units = SVG_GRADIENT_UNITS_USER_SPACE;
    p->has_units = 0;
    p->has_content_units = 0;
    p->has_coords = 0;
    p->x.value = 0.0f;
    p->y.value = 0.0f;
    p->width.value = 0.0f;
    p->height.value = 0.0f;
    p->x.is_percent = 1;
    p->y.is_percent = 1;
    p->width.is_percent = 1;
    p->height.is_percent = 1;
    svg_matrix_identity(&p->transform);
    svg_matrix_identity(&p->inv_transform);
    p->has_transform = 0;
    p->content = NULL;
    p->rgba = NULL;
    p->tile_w = 0;
    p->tile_h = 0;
    p->cached_bbox_x = 0.0f;
    p->cached_bbox_y = 0.0f;
    p->cached_bbox_w = 0.0f;
    p->cached_bbox_h = 0.0f;
    p->has_cached_bbox = 0;
    p->rendered = 0;
    p->rendering = 0;
    return p;
}

const svg_pattern *cupidimage_svg_defs_find_pattern(const svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->pattern_count; i++) {
        if (defs->patterns[i].id && strcmp(defs->patterns[i].id, id) == 0) {
            return &defs->patterns[i];
        }
    }
    return NULL;
}

svg_clip_path *cupidimage_svg_defs_add_clip(svg_defs *defs, const char *id) {
    if (!defs || !id || !*id) {
        return NULL;
    }
    if (defs->clip_count >= defs->clip_cap) {
        int new_cap = defs->clip_cap ? defs->clip_cap * 2 : 16;
        svg_clip_path *n =
            (svg_clip_path *)realloc(defs->clips, (size_t)new_cap * sizeof(svg_clip_path));
        if (!n) {
            return NULL;
        }
        defs->clips = n;
        defs->clip_cap = new_cap;
    }
    svg_clip_path *c = &defs->clips[defs->clip_count++];
    memset(c, 0, sizeof(*c));
    c->id = svg_strdup(id);
    c->units = SVG_GRADIENT_UNITS_USER_SPACE;
    c->has_units = 0;
    svg_matrix_identity(&c->transform);
    svg_matrix_identity(&c->inv_transform);
    c->has_transform = 0;
    c->content = NULL;
    c->rgba = NULL;
    c->mask_w = 0;
    c->mask_h = 0;
    c->cached_bbox_x = 0.0f;
    c->cached_bbox_y = 0.0f;
    c->cached_bbox_w = 0.0f;
    c->cached_bbox_h = 0.0f;
    c->has_cached_bbox = 0;
    c->rendered = 0;
    c->rendering = 0;
    return c;
}

svg_mask *cupidimage_svg_defs_add_mask(svg_defs *defs, const char *id) {
    if (!defs || !id || !*id) {
        return NULL;
    }
    if (defs->mask_count >= defs->mask_cap) {
        int new_cap = defs->mask_cap ? defs->mask_cap * 2 : 16;
        svg_mask *n = (svg_mask *)realloc(defs->masks, (size_t)new_cap * sizeof(svg_mask));
        if (!n) {
            return NULL;
        }
        defs->masks = n;
        defs->mask_cap = new_cap;
    }
    svg_mask *m = &defs->masks[defs->mask_count++];
    memset(m, 0, sizeof(*m));
    m->id = svg_strdup(id);
    m->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
    m->content_units = SVG_GRADIENT_UNITS_USER_SPACE;
    m->has_units = 0;
    m->has_content_units = 0;
    m->has_coords = 0;
    m->x.value = -0.1f;
    m->y.value = -0.1f;
    m->width.value = 1.2f;
    m->height.value = 1.2f;
    m->x.is_percent = 1;
    m->y.is_percent = 1;
    m->width.is_percent = 1;
    m->height.is_percent = 1;
    svg_matrix_identity(&m->transform);
    svg_matrix_identity(&m->inv_transform);
    m->has_transform = 0;
    m->type = SVG_MASK_TYPE_LUMINANCE;
    m->has_type = 0;
    m->content = NULL;
    m->rgba = NULL;
    m->mask_w = 0;
    m->mask_h = 0;
    m->cached_bbox_x = 0.0f;
    m->cached_bbox_y = 0.0f;
    m->cached_bbox_w = 0.0f;
    m->cached_bbox_h = 0.0f;
    m->has_cached_bbox = 0;
    m->rendered = 0;
    m->rendering = 0;
    return m;
}

svg_filter *cupidimage_svg_defs_add_filter(svg_defs *defs, const char *id) {
    if (!defs || !id || !*id) {
        return NULL;
    }
    if (defs->filter_count >= defs->filter_cap) {
        int new_cap = defs->filter_cap ? defs->filter_cap * 2 : 16;
        svg_filter *n = (svg_filter *)realloc(defs->filters, (size_t)new_cap * sizeof(svg_filter));
        if (!n) {
            return NULL;
        }
        defs->filters = n;
        defs->filter_cap = new_cap;
    }
    svg_filter *f = &defs->filters[defs->filter_count++];
    memset(f, 0, sizeof(*f));
    f->id = svg_strdup(id);
    f->content = NULL;
    f->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
    f->has_units = 0;
    f->has_coords = 0;
    f->x.value = -0.1f;
    f->y.value = -0.1f;
    f->width.value = 1.2f;
    f->height.value = 1.2f;
    f->x.is_percent = 1;
    f->y.is_percent = 1;
    f->width.is_percent = 1;
    f->height.is_percent = 1;
    return f;
}

svg_clip_path *cupidimage_svg_defs_find_clip(svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->clip_count; i++) {
        if (defs->clips[i].id && strcmp(defs->clips[i].id, id) == 0) {
            return &defs->clips[i];
        }
    }
    return NULL;
}

svg_mask *cupidimage_svg_defs_find_mask(svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->mask_count; i++) {
        if (defs->masks[i].id && strcmp(defs->masks[i].id, id) == 0) {
            return &defs->masks[i];
        }
    }
    return NULL;
}

const svg_filter *cupidimage_svg_defs_find_filter(const svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->filter_count; i++) {
        if (defs->filters[i].id && strcmp(defs->filters[i].id, id) == 0) {
            return &defs->filters[i];
        }
    }
    return NULL;
}

svg_path_def *cupidimage_svg_defs_add_path(svg_defs *defs, const char *id, const char *d) {
    if (!defs || !id || !*id || !d) {
        return NULL;
    }
    for (int i = 0; i < defs->path_count; i++) {
        if (defs->paths[i].id && strcmp(defs->paths[i].id, id) == 0) {
            free(defs->paths[i].d);
            defs->paths[i].d = svg_strdup(d);
            return &defs->paths[i];
        }
    }
    if (defs->path_count >= defs->path_cap) {
        int new_cap = defs->path_cap ? defs->path_cap * 2 : 16;
        svg_path_def *n =
            (svg_path_def *)realloc(defs->paths, (size_t)new_cap * sizeof(svg_path_def));
        if (!n) {
            return NULL;
        }
        defs->paths = n;
        defs->path_cap = new_cap;
    }
    svg_path_def *pd = &defs->paths[defs->path_count++];
    pd->id = svg_strdup(id);
    pd->d = svg_strdup(d);
    return pd;
}

const svg_path_def *cupidimage_svg_defs_find_path(const svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->path_count; i++) {
        if (defs->paths[i].id && strcmp(defs->paths[i].id, id) == 0) {
            return &defs->paths[i];
        }
    }
    return NULL;
}

static int svg_tag_is_use_def_shape(const char *local) {
    if (!local) {
        return 0;
    }
    return strcmp(local, "rect") == 0 || strcmp(local, "circle") == 0 ||
           strcmp(local, "ellipse") == 0 || strcmp(local, "line") == 0 ||
           strcmp(local, "polyline") == 0 || strcmp(local, "polygon") == 0 ||
           strcmp(local, "path") == 0;
}

svg_use_def *cupidimage_svg_defs_find_use(svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->use_count; i++) {
        if (defs->uses[i].id && strcmp(defs->uses[i].id, id) == 0) {
            return &defs->uses[i];
        }
    }
    return NULL;
}

svg_use_def *cupidimage_svg_defs_add_use(svg_defs *defs, const svg_tag *tag) {
    if (!defs || !tag) {
        return NULL;
    }
    const char *id = cupidimage_svg_get_attr(tag, "id");
    const char *local = svg_local_name(tag->name);
    if (!id || !*id || !svg_tag_is_use_def_shape(local)) {
        return NULL;
    }
    svg_use_def *ud = cupidimage_svg_defs_find_use(defs, id);
    if (!ud) {
        if (defs->use_count >= defs->use_cap) {
            int new_cap = defs->use_cap ? defs->use_cap * 2 : 16;
            svg_use_def *n =
                (svg_use_def *)realloc(defs->uses, (size_t)new_cap * sizeof(svg_use_def));
            if (!n) {
                return NULL;
            }
            defs->uses = n;
            defs->use_cap = new_cap;
        }
        ud = &defs->uses[defs->use_count++];
        memset(ud, 0, sizeof(*ud));
        ud->id = svg_strdup(id);
    } else {
        free(ud->tag_name);
        ud->tag_name = NULL;
        for (int i = 0; i < ud->attr_count; i++) {
            free(ud->attrs[i].name);
            free(ud->attrs[i].value);
            ud->attrs[i].name = NULL;
            ud->attrs[i].value = NULL;
        }
        ud->attr_count = 0;
    }
    ud->tag_name = svg_strdup(local);
    if (!ud->tag_name) {
        return NULL;
    }
    ud->attr_count = tag->attr_count;
    if (ud->attr_count > SVG_MAX_ATTR) {
        ud->attr_count = SVG_MAX_ATTR;
    }
    for (int i = 0; i < ud->attr_count; i++) {
        ud->attrs[i].name = svg_strdup(tag->attrs[i].name ? tag->attrs[i].name : "");
        ud->attrs[i].value = svg_strdup(tag->attrs[i].value ? tag->attrs[i].value : "");
        if (!ud->attrs[i].name || !ud->attrs[i].value) {
            for (int j = 0; j <= i; j++) {
                free(ud->attrs[j].name);
                free(ud->attrs[j].value);
                ud->attrs[j].name = NULL;
                ud->attrs[j].value = NULL;
            }
            ud->attr_count = 0;
            return NULL;
        }
    }
    return ud;
}

svg_symbol *cupidimage_svg_defs_add_symbol(svg_defs *defs, const char *id) {
    if (!defs || !id || !*id) {
        return NULL;
    }
    for (int i = 0; i < defs->symbol_count; i++) {
        if (defs->symbols[i].id && strcmp(defs->symbols[i].id, id) == 0) {
            return &defs->symbols[i];
        }
    }
    if (defs->symbol_count >= defs->symbol_cap) {
        int new_cap = defs->symbol_cap ? defs->symbol_cap * 2 : 8;
        svg_symbol *n = (svg_symbol *)realloc(defs->symbols, (size_t)new_cap * sizeof(svg_symbol));
        if (!n) {
            return NULL;
        }
        defs->symbols = n;
        defs->symbol_cap = new_cap;
    }
    svg_symbol *sym = &defs->symbols[defs->symbol_count++];
    memset(sym, 0, sizeof(*sym));
    sym->id = svg_strdup(id);
    sym->align_x = 0.5f;
    sym->align_y = 0.5f;
    return sym;
}

const svg_symbol *cupidimage_svg_defs_find_symbol(const svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->symbol_count; i++) {
        if (defs->symbols[i].id && strcmp(defs->symbols[i].id, id) == 0) {
            return &defs->symbols[i];
        }
    }
    return NULL;
}

svg_marker *cupidimage_svg_defs_add_marker(svg_defs *defs, const char *id) {
    if (!defs || !id || !*id) {
        return NULL;
    }
    for (int i = 0; i < defs->marker_count; i++) {
        if (defs->markers[i].id && strcmp(defs->markers[i].id, id) == 0) {
            return &defs->markers[i];
        }
    }
    if (defs->marker_count >= defs->marker_cap) {
        int new_cap = defs->marker_cap ? defs->marker_cap * 2 : 8;
        svg_marker *n = (svg_marker *)realloc(defs->markers, (size_t)new_cap * sizeof(svg_marker));
        if (!n) {
            return NULL;
        }
        defs->markers = n;
        defs->marker_cap = new_cap;
    }
    svg_marker *mk = &defs->markers[defs->marker_count++];
    memset(mk, 0, sizeof(*mk));
    mk->id = svg_strdup(id);
    mk->ref_x = 0.0f;
    mk->ref_y = 0.0f;
    mk->marker_w = 3.0f;
    mk->marker_h = 3.0f;
    mk->units_stroke_width = 1;
    mk->orient_auto = 0;
    mk->orient_auto_start_reverse = 0;
    mk->orient_angle = 0.0f;
    mk->align_x = 0.5f;
    mk->align_y = 0.5f;
    return mk;
}

svg_marker *cupidimage_svg_defs_find_marker(svg_defs *defs, const char *id) {
    if (!defs || !id) {
        return NULL;
    }
    for (int i = 0; i < defs->marker_count; i++) {
        if (defs->markers[i].id && strcmp(defs->markers[i].id, id) == 0) {
            return &defs->markers[i];
        }
    }
    return NULL;
}

int cupidimage_svg_parse_grad_coord(const char *s, svg_grad_coord *out, svg_gradient_units units) {
    if (!out || !s) {
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
    int percent = 0;
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end == '%') {
        percent = 1;
        end++;
    }
    if (units == SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX) {
        out->value = percent ? (float)(v / 100.0) : (float)v;
        out->is_percent = 1;
    } else {
        out->value = percent ? (float)(v / 100.0) : (float)v;
        out->is_percent = percent;
    }
    return 1;
}

static int svg_parse_stop_offset(const char *s, float *out) {
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
    int percent = 0;
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (*end == '%') {
        percent = 1;
    }
    float off = percent ? (float)(v / 100.0) : (float)v;
    if (off < 0.0f)
        off = 0.0f;
    if (off > 1.0f)
        off = 1.0f;
    *out = off;
    return 1;
}

static void svg_stop_apply_style(const char *style, uint32_t *color, int *color_set, float *opacity,
                                 int *opacity_set) {
    if (!style) {
        return;
    }
    const char *p = style;
    while (*p) {
        while (isspace((unsigned char)*p) || *p == ';') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *name_start = p;
        while (*p && *p != ':' && *p != ';') {
            p++;
        }
        if (*p != ':') {
            break;
        }
        const char *name_end = p;
        p++;
        const char *value_start = p;
        while (*p && *p != ';') {
            p++;
        }
        const char *value_end = p;
        while (name_end > name_start && isspace((unsigned char)name_end[-1])) {
            name_end--;
        }
        while (value_end > value_start && isspace((unsigned char)value_end[-1])) {
            value_end--;
        }
        if (name_end > name_start && value_end > value_start) {
            char name_buf[64];
            size_t nlen = (size_t)(name_end - name_start);
            if (nlen >= sizeof(name_buf)) {
                nlen = sizeof(name_buf) - 1;
            }
            memcpy(name_buf, name_start, nlen);
            name_buf[nlen] = '\0';
            svg_lowercase(name_buf);
            char value_buf[128];
            size_t vlen = (size_t)(value_end - value_start);
            if (vlen >= sizeof(value_buf)) {
                vlen = sizeof(value_buf) - 1;
            }
            memcpy(value_buf, value_start, vlen);
            value_buf[vlen] = '\0';
            if (strcmp(name_buf, "stop-color") == 0) {
                uint32_t c = 0;
                int is_none = 0;
                if (cupidimage_svg_parse_color(value_buf, &c, &is_none) && !is_none) {
                    *color = c;
                    *color_set = 1;
                }
            } else if (strcmp(name_buf, "stop-opacity") == 0) {
                float o = 0.0f;
                if (cupidimage_svg_parse_opacity_value(value_buf, &o)) {
                    *opacity = o;
                    *opacity_set = 1;
                }
            }
        }
        if (*p == ';') {
            p++;
        }
    }
}

int cupidimage_svg_gradient_add_stop(svg_gradient *g, float offset, uint32_t color) {
    if (!g) {
        return 0;
    }
    int idx = g->stop_count;
    svg_gradient_stop *n =
        (svg_gradient_stop *)realloc(g->stops, (size_t)(idx + 1) * sizeof(svg_gradient_stop));
    if (!n) {
        return 0;
    }
    g->stops = n;
    g->stops[idx].offset = offset;
    g->stops[idx].color = color;
    g->stop_count++;
    return 1;
}

int cupidimage_svg_stop_parse(const svg_tag *tag, float *offset, uint32_t *color) {
    if (!tag || !offset || !color) {
        return 0;
    }
    float off = 0.0f;
    int off_ok = 0;
    const char *offset_attr = cupidimage_svg_get_attr(tag, "offset");
    if (offset_attr) {
        off_ok = svg_parse_stop_offset(offset_attr, &off);
    }
    if (!off_ok) {
        off = 0.0f;
    }
    uint32_t stop_color = 0x000000FFu;
    int color_set = 0;
    float stop_opacity = 1.0f;
    int opacity_set = 0;
    const char *color_attr = cupidimage_svg_get_attr(tag, "stop-color");
    if (color_attr) {
        uint32_t c = 0;
        int is_none = 0;
        if (cupidimage_svg_parse_color(color_attr, &c, &is_none) && !is_none) {
            stop_color = c;
            color_set = 1;
        }
    }
    const char *opacity_attr = cupidimage_svg_get_attr(tag, "stop-opacity");
    if (opacity_attr) {
        float o = 0.0f;
        if (cupidimage_svg_parse_opacity_value(opacity_attr, &o)) {
            stop_opacity = o;
            opacity_set = 1;
        }
    }
    const char *style_attr = cupidimage_svg_get_attr(tag, "style");
    if (style_attr) {
        svg_stop_apply_style(style_attr, &stop_color, &color_set, &stop_opacity, &opacity_set);
    }
    uint8_t a = (uint8_t)(stop_color & 0xFFu);
    float alpha = (a / 255.0f) * stop_opacity;
    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;
    a = (uint8_t)lroundf(alpha * 255.0f);
    stop_color = (stop_color & 0xFFFFFF00u) | a;
    *offset = off;
    *color = stop_color;
    (void)color_set;
    (void)opacity_set;
    return 1;
}

static int svg_stop_offset_cmp(const void *a, const void *b) {
    const svg_gradient_stop *sa = (const svg_gradient_stop *)a;
    const svg_gradient_stop *sb = (const svg_gradient_stop *)b;
    if (sa->offset < sb->offset)
        return -1;
    if (sa->offset > sb->offset)
        return 1;
    return 0;
}

static void svg_gradient_sort_stops(svg_gradient *g) {
    if (g && g->stop_count > 1) {
        qsort(g->stops, (size_t)g->stop_count, sizeof(svg_gradient_stop), svg_stop_offset_cmp);
    }
}

void cupidimage_svg_gradient_inherit(svg_gradient *g, const svg_defs *defs) {
    if (!g || !g->href || !defs) {
        return;
    }
    const svg_gradient *ref = cupidimage_svg_defs_find_gradient(defs, g->href);
    if (!ref || ref == g) {
        return;
    }
    if (!g->has_units) {
        g->units = ref->units;
    }
    if (!g->has_spread) {
        g->spread = ref->spread;
    }
    if (!g->has_transform) {
        g->transform = ref->transform;
        g->inv_transform = ref->inv_transform;
        g->has_transform = ref->has_transform;
    }
    if (!g->has_coords) {
        if (g->type == SVG_PAINT_LINEAR_GRADIENT && ref->type == SVG_PAINT_LINEAR_GRADIENT) {
            g->x1 = ref->x1;
            g->y1 = ref->y1;
            g->x2 = ref->x2;
            g->y2 = ref->y2;
        } else if (g->type == SVG_PAINT_RADIAL_GRADIENT && ref->type == SVG_PAINT_RADIAL_GRADIENT) {
            g->cx = ref->cx;
            g->cy = ref->cy;
            g->r = ref->r;
        }
    }
    if (!g->has_focal && g->type == SVG_PAINT_RADIAL_GRADIENT &&
        ref->type == SVG_PAINT_RADIAL_GRADIENT) {
        g->fx = ref->fx;
        g->fy = ref->fy;
    }
    if (g->stop_count == 0 && ref->stop_count > 0) {
        g->stops = (svg_gradient_stop *)malloc((size_t)ref->stop_count * sizeof(svg_gradient_stop));
        if (!g->stops) {
            return;
        }
        memcpy(g->stops, ref->stops, (size_t)ref->stop_count * sizeof(svg_gradient_stop));
        g->stop_count = ref->stop_count;
    }
}

void cupidimage_svg_gradient_finalize(svg_gradient *g) {
    if (!g) {
        return;
    }
    if (g->has_transform) {
        if (!svg_matrix_invert(&g->transform, &g->inv_transform)) {
            g->has_transform = 0;
        }
    }
    svg_gradient_sort_stops(g);
}

void cupidimage_svg_parse_gradient_tag(const svg_tag *tag, svg_gradient *g) {
    if (!tag || !g) {
        return;
    }
    const char *units = cupidimage_svg_get_attr(tag, "gradientUnits");
    if (units) {
        if (svg_strcasecmp(units, "userSpaceOnUse") == 0) {
            g->units = SVG_GRADIENT_UNITS_USER_SPACE;
            g->has_units = 1;
        } else if (svg_strcasecmp(units, "objectBoundingBox") == 0) {
            g->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
            g->has_units = 1;
        }
    }
    const char *spread = cupidimage_svg_get_attr(tag, "spreadMethod");
    if (spread) {
        if (svg_strcasecmp(spread, "reflect") == 0) {
            g->spread = SVG_SPREAD_REFLECT;
            g->has_spread = 1;
        } else if (svg_strcasecmp(spread, "repeat") == 0) {
            g->spread = SVG_SPREAD_REPEAT;
            g->has_spread = 1;
        } else if (svg_strcasecmp(spread, "pad") == 0) {
            g->spread = SVG_SPREAD_PAD;
            g->has_spread = 1;
        }
    }
    const char *transform = cupidimage_svg_get_attr(tag, "gradientTransform");
    if (transform) {
        svg_matrix t;
        if (cupidimage_svg_parse_transform(transform, &t)) {
            g->transform = t;
            g->has_transform = 1;
        }
    }
    const char *href = cupidimage_svg_get_attr(tag, "href");
    char href_id[96];
    if (cupidimage_svg_parse_href_id(href, href_id, sizeof(href_id))) {
        free(g->href);
        g->href = svg_strdup(href_id);
    }
    if (g->type == SVG_PAINT_LINEAR_GRADIENT) {
        int has = 0;
        const char *x1 = cupidimage_svg_get_attr(tag, "x1");
        const char *y1 = cupidimage_svg_get_attr(tag, "y1");
        const char *x2 = cupidimage_svg_get_attr(tag, "x2");
        const char *y2 = cupidimage_svg_get_attr(tag, "y2");
        if (x1 && cupidimage_svg_parse_grad_coord(x1, &g->x1, g->units))
            has = 1;
        if (y1 && cupidimage_svg_parse_grad_coord(y1, &g->y1, g->units))
            has = 1;
        if (x2 && cupidimage_svg_parse_grad_coord(x2, &g->x2, g->units))
            has = 1;
        if (y2 && cupidimage_svg_parse_grad_coord(y2, &g->y2, g->units))
            has = 1;
        if (has)
            g->has_coords = 1;
    } else {
        int has = 0;
        const char *cx = cupidimage_svg_get_attr(tag, "cx");
        const char *cy = cupidimage_svg_get_attr(tag, "cy");
        const char *r = cupidimage_svg_get_attr(tag, "r");
        const char *fx = cupidimage_svg_get_attr(tag, "fx");
        const char *fy = cupidimage_svg_get_attr(tag, "fy");
        if (cx && cupidimage_svg_parse_grad_coord(cx, &g->cx, g->units))
            has = 1;
        if (cy && cupidimage_svg_parse_grad_coord(cy, &g->cy, g->units))
            has = 1;
        if (r && cupidimage_svg_parse_grad_coord(r, &g->r, g->units))
            has = 1;
        if (fx && cupidimage_svg_parse_grad_coord(fx, &g->fx, g->units)) {
            g->has_focal = 1;
        }
        if (fy && cupidimage_svg_parse_grad_coord(fy, &g->fy, g->units)) {
            g->has_focal = 1;
        }
        if (has)
            g->has_coords = 1;
    }
}

void cupidimage_svg_parse_pattern_tag(const svg_tag *tag, svg_pattern *p) {
    if (!tag || !p) {
        return;
    }
    const char *units = cupidimage_svg_get_attr(tag, "patternUnits");
    if (units) {
        if (svg_strcasecmp(units, "userSpaceOnUse") == 0) {
            p->units = SVG_GRADIENT_UNITS_USER_SPACE;
            p->has_units = 1;
        } else if (svg_strcasecmp(units, "objectBoundingBox") == 0) {
            p->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
            p->has_units = 1;
        }
    }
    const char *content_units = cupidimage_svg_get_attr(tag, "patternContentUnits");
    if (content_units) {
        if (svg_strcasecmp(content_units, "userSpaceOnUse") == 0) {
            p->content_units = SVG_GRADIENT_UNITS_USER_SPACE;
            p->has_content_units = 1;
        } else if (svg_strcasecmp(content_units, "objectBoundingBox") == 0) {
            p->content_units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
            p->has_content_units = 1;
        }
    }
    const char *transform = cupidimage_svg_get_attr(tag, "patternTransform");
    if (transform) {
        svg_matrix t;
        if (cupidimage_svg_parse_transform(transform, &t)) {
            p->transform = t;
            p->has_transform = 1;
        }
    }
    const char *href = cupidimage_svg_get_attr(tag, "href");
    char href_id[96];
    if (cupidimage_svg_parse_href_id(href, href_id, sizeof(href_id))) {
        free(p->href);
        p->href = svg_strdup(href_id);
    }
    int has = 0;
    const char *x = cupidimage_svg_get_attr(tag, "x");
    const char *y = cupidimage_svg_get_attr(tag, "y");
    const char *w = cupidimage_svg_get_attr(tag, "width");
    const char *h = cupidimage_svg_get_attr(tag, "height");
    if (x && cupidimage_svg_parse_grad_coord(x, &p->x, p->units))
        has = 1;
    if (y && cupidimage_svg_parse_grad_coord(y, &p->y, p->units))
        has = 1;
    if (w && cupidimage_svg_parse_grad_coord(w, &p->width, p->units))
        has = 1;
    if (h && cupidimage_svg_parse_grad_coord(h, &p->height, p->units))
        has = 1;
    if (has) {
        p->has_coords = 1;
    }
}

void cupidimage_svg_pattern_inherit(svg_pattern *p, const svg_defs *defs) {
    if (!p || !p->href || !defs) {
        return;
    }
    const svg_pattern *ref = cupidimage_svg_defs_find_pattern(defs, p->href);
    if (!ref || ref == p) {
        return;
    }
    if (!p->has_units) {
        p->units = ref->units;
    }
    if (!p->has_content_units) {
        p->content_units = ref->content_units;
    }
    if (!p->has_transform) {
        p->transform = ref->transform;
        p->inv_transform = ref->inv_transform;
        p->has_transform = ref->has_transform;
    }
    if (!p->has_coords) {
        p->x = ref->x;
        p->y = ref->y;
        p->width = ref->width;
        p->height = ref->height;
    }
    if (!p->content && ref->content) {
        p->content = svg_strdup(ref->content);
    }
}

void cupidimage_svg_pattern_finalize(svg_pattern *p) {
    if (!p) {
        return;
    }
    if (p->has_transform) {
        if (!svg_matrix_invert(&p->transform, &p->inv_transform)) {
            p->has_transform = 0;
        }
    }
}

void cupidimage_svg_parse_clip_tag(const svg_tag *tag, svg_clip_path *c) {
    if (!tag || !c) {
        return;
    }
    const char *units = cupidimage_svg_get_attr(tag, "clipPathUnits");
    if (units) {
        if (svg_strcasecmp(units, "userSpaceOnUse") == 0) {
            c->units = SVG_GRADIENT_UNITS_USER_SPACE;
            c->has_units = 1;
        } else if (svg_strcasecmp(units, "objectBoundingBox") == 0) {
            c->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
            c->has_units = 1;
        }
    }
    const char *transform = cupidimage_svg_get_attr(tag, "transform");
    if (transform) {
        svg_matrix t;
        if (cupidimage_svg_parse_transform(transform, &t)) {
            c->transform = t;
            c->has_transform = 1;
        }
    }
    const char *href = cupidimage_svg_get_attr(tag, "href");
    char href_id[96];
    if (cupidimage_svg_parse_href_id(href, href_id, sizeof(href_id))) {
        free(c->href);
        c->href = svg_strdup(href_id);
    }
}

void cupidimage_svg_clip_inherit(svg_clip_path *c, svg_defs *defs) {
    if (!c || !c->href || !defs) {
        return;
    }
    svg_clip_path *ref = cupidimage_svg_defs_find_clip(defs, c->href);
    if (!ref || ref == c) {
        return;
    }
    if (!c->has_units) {
        c->units = ref->units;
    }
    if (!c->has_transform) {
        c->transform = ref->transform;
        c->inv_transform = ref->inv_transform;
        c->has_transform = ref->has_transform;
    }
    if (!c->content && ref->content) {
        c->content = svg_strdup(ref->content);
    }
}

void cupidimage_svg_clip_finalize(svg_clip_path *c) {
    if (!c) {
        return;
    }
    if (c->has_transform) {
        if (!svg_matrix_invert(&c->transform, &c->inv_transform)) {
            c->has_transform = 0;
        }
    }
}

void cupidimage_svg_parse_mask_tag(const svg_tag *tag, svg_mask *m) {
    if (!tag || !m) {
        return;
    }
    const char *units = cupidimage_svg_get_attr(tag, "maskUnits");
    if (units) {
        if (svg_strcasecmp(units, "userSpaceOnUse") == 0) {
            m->units = SVG_GRADIENT_UNITS_USER_SPACE;
            m->has_units = 1;
        } else if (svg_strcasecmp(units, "objectBoundingBox") == 0) {
            m->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
            m->has_units = 1;
        }
    }
    const char *content_units = cupidimage_svg_get_attr(tag, "maskContentUnits");
    if (content_units) {
        if (svg_strcasecmp(content_units, "userSpaceOnUse") == 0) {
            m->content_units = SVG_GRADIENT_UNITS_USER_SPACE;
            m->has_content_units = 1;
        } else if (svg_strcasecmp(content_units, "objectBoundingBox") == 0) {
            m->content_units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
            m->has_content_units = 1;
        }
    }
    const char *transform = cupidimage_svg_get_attr(tag, "transform");
    if (transform) {
        svg_matrix t;
        if (cupidimage_svg_parse_transform(transform, &t)) {
            m->transform = t;
            m->has_transform = 1;
        }
    }
    const char *href = cupidimage_svg_get_attr(tag, "href");
    char href_id[96];
    if (cupidimage_svg_parse_href_id(href, href_id, sizeof(href_id))) {
        free(m->href);
        m->href = svg_strdup(href_id);
    }
    int has = 0;
    const char *x = cupidimage_svg_get_attr(tag, "x");
    const char *y = cupidimage_svg_get_attr(tag, "y");
    const char *w = cupidimage_svg_get_attr(tag, "width");
    const char *h = cupidimage_svg_get_attr(tag, "height");
    if (x && cupidimage_svg_parse_grad_coord(x, &m->x, m->units))
        has = 1;
    if (y && cupidimage_svg_parse_grad_coord(y, &m->y, m->units))
        has = 1;
    if (w && cupidimage_svg_parse_grad_coord(w, &m->width, m->units))
        has = 1;
    if (h && cupidimage_svg_parse_grad_coord(h, &m->height, m->units))
        has = 1;
    if (has) {
        m->has_coords = 1;
    }
    const char *mask_type = cupidimage_svg_get_attr(tag, "mask-type");
    if (mask_type) {
        if (svg_strcasecmp(mask_type, "alpha") == 0) {
            m->type = SVG_MASK_TYPE_ALPHA;
            m->has_type = 1;
        } else if (svg_strcasecmp(mask_type, "luminance") == 0) {
            m->type = SVG_MASK_TYPE_LUMINANCE;
            m->has_type = 1;
        }
    }
}

void cupidimage_svg_parse_filter_tag(const svg_tag *tag, svg_filter *f) {
    if (!tag || !f) {
        return;
    }
    const char *units = cupidimage_svg_get_attr(tag, "filterUnits");
    if (units) {
        if (svg_strcasecmp(units, "userSpaceOnUse") == 0) {
            f->units = SVG_GRADIENT_UNITS_USER_SPACE;
            f->has_units = 1;
        } else if (svg_strcasecmp(units, "objectBoundingBox") == 0) {
            f->units = SVG_GRADIENT_UNITS_OBJECT_BOUNDING_BOX;
            f->has_units = 1;
        }
    }
    int has = 0;
    const char *x = cupidimage_svg_get_attr(tag, "x");
    const char *y = cupidimage_svg_get_attr(tag, "y");
    const char *w = cupidimage_svg_get_attr(tag, "width");
    const char *h = cupidimage_svg_get_attr(tag, "height");
    if (x && cupidimage_svg_parse_grad_coord(x, &f->x, f->units))
        has = 1;
    if (y && cupidimage_svg_parse_grad_coord(y, &f->y, f->units))
        has = 1;
    if (w && cupidimage_svg_parse_grad_coord(w, &f->width, f->units))
        has = 1;
    if (h && cupidimage_svg_parse_grad_coord(h, &f->height, f->units))
        has = 1;
    if (has) {
        f->has_coords = 1;
    }
}

void cupidimage_svg_parse_symbol_tag(const svg_tag *tag, svg_symbol *sym) {
    if (!tag || !sym) {
        return;
    }
    sym->vb_ok = cupidimage_svg_parse_viewbox(cupidimage_svg_get_attr(tag, "viewBox"), &sym->vb_x,
                                              &sym->vb_y, &sym->vb_w, &sym->vb_h);
    sym->preserve_none = 0;
    sym->align_x = 0.5f;
    sym->align_y = 0.5f;
    cupidimage_svg_parse_preserve_aspect_ratio(cupidimage_svg_get_attr(tag, "preserveAspectRatio"),
                                               &sym->preserve_none, &sym->align_x, &sym->align_y);
}

void cupidimage_svg_parse_marker_tag(const svg_tag *tag, svg_marker *mk, float base_len,
                                     float dpi) {
    if (!tag || !mk) {
        return;
    }
    int ok = 0;
    float v = 0.0f;
    v = cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "refX"), base_len, dpi, &ok);
    if (ok) {
        mk->ref_x = v;
    }
    v = cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "refY"), base_len, dpi, &ok);
    if (ok) {
        mk->ref_y = v;
    }
    v = cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "markerWidth"), base_len, dpi,
                                    &ok);
    if (ok && v > 0.0f) {
        mk->marker_w = v;
    }
    v = cupidimage_svg_parse_length(cupidimage_svg_get_attr(tag, "markerHeight"), base_len, dpi,
                                    &ok);
    if (ok && v > 0.0f) {
        mk->marker_h = v;
    }
    const char *units = cupidimage_svg_get_attr(tag, "markerUnits");
    if (units) {
        if (svg_strcasecmp(units, "userspaceonuse") == 0) {
            mk->units_stroke_width = 0;
        } else if (svg_strcasecmp(units, "strokewidth") == 0) {
            mk->units_stroke_width = 1;
        }
    }
    const char *orient = cupidimage_svg_get_attr(tag, "orient");
    if (orient) {
        if (svg_strcasecmp(orient, "auto-start-reverse") == 0) {
            mk->orient_auto = 1;
            mk->orient_auto_start_reverse = 1;
        } else if (svg_strcasecmp(orient, "auto") == 0) {
            mk->orient_auto = 1;
            mk->orient_auto_start_reverse = 0;
        } else {
            char *end = NULL;
            double a = strtod(orient, &end);
            if (end != orient) {
                mk->orient_auto = 0;
                mk->orient_auto_start_reverse = 0;
                mk->orient_angle = (float)a;
            }
        }
    }
    mk->vb_ok = cupidimage_svg_parse_viewbox(cupidimage_svg_get_attr(tag, "viewBox"), &mk->vb_x,
                                             &mk->vb_y, &mk->vb_w, &mk->vb_h);
    mk->preserve_none = 0;
    mk->align_x = 0.5f;
    mk->align_y = 0.5f;
    cupidimage_svg_parse_preserve_aspect_ratio(cupidimage_svg_get_attr(tag, "preserveAspectRatio"),
                                               &mk->preserve_none, &mk->align_x, &mk->align_y);
}

void cupidimage_svg_mask_inherit(svg_mask *m, svg_defs *defs) {
    if (!m || !m->href || !defs) {
        return;
    }
    svg_mask *ref = cupidimage_svg_defs_find_mask(defs, m->href);
    if (!ref || ref == m) {
        return;
    }
    if (!m->has_units) {
        m->units = ref->units;
    }
    if (!m->has_content_units) {
        m->content_units = ref->content_units;
    }
    if (!m->has_transform) {
        m->transform = ref->transform;
        m->inv_transform = ref->inv_transform;
        m->has_transform = ref->has_transform;
    }
    if (!m->has_type) {
        m->type = ref->type;
    }
    if (!m->has_coords) {
        m->x = ref->x;
        m->y = ref->y;
        m->width = ref->width;
        m->height = ref->height;
    }
    if (!m->content && ref->content) {
        m->content = svg_strdup(ref->content);
    }
}

void cupidimage_svg_mask_finalize(svg_mask *m) {
    if (!m) {
        return;
    }
    if (m->has_transform) {
        if (!svg_matrix_invert(&m->transform, &m->inv_transform)) {
            m->has_transform = 0;
        }
    }
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
