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

static void svg_selector_part_free(svg_selector_part *part);
static void svg_selector_free(svg_selector *sel);
static void svg_css_rule_free(svg_css_rule *rule);
static void svg_css_keyframes_free(svg_css_keyframes *kf);
static char *svg_css_strip_comments(const char *text);
static void svg_css_trim_span(const char **start, const char **end);
static char *svg_css_span_dup(const char *start, const char *end);
static const char *svg_css_find_matching_brace(const char *p);
static int svg_css_parse_label_offset(const char *s, float *out);
static int svg_css_extract_decl_value_span(const char *decls_start, const char *decls_end,
                                           const char *name, char **out_value);
static int svg_css_add_keyframe_step(svg_css_keyframes *kf, const svg_css_keyframe_step *step);
static int svg_css_add_keyframes(svg_css *css, svg_css_keyframes *kf);
static int svg_css_add_animation_binding(svg_css *css, svg_css_animation_binding *anim);
static int svg_css_parse_animation_shorthand(const char *value, svg_css_animation_binding *anim);
static int svg_css_parse_target_id(const char *selector, char *out, size_t out_cap);
static int svg_css_parse_keyframes_block(svg_css *css, const char *selector_start,
                                         const char *selector_end, const char *body_start,
                                         const char *body_end);
static void svg_css_parse_animation_bindings(svg_css *css, const char *selector_start,
                                             const char *selector_end, const char *decl_start,
                                             const char *decl_end);
static int svg_css_parse_selector(const char *s, svg_selector *out);
static int svg_css_parse_decls(svg_css_rule *rule, const char *s);
static int svg_css_add_rule(svg_css *css, svg_css_rule *rule);
static int svg_class_match(const char *class_attr, const char *class_name);
static int svg_selector_part_matches(const svg_selector_part *part, const svg_elem_info *info);
static int svg_selector_matches(const svg_selector *sel, const svg_elem_info *stack, int depth,
                                const svg_elem_info *siblings, int sibling_count);

void cupidimage_svg_css_init(svg_css *css) {
    if (!css) {
        return;
    }
    css->rules = NULL;
    css->rule_count = 0;
    css->rule_cap = 0;
    css->keyframes = NULL;
    css->keyframe_count = 0;
    css->keyframe_cap = 0;
    css->animations = NULL;
    css->animation_count = 0;
    css->animation_cap = 0;
}

static void svg_selector_part_free(svg_selector_part *part) {
    free(part->tag);
    free(part->id);
    for (int c = 0; c < part->class_count; c++)
        free(part->classes[c]);
    free(part->classes);
    for (int a = 0; a < part->attr_count; a++) {
        free(part->attr_names[a]);
        free(part->attr_values[a]);
    }
    free(part->attr_names);
    free(part->attr_values);
    free(part->attr_has_value);
    memset(part, 0, sizeof(*part));
}

static void svg_selector_free(svg_selector *sel) {
    for (int p = 0; p < sel->part_count; p++)
        svg_selector_part_free(&sel->parts[p]);
    free(sel->parts);
    free(sel->combinators);
    memset(sel, 0, sizeof(*sel));
}

static void svg_css_rule_free(svg_css_rule *rule) {
    for (int s = 0; s < rule->selector_count; s++)
        svg_selector_free(&rule->selectors[s]);
    free(rule->selectors);
    for (int d = 0; d < rule->decl_count; d++)
        free(rule->decls[d].value);
    free(rule->decls);
    memset(rule, 0, sizeof(*rule));
}

static void svg_css_keyframes_free(svg_css_keyframes *kf) {
    free(kf->name);
    for (int k = 0; k < kf->step_count; k++)
        free(kf->steps[k].transform);
    free(kf->steps);
    memset(kf, 0, sizeof(*kf));
}

void cupidimage_svg_css_free(svg_css *css) {
    if (!css) {
        return;
    }
    for (int i = 0; i < css->rule_count; i++) {
        svg_css_rule_free(&css->rules[i]);
    }
    free(css->rules);
    css->rules = NULL;
    css->rule_count = 0;
    css->rule_cap = 0;
    for (int i = 0; i < css->keyframe_count; i++) {
        svg_css_keyframes_free(&css->keyframes[i]);
    }
    free(css->keyframes);
    css->keyframes = NULL;
    css->keyframe_count = 0;
    css->keyframe_cap = 0;
    for (int i = 0; i < css->animation_count; i++) {
        free(css->animations[i].target_id);
        free(css->animations[i].name);
    }
    free(css->animations);
    css->animations = NULL;
    css->animation_count = 0;
    css->animation_cap = 0;
}

static char *svg_css_strip_comments(const char *text) {
    if (!text) {
        return NULL;
    }
    size_t len = strlen(text);
    char *out = (char *)malloc(len + 1);
    if (!out) {
        return NULL;
    }
    size_t w = 0;
    for (size_t i = 0; i < len;) {
        if (text[i] == '/' && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(text[i] == '*' && text[i + 1] == '/')) {
                i++;
            }
            if (i + 1 < len) {
                i += 2;
            }
            continue;
        }
        if (strncmp(text + i, "<![CDATA[", 9) == 0) {
            i += 9;
            continue;
        }
        if (strncmp(text + i, "]]>", 3) == 0) {
            i += 3;
            continue;
        }
        out[w++] = text[i++];
    }
    out[w] = '\0';
    return out;
}

static void svg_css_trim_span(const char **start, const char **end) {
    if (!start || !end || !*start || !*end) {
        return;
    }
    while (*start < *end && isspace((unsigned char)**start)) {
        (*start)++;
    }
    while (*end > *start && isspace((unsigned char)(*end)[-1])) {
        (*end)--;
    }
}

static char *svg_css_span_dup(const char *start, const char *end) {
    if (!start || !end || end <= start) {
        return NULL;
    }
    return svg_strndup(start, (size_t)(end - start));
}

static const char *svg_css_find_matching_brace(const char *p) {
    if (!p || *p != '{') {
        return NULL;
    }
    int depth = 0;
    char quote = '\0';
    for (; *p; p++) {
        if (quote) {
            if (*p == quote) {
                quote = '\0';
            } else if (*p == '\\' && p[1]) {
                p++;
            }
            continue;
        }
        if (*p == '"' || *p == '\'') {
            quote = *p;
            continue;
        }
        if (*p == '{') {
            depth++;
            continue;
        }
        if (*p == '}') {
            depth--;
            if (depth == 0) {
                return p;
            }
        }
    }
    return NULL;
}

static int svg_css_parse_label_offset(const char *s, float *out) {
    if (!s || !out) {
        return 0;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s) {
        return 0;
    }
    if (svg_strcasecmp(s, "from") == 0) {
        *out = 0.0f;
        return 1;
    }
    if (svg_strcasecmp(s, "to") == 0) {
        *out = 1.0f;
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
    if (*end == '%') {
        end++;
        v /= 100.0;
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

static int svg_css_extract_decl_value_span(const char *decls_start, const char *decls_end,
                                           const char *name, char **out_value) {
    if (!decls_start || !decls_end || decls_end <= decls_start || !name || !out_value) {
        return 0;
    }
    const char *p = decls_start;
    size_t name_len = strlen(name);
    while (p < decls_end) {
        while (p < decls_end && (isspace((unsigned char)*p) || *p == ';')) {
            p++;
        }
        if (p >= decls_end) {
            break;
        }
        const char *nstart = p;
        while (p < decls_end && *p != ':' && *p != ';') {
            p++;
        }
        if (p >= decls_end || *p != ':') {
            break;
        }
        const char *nend = p;
        p++;
        const char *vstart = p;
        int paren_depth = 0;
        char quote = '\0';
        while (p < decls_end) {
            char c = *p;
            if (quote) {
                if (c == quote) {
                    quote = '\0';
                } else if (c == '\\' && p + 1 < decls_end) {
                    p++;
                }
                p++;
                continue;
            }
            if (c == '"' || c == '\'') {
                quote = c;
                p++;
                continue;
            }
            if (c == '(') {
                paren_depth++;
                p++;
                continue;
            }
            if (c == ')' && paren_depth > 0) {
                paren_depth--;
                p++;
                continue;
            }
            if (c == ';' && paren_depth == 0) {
                break;
            }
            p++;
        }
        const char *vend = p;
        while (nend > nstart && isspace((unsigned char)nend[-1])) {
            nend--;
        }
        while (vend > vstart && isspace((unsigned char)vend[-1])) {
            vend--;
        }
        while (nstart < nend && isspace((unsigned char)*nstart)) {
            nstart++;
        }
        if ((size_t)(nend - nstart) == name_len && svg_strncasecmp(nstart, name, name_len) == 0) {
            *out_value = svg_css_span_dup(vstart, vend);
            return *out_value != NULL;
        }
        if (p < decls_end && *p == ';') {
            p++;
        }
    }
    return 0;
}

static int svg_css_add_keyframe_step(svg_css_keyframes *kf, const svg_css_keyframe_step *step) {
    if (!kf || !step) {
        return 0;
    }
    if (kf->step_count >= kf->step_cap) {
        int new_cap = kf->step_cap ? kf->step_cap * 2 : 8;
        svg_css_keyframe_step *n = (svg_css_keyframe_step *)realloc(
            kf->steps, (size_t)new_cap * sizeof(svg_css_keyframe_step));
        if (!n) {
            return 0;
        }
        kf->steps = n;
        kf->step_cap = new_cap;
    }
    kf->steps[kf->step_count++] = *step;
    return 1;
}

static int svg_css_add_keyframes(svg_css *css, svg_css_keyframes *kf) {
    if (!css || !kf || !kf->name || kf->step_count <= 0) {
        return 0;
    }
    if (css->keyframe_count >= css->keyframe_cap) {
        int new_cap = css->keyframe_cap ? css->keyframe_cap * 2 : 8;
        svg_css_keyframes *n = (svg_css_keyframes *)realloc(
            css->keyframes, (size_t)new_cap * sizeof(svg_css_keyframes));
        if (!n) {
            return 0;
        }
        css->keyframes = n;
        css->keyframe_cap = new_cap;
    }
    css->keyframes[css->keyframe_count++] = *kf;
    return 1;
}

static int svg_css_add_animation_binding(svg_css *css, svg_css_animation_binding *anim) {
    if (!css || !anim || !anim->target_id || !anim->name || anim->duration <= 0.0f) {
        return 0;
    }
    if (css->animation_count >= css->animation_cap) {
        int new_cap = css->animation_cap ? css->animation_cap * 2 : 16;
        svg_css_animation_binding *n = (svg_css_animation_binding *)realloc(
            css->animations, (size_t)new_cap * sizeof(svg_css_animation_binding));
        if (!n) {
            return 0;
        }
        css->animations = n;
        css->animation_cap = new_cap;
    }
    css->animations[css->animation_count++] = *anim;
    return 1;
}

static int svg_css_parse_animation_shorthand(const char *value, svg_css_animation_binding *anim) {
    if (!value || !anim) {
        return 0;
    }
    memset(anim, 0, sizeof(*anim));
    anim->repeat_count = 1.0f;
    anim->direction = SVG_CSS_ANIM_DIR_NORMAL;
    anim->fill_mode = SVG_CSS_ANIM_FILL_NONE;

    const char *p = value;
    int have_name = 0;
    int have_duration = 0;
    while (*p) {
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *tstart = p;
        int paren_depth = 0;
        while (*p) {
            char c = *p;
            if (c == '(') {
                paren_depth++;
            } else if (c == ')' && paren_depth > 0) {
                paren_depth--;
            } else if (isspace((unsigned char)c) && paren_depth == 0) {
                break;
            }
            p++;
        }
        const char *tend = p;
        char *tok = svg_css_span_dup(tstart, tend);
        if (!tok) {
            free(anim->name);
            anim->name = NULL;
            return 0;
        }
        if (strchr(tok, ',')) {
            char *comma = strchr(tok, ',');
            *comma = '\0';
            while (comma > tok && isspace((unsigned char)comma[-1])) {
                comma--;
            }
            *comma = '\0';
            p = tend;
        }
        float secs = 0.0f;
        if (cupidimage_svg_parse_clock_seconds(tok, &secs)) {
            if (!have_duration) {
                anim->duration = secs;
                have_duration = 1;
            } else {
                anim->delay = secs;
            }
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "infinite") == 0) {
            anim->repeat_indefinite = 1;
            free(tok);
            continue;
        }
        char *num_end = NULL;
        float rep = strtof(tok, &num_end);
        if (num_end && *num_end == '\0' && rep > 0.0f) {
            anim->repeat_count = rep;
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "normal") == 0) {
            anim->direction = SVG_CSS_ANIM_DIR_NORMAL;
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "reverse") == 0) {
            anim->direction = SVG_CSS_ANIM_DIR_REVERSE;
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "alternate") == 0) {
            anim->direction = SVG_CSS_ANIM_DIR_ALTERNATE;
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "alternate-reverse") == 0) {
            anim->direction = SVG_CSS_ANIM_DIR_ALTERNATE_REVERSE;
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "forwards") == 0) {
            anim->fill_mode = SVG_CSS_ANIM_FILL_FORWARDS;
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "backwards") == 0) {
            anim->fill_mode = SVG_CSS_ANIM_FILL_BACKWARDS;
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "both") == 0) {
            anim->fill_mode = SVG_CSS_ANIM_FILL_BOTH;
            free(tok);
            continue;
        }
        if (svg_strcasecmp(tok, "none") == 0 || svg_strcasecmp(tok, "linear") == 0 ||
            svg_strcasecmp(tok, "ease") == 0 || svg_strcasecmp(tok, "ease-in") == 0 ||
            svg_strcasecmp(tok, "ease-out") == 0 || svg_strcasecmp(tok, "ease-in-out") == 0 ||
            svg_strncasecmp(tok, "steps(", 6) == 0 ||
            svg_strncasecmp(tok, "cubic-bezier(", 13) == 0 || svg_strcasecmp(tok, "running") == 0 ||
            svg_strcasecmp(tok, "paused") == 0) {
            free(tok);
            continue;
        }
        if (!have_name) {
            anim->name = tok;
            tok = NULL;
            have_name = 1;
        }
        free(tok);
    }
    if (!have_name || !have_duration || anim->duration <= 0.0f) {
        free(anim->name);
        anim->name = NULL;
        return 0;
    }
    return 1;
}

static int svg_css_parse_target_id(const char *selector, char *out, size_t out_cap) {
    if (!selector || !out || out_cap == 0) {
        return 0;
    }
    const char *p = selector;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '#') {
        return 0;
    }
    p++;
    const char *start = p;
    while (*p && (isalnum((unsigned char)*p) || *p == '_' || *p == '-' || *p == ':' || *p == '.')) {
        p++;
    }
    if (p == start) {
        return 0;
    }
    size_t n = (size_t)(p - start);
    if (n >= out_cap) {
        n = out_cap - 1u;
    }
    memcpy(out, start, n);
    out[n] = '\0';
    return 1;
}

static int svg_css_parse_keyframes_block(svg_css *css, const char *selector_start,
                                         const char *selector_end, const char *body_start,
                                         const char *body_end) {
    if (!css || !selector_start || !selector_end || !body_start || !body_end) {
        return 0;
    }
    const char *s = selector_start;
    const char *e = selector_end;
    svg_css_trim_span(&s, &e);
    if (e <= s) {
        return 0;
    }
    if (!(svg_strncasecmp(s, "@keyframes", 10) == 0 ||
          svg_strncasecmp(s, "@-webkit-keyframes", 18) == 0)) {
        return 0;
    }
    const char *name = s + (svg_strncasecmp(s, "@-webkit-keyframes", 18) == 0 ? 18 : 10);
    while (name < e && isspace((unsigned char)*name)) {
        name++;
    }
    const char *name_end = e;
    while (name_end > name && isspace((unsigned char)name_end[-1])) {
        name_end--;
    }
    if (name_end <= name) {
        return 0;
    }

    svg_css_keyframes kf;
    memset(&kf, 0, sizeof(kf));
    kf.name = svg_css_span_dup(name, name_end);
    if (!kf.name) {
        return 0;
    }
    kf.prop = SVG_CSS_ANIM_PROP_NONE;

    const char *p = body_start;
    while (p < body_end) {
        while (p < body_end && isspace((unsigned char)*p)) {
            p++;
        }
        if (p >= body_end) {
            break;
        }
        const char *label_start = p;
        while (p < body_end && *p != '{') {
            p++;
        }
        if (p >= body_end || *p != '{') {
            break;
        }
        const char *label_end = p;
        const char *close = svg_css_find_matching_brace(p);
        if (!close || close >= body_end) {
            break;
        }
        const char *decl_start = p + 1;
        const char *decl_end = close;

        char *transform_val = NULL;
        char *dash_val = NULL;
        char *color_val = NULL;
        svg_css_extract_decl_value_span(decl_start, decl_end, "transform", &transform_val);
        svg_css_extract_decl_value_span(decl_start, decl_end, "stroke-dashoffset", &dash_val);
        svg_css_extract_decl_value_span(decl_start, decl_end, "stop-color", &color_val);

        int prop = SVG_CSS_ANIM_PROP_NONE;
        if (transform_val && *transform_val) {
            prop = SVG_CSS_ANIM_PROP_TRANSFORM;
        } else if (dash_val && *dash_val) {
            prop = SVG_CSS_ANIM_PROP_STROKE_DASHOFFSET;
        } else if (color_val && *color_val) {
            prop = SVG_CSS_ANIM_PROP_STOP_COLOR;
        }
        if (prop != SVG_CSS_ANIM_PROP_NONE) {
            if (kf.prop == SVG_CSS_ANIM_PROP_NONE) {
                kf.prop = prop;
            }
            if (kf.prop == prop) {
                const char *ls = label_start;
                while (ls < label_end) {
                    while (ls < label_end && (isspace((unsigned char)*ls) || *ls == ',')) {
                        ls++;
                    }
                    const char *le = ls;
                    while (le < label_end && *le != ',') {
                        le++;
                    }
                    const char *lts = ls;
                    const char *lte = le;
                    svg_css_trim_span(&lts, &lte);
                    if (lte > lts) {
                        char *label = svg_css_span_dup(lts, lte);
                        float off = 0.0f;
                        if (label && svg_css_parse_label_offset(label, &off)) {
                            svg_css_keyframe_step step;
                            memset(&step, 0, sizeof(step));
                            step.offset = off;
                            if (prop == SVG_CSS_ANIM_PROP_TRANSFORM) {
                                step.transform = svg_strdup(transform_val);
                            } else if (prop == SVG_CSS_ANIM_PROP_STROKE_DASHOFFSET) {
                                char *num_end = NULL;
                                step.number = strtof(dash_val, &num_end);
                                if (!num_end || num_end == dash_val) {
                                    step.number = 0.0f;
                                }
                            } else if (prop == SVG_CSS_ANIM_PROP_STOP_COLOR) {
                                uint32_t col = 0;
                                int is_none = 0;
                                if (cupidimage_svg_parse_color(color_val, &col, &is_none) &&
                                    !is_none) {
                                    step.color = col;
                                }
                            }
                            if (!svg_css_add_keyframe_step(&kf, &step))
                                free(step.transform);
                        }
                        free(label);
                    }
                    ls = (le < label_end) ? (le + 1) : le;
                }
            }
        }
        free(transform_val);
        free(dash_val);
        free(color_val);
        p = close + 1;
    }

    for (int i = 0; i < kf.step_count - 1; i++) {
        for (int j = i + 1; j < kf.step_count; j++) {
            if (kf.steps[j].offset < kf.steps[i].offset) {
                svg_css_keyframe_step tmp = kf.steps[i];
                kf.steps[i] = kf.steps[j];
                kf.steps[j] = tmp;
            }
        }
    }
    if (kf.prop == SVG_CSS_ANIM_PROP_NONE || kf.step_count == 0 ||
        !svg_css_add_keyframes(css, &kf)) {
        svg_css_keyframes_free(&kf);
        return 0;
    }
    return 1;
}

static void svg_css_parse_animation_bindings(svg_css *css, const char *selector_start,
                                             const char *selector_end, const char *decl_start,
                                             const char *decl_end) {
    if (!css || !selector_start || !selector_end || !decl_start || !decl_end) {
        return;
    }
    char *anim_value = NULL;
    if (!svg_css_extract_decl_value_span(decl_start, decl_end, "animation", &anim_value) ||
        !anim_value) {
        return;
    }
    const char *sel = selector_start;
    while (sel < selector_end) {
        while (sel < selector_end && (isspace((unsigned char)*sel) || *sel == ',')) {
            sel++;
        }
        const char *send = sel;
        while (send < selector_end && *send != ',') {
            send++;
        }
        const char *ts = sel;
        const char *te = send;
        svg_css_trim_span(&ts, &te);
        if (te > ts) {
            char id[128];
            if (svg_css_parse_target_id(ts, id, sizeof(id))) {
                svg_css_animation_binding anim;
                if (svg_css_parse_animation_shorthand(anim_value, &anim)) {
                    anim.target_id = svg_strdup(id);
                    if (!anim.target_id || !svg_css_add_animation_binding(css, &anim)) {
                        free(anim.target_id);
                        free(anim.name);
                    }
                }
            }
        }
        sel = (send < selector_end) ? (send + 1) : send;
    }
    free(anim_value);
}

static int svg_css_parse_selector(const char *s, svg_selector *out) {
    if (!s || !out) {
        return 0;
    }
    svg_selector sel;
    memset(&sel, 0, sizeof(sel));
    svg_selector_part part = {0};
    const char *p = s;
    int pending_combinator = 0; /* 0: descendant, 1: child (>), 2: adjacent (+), 3: sibling (~) */
    while (*p) {
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        if (*p == '>' || *p == '+' || *p == '~') {
            pending_combinator = (*p == '>') ? 1 : (*p == '+' ? 2 : 3);
            p++;
            while (isspace((unsigned char)*p)) {
                p++;
            }
            if (!*p) {
                break;
            }
        }
        memset(&part, 0, sizeof(part));
        int matched = 0;
        if (*p == '*') {
            p++;
            matched = 1;
        } else if (isalpha((unsigned char)*p)) {
            const char *start = p;
            while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '_')) {
                p++;
            }
            part.tag = svg_strndup(start, (size_t)(p - start));
            if (!part.tag)
                goto fail;
            svg_lowercase(part.tag);
            sel.specificity += 1;
            matched = 1;
        }
        while (*p == '.' || *p == '#') {
            char type = *p++;
            const char *start = p;
            while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '_')) {
                p++;
            }
            if (p == start) {
                break;
            }
            char *name = svg_strndup(start, (size_t)(p - start));
            if (!name) {
                goto fail;
            }
            matched = 1;
            if (type == '#') {
                free(part.id);
                part.id = name;
                sel.specificity += 100;
            } else {
                char **classes =
                    (char **)realloc(part.classes, (size_t)(part.class_count + 1) * sizeof(char *));
                if (!classes) {
                    free(name);
                    goto fail;
                }
                part.classes = classes;
                part.classes[part.class_count++] = name;
                sel.specificity += 10;
            }
        }
        while (*p == '[') {
            p++;
            while (isspace((unsigned char)*p)) {
                p++;
            }
            const char *nstart = p;
            while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '_' || *p == ':')) {
                p++;
            }
            if (p == nstart) {
                break;
            }
            char *aname = svg_strndup(nstart, (size_t)(p - nstart));
            if (!aname) {
                goto fail;
            }
            svg_lowercase(aname);
            while (isspace((unsigned char)*p)) {
                p++;
            }
            int has_value = 0;
            char *avalue = NULL;
            if (*p == '=') {
                has_value = 1;
                p++;
                while (isspace((unsigned char)*p)) {
                    p++;
                }
                if (*p == '"' || *p == '\'') {
                    char q = *p++;
                    const char *vstart = p;
                    while (*p && *p != q) {
                        p++;
                    }
                    avalue = svg_strndup(vstart, (size_t)(p - vstart));
                    if (*p == q) {
                        p++;
                    }
                } else {
                    const char *vstart = p;
                    while (*p && *p != ']' && !isspace((unsigned char)*p)) {
                        p++;
                    }
                    avalue = svg_strndup(vstart, (size_t)(p - vstart));
                }
                if (!avalue) {
                    free(aname);
                    goto fail;
                }
            }
            while (isspace((unsigned char)*p)) {
                p++;
            }
            if (*p == ']') {
                p++;
            }
            int new_count = part.attr_count + 1;
            char **names = (char **)malloc((size_t)new_count * sizeof(char *));
            char **values = (char **)malloc((size_t)new_count * sizeof(char *));
            uint8_t *flags = (uint8_t *)malloc((size_t)new_count * sizeof(uint8_t));
            if (!names || !values || !flags) {
                free(aname);
                free(avalue);
                free(names);
                free(values);
                free(flags);
                goto fail;
            }
            for (int ai = 0; ai < part.attr_count; ai++) {
                names[ai] = part.attr_names[ai];
                values[ai] = part.attr_values[ai];
                flags[ai] = part.attr_has_value[ai];
            }
            free(part.attr_names);
            free(part.attr_values);
            free(part.attr_has_value);
            part.attr_names = names;
            part.attr_values = values;
            part.attr_has_value = flags;
            part.attr_names[part.attr_count] = aname;
            part.attr_values[part.attr_count] = avalue;
            part.attr_has_value[part.attr_count] = (uint8_t)has_value;
            part.attr_count = new_count;
            sel.specificity += 10;
            matched = 1;
        }
        if (!matched) {
            break;
        }
        int old_count = sel.part_count;
        svg_selector_part *parts = (svg_selector_part *)realloc(
            sel.parts, (size_t)(old_count + 1) * sizeof(svg_selector_part));
        if (!parts) {
            goto fail;
        }
        sel.parts = parts;
        sel.parts[old_count] = part;
        sel.part_count = old_count + 1;
        memset(&part, 0, sizeof(part));
        if (old_count > 0) {
            int *combs = (int *)realloc(sel.combinators, (size_t)old_count * sizeof(int));
            if (!combs) {
                goto fail;
            }
            sel.combinators = combs;
            sel.combinators[old_count - 1] = pending_combinator;
        }
        pending_combinator = 0;
        int saw_space = 0;
        while (isspace((unsigned char)*p)) {
            saw_space = 1;
            p++;
        }
        if (*p == '>' || *p == '+' || *p == '~') {
            pending_combinator = (*p == '>') ? 1 : (*p == '+' ? 2 : 3);
            p++;
        } else if (saw_space) {
            pending_combinator = 0;
        }
    }
    if (sel.part_count == 0) {
        goto fail;
    }
    svg_selector_part_free(&part);
    *out = sel;
    return 1;
fail:
    svg_selector_part_free(&part);
    svg_selector_free(&sel);
    return 0;
}

static int svg_css_parse_decls(svg_css_rule *rule, const char *s) {
    if (!rule || !s) {
        return 0;
    }
    const char *p = s;
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
            int prop = cupidimage_svg_property_id(name_buf);
            if (prop >= 0) {
                char *val = svg_strndup(value_start, (size_t)(value_end - value_start));
                if (!val) {
                    return 0;
                }
                svg_css_decl *decls = (svg_css_decl *)realloc(
                    rule->decls, (size_t)(rule->decl_count + 1) * sizeof(svg_css_decl));
                if (!decls) {
                    free(val);
                    return 0;
                }
                rule->decls = decls;
                rule->decls[rule->decl_count].prop = prop;
                rule->decls[rule->decl_count].value = val;
                rule->decl_count++;
            }
        }
        if (*p == ';') {
            p++;
        }
    }
    return rule->decl_count > 0;
}

static int svg_css_add_rule(svg_css *css, svg_css_rule *rule) {
    if (!css || !rule) {
        return 0;
    }
    if (css->rule_count >= css->rule_cap) {
        int new_cap = css->rule_cap ? css->rule_cap * 2 : 16;
        svg_css_rule *n =
            (svg_css_rule *)realloc(css->rules, (size_t)new_cap * sizeof(svg_css_rule));
        if (!n) {
            return 0;
        }
        css->rules = n;
        css->rule_cap = new_cap;
    }
    css->rules[css->rule_count++] = *rule;
    return 1;
}

int cupidimage_svg_css_parse(svg_css *css, const char *text) {
    if (!css || !text) {
        return 0;
    }
    char *clean = svg_css_strip_comments(text);
    if (!clean) {
        return 0;
    }
    char *p = clean;
    while (*p) {
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        char *brace = strchr(p, '{');
        if (!brace) {
            break;
        }
        char *block_end = (char *)svg_css_find_matching_brace(brace);
        if (!block_end) {
            break;
        }
        const char *selector_start = p;
        const char *selector_end = brace;
        const char *body_start = brace + 1;
        const char *body_end = block_end;

        svg_css_parse_keyframes_block(css, selector_start, selector_end, body_start, body_end);
        svg_css_parse_animation_bindings(css, selector_start, selector_end, body_start, body_end);

        char *selectors_str = svg_css_span_dup(selector_start, selector_end);
        char *decls_str = svg_css_span_dup(body_start, body_end);
        if (!selectors_str || !decls_str) {
            free(selectors_str);
            free(decls_str);
            p = block_end + 1;
            continue;
        }
        const char *ss = selectors_str;
        const char *se = selectors_str + strlen(selectors_str);
        svg_css_trim_span(&ss, &se);
        if (se > ss && *ss == '@') {
            free(selectors_str);
            free(decls_str);
            p = block_end + 1;
            continue;
        }

        svg_css_rule rule;
        memset(&rule, 0, sizeof(rule));
        rule.order = css->rule_count;
        char *sel_p = selectors_str;
        while (*sel_p) {
            while (isspace((unsigned char)*sel_p) || *sel_p == ',') {
                sel_p++;
            }
            if (!*sel_p) {
                break;
            }
            char *sel_end = sel_p;
            while (*sel_end && *sel_end != ',') {
                sel_end++;
            }
            char *sel_str = svg_strndup(sel_p, (size_t)(sel_end - sel_p));
            if (sel_str) {
                svg_selector sel;
                memset(&sel, 0, sizeof(sel));
                if (svg_css_parse_selector(sel_str, &sel)) {
                    svg_selector *sels = (svg_selector *)realloc(
                        rule.selectors, (size_t)(rule.selector_count + 1) * sizeof(svg_selector));
                    if (sels) {
                        rule.selectors = sels;
                        rule.selectors[rule.selector_count++] = sel;
                    } else {
                        svg_selector_free(&sel);
                    }
                }
                free(sel_str);
            }
            if (*sel_end == ',') {
                sel_end++;
            }
            sel_p = sel_end;
        }
        if (rule.selector_count == 0 || !svg_css_parse_decls(&rule, decls_str) ||
            !svg_css_add_rule(css, &rule)) {
            svg_css_rule_free(&rule);
        }
        free(selectors_str);
        free(decls_str);
        p = block_end + 1;
    }
    free(clean);
    return 1;
}

static int svg_class_match(const char *class_attr, const char *class_name) {
    if (!class_attr || !class_name) {
        return 0;
    }
    const char *p = class_attr;
    while (*p) {
        while (isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *start = p;
        while (*p && !isspace((unsigned char)*p)) {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (strlen(class_name) == len && strncmp(start, class_name, len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int svg_selector_part_matches(const svg_selector_part *part, const svg_elem_info *info) {
    if (!part || !info) {
        return 0;
    }
    if (part->tag && (!info->tag || strcmp(part->tag, info->tag) != 0)) {
        return 0;
    }
    if (part->id && (!info->id || strcmp(part->id, info->id) != 0)) {
        return 0;
    }
    for (int i = 0; i < part->class_count; i++) {
        if (!svg_class_match(info->class_attr, part->classes[i])) {
            return 0;
        }
    }
    for (int i = 0; i < part->attr_count; i++) {
        const char *match_value = NULL;
        int found = 0;
        for (int a = 0; a < info->attr_count; a++) {
            const char *aname = svg_local_name(info->attr_names[a]);
            if (aname && svg_strcasecmp(aname, part->attr_names[i]) == 0) {
                found = 1;
                match_value = info->attr_values[a];
                break;
            }
        }
        if (!found) {
            return 0;
        }
        if (part->attr_has_value[i]) {
            if (!match_value || strcmp(match_value, part->attr_values[i]) != 0) {
                return 0;
            }
        }
    }
    return 1;
}

static int svg_selector_matches(const svg_selector *sel, const svg_elem_info *stack, int depth,
                                const svg_elem_info *siblings, int sibling_count) {
    if (!sel || !stack || depth <= 0) {
        return 0;
    }
    int part_idx = sel->part_count - 1;
    if (!svg_selector_part_matches(&sel->parts[part_idx], &stack[depth - 1])) {
        return 0;
    }
    part_idx--;
    int ancestor_idx = depth - 2;
    while (part_idx >= 0) {
        int comb = 0;
        if (sel->combinators) {
            comb = sel->combinators[part_idx];
        }
        if (comb == 2 || comb == 3) {
            int found = 0;
            if (siblings && sibling_count > 0) {
                if (comb == 2) {
                    const svg_elem_info *prev = &siblings[sibling_count - 1];
                    found = svg_selector_part_matches(&sel->parts[part_idx], prev);
                } else {
                    for (int i = sibling_count - 1; i >= 0; i--) {
                        if (svg_selector_part_matches(&sel->parts[part_idx], &siblings[i])) {
                            found = 1;
                            break;
                        }
                    }
                }
            }
            if (!found) {
                return 0;
            }
            part_idx--;
            continue;
        }
        if (comb == 1) {
            if (ancestor_idx < 0) {
                return 0;
            }
            if (!svg_selector_part_matches(&sel->parts[part_idx], &stack[ancestor_idx])) {
                return 0;
            }
            ancestor_idx--;
            part_idx--;
            continue;
        }
        int found_idx = -1;
        for (int i = ancestor_idx; i >= 0; i--) {
            if (svg_selector_part_matches(&sel->parts[part_idx], &stack[i])) {
                found_idx = i;
                break;
            }
        }
        if (found_idx < 0) {
            return 0;
        }
        ancestor_idx = found_idx - 1;
        part_idx--;
    }
    return 1;
}

void cupidimage_svg_css_apply(svg_style *style, const svg_css *css, const svg_defs *defs,
                              const svg_elem_info *stack, int depth, const svg_elem_info *siblings,
                              int sibling_count, float base_len, float dpi) {
    if (!style || !css || css->rule_count == 0 || !stack || depth <= 0) {
        return;
    }
    int best_spec[SVG_PROP_COUNT];
    int best_order[SVG_PROP_COUNT];
    for (int i = 0; i < SVG_PROP_COUNT; i++) {
        best_spec[i] = -1;
        best_order[i] = -1;
    }
    for (int r = 0; r < css->rule_count; r++) {
        const svg_css_rule *rule = &css->rules[r];
        for (int s = 0; s < rule->selector_count; s++) {
            const svg_selector *sel = &rule->selectors[s];
            if (!svg_selector_matches(sel, stack, depth, siblings, sibling_count)) {
                continue;
            }
            int spec = sel->specificity;
            for (int d = 0; d < rule->decl_count; d++) {
                int prop = rule->decls[d].prop;
                if (prop < 0 || prop >= SVG_PROP_COUNT) {
                    continue;
                }
                if (spec > best_spec[prop] ||
                    (spec == best_spec[prop] && rule->order >= best_order[prop])) {
                    cupidimage_svg_style_apply_property_id(style, prop, rule->decls[d].value,
                                                           base_len, dpi, defs);
                    best_spec[prop] = spec;
                    best_order[prop] = rule->order;
                }
            }
        }
    }
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
