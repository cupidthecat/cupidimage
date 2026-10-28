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

static float svg_hue_to_rgb_component(float p, float q, float t);
static void svg_hsl_to_rgb(float h_deg, float s, float l, float *r, float *g, float *b);
static int svg_parse_color_component(const char **p, float *out, int *is_percent);
static int svg_parse_hue_component(const char **p, float *out_deg);
static int svg_parse_alpha_component(const char **p, float *out_alpha);
static int svg_parse_url_id(const char *value, char *out, size_t outcap);
static int svg_should_skip_use_wrapper_attr(const char *name);
static void svg_style_apply_property(svg_style *style, const char *name, const char *value,
                                     float base_len, float dpi, const svg_defs *defs);
static void svg_style_apply_style_attr(svg_style *style, const char *value, float base_len,
                                       float dpi, const svg_defs *defs);

void cupidimage_svg_style_init(svg_style *style) {
    style->fill_type = SVG_PAINT_COLOR;
    style->stroke_type = SVG_PAINT_NONE;
    style->fill_color = 0x000000FFu;
    style->stroke_color = 0x000000FFu;
    style->fill_gradient = NULL;
    style->stroke_gradient = NULL;
    style->fill_pattern = NULL;
    style->stroke_pattern = NULL;
    style->clip_path = NULL;
    style->mask = NULL;
    style->marker_start = NULL;
    style->marker_mid = NULL;
    style->marker_end = NULL;
    style->filter = NULL;
    style->color = 0x000000FFu;
    style->fill_opacity = 1.0f;
    style->stroke_opacity = 1.0f;
    style->opacity = 1.0f;
    style->stroke_width = 1.0f;
    style->stroke_linecap = SVG_LINECAP_BUTT;
    style->stroke_linejoin = SVG_LINEJOIN_MITER;
    style->stroke_miterlimit = 4.0f;
    for (int i = 0; i < SVG_MAX_DASH; i++) {
        style->stroke_dasharray[i] = 0.0f;
    }
    style->stroke_dashcount = 0;
    style->stroke_dashoffset = 0.0f;
    style->fill_rule_evenodd = 0;
    style->font_size = 16.0f;
    style->letter_spacing = 0.0f;
    style->word_spacing = 0.0f;
    style->text_anchor = 0;
    style->bold = 0;
    style->italic = 0;
    style->dominant_baseline = 0;
    style->display_none = 0;
    style->visibility_hidden = 0;
}

static const svg_named_color svg_named_colors[] = {{"aliceblue", 0xF0F8FFFFu},
                                                   {"antiquewhite", 0xFAEBD7FFu},
                                                   {"aqua", 0x00FFFFFFu},
                                                   {"aquamarine", 0x7FFFD4FFu},
                                                   {"azure", 0xF0FFFFFFu},
                                                   {"beige", 0xF5F5DCFFu},
                                                   {"bisque", 0xFFE4C4FFu},
                                                   {"black", 0x000000FFu},
                                                   {"blanchedalmond", 0xFFEBCDFFu},
                                                   {"blue", 0x0000FFFFu},
                                                   {"blueviolet", 0x8A2BE2FFu},
                                                   {"brown", 0xA52A2AFFu},
                                                   {"burlywood", 0xDEB887FFu},
                                                   {"cadetblue", 0x5F9EA0FFu},
                                                   {"chartreuse", 0x7FFF00FFu},
                                                   {"chocolate", 0xD2691EFFu},
                                                   {"coral", 0xFF7F50FFu},
                                                   {"cornflowerblue", 0x6495EDFFu},
                                                   {"cornsilk", 0xFFF8DCFFu},
                                                   {"crimson", 0xDC143CFFu},
                                                   {"cyan", 0x00FFFFFFu},
                                                   {"darkblue", 0x00008BFFu},
                                                   {"darkcyan", 0x008B8BFFu},
                                                   {"darkgoldenrod", 0xB8860BFFu},
                                                   {"darkgray", 0xA9A9A9FFu},
                                                   {"darkgreen", 0x006400FFu},
                                                   {"darkgrey", 0xA9A9A9FFu},
                                                   {"darkkhaki", 0xBDB76BFFu},
                                                   {"darkmagenta", 0x8B008BFFu},
                                                   {"darkolivegreen", 0x556B2FFFu},
                                                   {"darkorange", 0xFF8C00FFu},
                                                   {"darkorchid", 0x9932CCFFu},
                                                   {"darkred", 0x8B0000FFu},
                                                   {"darksalmon", 0xE9967AFFu},
                                                   {"darkseagreen", 0x8FBC8FFFu},
                                                   {"darkslateblue", 0x483D8BFFu},
                                                   {"darkslategray", 0x2F4F4FFFu},
                                                   {"darkslategrey", 0x2F4F4FFFu},
                                                   {"darkturquoise", 0x00CED1FFu},
                                                   {"darkviolet", 0x9400D3FFu},
                                                   {"deeppink", 0xFF1493FFu},
                                                   {"deepskyblue", 0x00BFFFFFu},
                                                   {"dimgray", 0x696969FFu},
                                                   {"dimgrey", 0x696969FFu},
                                                   {"dodgerblue", 0x1E90FFFFu},
                                                   {"firebrick", 0xB22222FFu},
                                                   {"floralwhite", 0xFFFAF0FFu},
                                                   {"forestgreen", 0x228B22FFu},
                                                   {"fuchsia", 0xFF00FFFFu},
                                                   {"gainsboro", 0xDCDCDCFFu},
                                                   {"ghostwhite", 0xF8F8FFFFu},
                                                   {"gold", 0xFFD700FFu},
                                                   {"goldenrod", 0xDAA520FFu},
                                                   {"gray", 0x808080FFu},
                                                   {"green", 0x008000FFu},
                                                   {"greenyellow", 0xADFF2FFFu},
                                                   {"grey", 0x808080FFu},
                                                   {"honeydew", 0xF0FFF0FFu},
                                                   {"hotpink", 0xFF69B4FFu},
                                                   {"indianred", 0xCD5C5CFFu},
                                                   {"indigo", 0x4B0082FFu},
                                                   {"ivory", 0xFFFFF0FFu},
                                                   {"khaki", 0xF0E68CFFu},
                                                   {"lavender", 0xE6E6FAFFu},
                                                   {"lavenderblush", 0xFFF0F5FFu},
                                                   {"lawngreen", 0x7CFC00FFu},
                                                   {"lemonchiffon", 0xFFFACDFFu},
                                                   {"lightblue", 0xADD8E6FFu},
                                                   {"lightcoral", 0xF08080FFu},
                                                   {"lightcyan", 0xE0FFFFFFu},
                                                   {"lightgoldenrodyellow", 0xFAFAD2FFu},
                                                   {"lightgray", 0xD3D3D3FFu},
                                                   {"lightgreen", 0x90EE90FFu},
                                                   {"lightgrey", 0xD3D3D3FFu},
                                                   {"lightpink", 0xFFB6C1FFu},
                                                   {"lightsalmon", 0xFFA07AFFu},
                                                   {"lightseagreen", 0x20B2AAFFu},
                                                   {"lightskyblue", 0x87CEFAFFu},
                                                   {"lightslategray", 0x778899FFu},
                                                   {"lightslategrey", 0x778899FFu},
                                                   {"lightsteelblue", 0xB0C4DEFFu},
                                                   {"lightyellow", 0xFFFFE0FFu},
                                                   {"lime", 0x00FF00FFu},
                                                   {"limegreen", 0x32CD32FFu},
                                                   {"linen", 0xFAF0E6FFu},
                                                   {"magenta", 0xFF00FFFFu},
                                                   {"maroon", 0x800000FFu},
                                                   {"mediumaquamarine", 0x66CDAAFFu},
                                                   {"mediumblue", 0x0000CDFFu},
                                                   {"mediumorchid", 0xBA55D3FFu},
                                                   {"mediumpurple", 0x9370DBFFu},
                                                   {"mediumseagreen", 0x3CB371FFu},
                                                   {"mediumslateblue", 0x7B68EEFFu},
                                                   {"mediumspringgreen", 0x00FA9AFFu},
                                                   {"mediumturquoise", 0x48D1CCFFu},
                                                   {"mediumvioletred", 0xC71585FFu},
                                                   {"midnightblue", 0x191970FFu},
                                                   {"mintcream", 0xF5FFFAFFu},
                                                   {"mistyrose", 0xFFE4E1FFu},
                                                   {"moccasin", 0xFFE4B5FFu},
                                                   {"navajowhite", 0xFFDEADFFu},
                                                   {"navy", 0x000080FFu},
                                                   {"oldlace", 0xFDF5E6FFu},
                                                   {"olive", 0x808000FFu},
                                                   {"olivedrab", 0x6B8E23FFu},
                                                   {"orange", 0xFFA500FFu},
                                                   {"orangered", 0xFF4500FFu},
                                                   {"orchid", 0xDA70D6FFu},
                                                   {"palegoldenrod", 0xEEE8AAFFu},
                                                   {"palegreen", 0x98FB98FFu},
                                                   {"paleturquoise", 0xAFEEEEFFu},
                                                   {"palevioletred", 0xDB7093FFu},
                                                   {"papayawhip", 0xFFEFD5FFu},
                                                   {"peachpuff", 0xFFDAB9FFu},
                                                   {"peru", 0xCD853FFFu},
                                                   {"pink", 0xFFC0CBFFu},
                                                   {"plum", 0xDDA0DDFFu},
                                                   {"powderblue", 0xB0E0E6FFu},
                                                   {"purple", 0x800080FFu},
                                                   {"rebeccapurple", 0x663399FFu},
                                                   {"red", 0xFF0000FFu},
                                                   {"rosybrown", 0xBC8F8FFFu},
                                                   {"royalblue", 0x4169E1FFu},
                                                   {"saddlebrown", 0x8B4513FFu},
                                                   {"salmon", 0xFA8072FFu},
                                                   {"sandybrown", 0xF4A460FFu},
                                                   {"seagreen", 0x2E8B57FFu},
                                                   {"seashell", 0xFFF5EEFFu},
                                                   {"sienna", 0xA0522DFFu},
                                                   {"silver", 0xC0C0C0FFu},
                                                   {"skyblue", 0x87CEEBFFu},
                                                   {"slateblue", 0x6A5ACDFFu},
                                                   {"slategray", 0x708090FFu},
                                                   {"slategrey", 0x708090FFu},
                                                   {"snow", 0xFFFAFAFFu},
                                                   {"springgreen", 0x00FF7FFFu},
                                                   {"steelblue", 0x4682B4FFu},
                                                   {"tan", 0xD2B48CFFu},
                                                   {"teal", 0x008080FFu},
                                                   {"thistle", 0xD8BFD8FFu},
                                                   {"tomato", 0xFF6347FFu},
                                                   {"turquoise", 0x40E0D0FFu},
                                                   {"violet", 0xEE82EEFFu},
                                                   {"wheat", 0xF5DEB3FFu},
                                                   {"white", 0xFFFFFFFFu},
                                                   {"whitesmoke", 0xF5F5F5FFu},
                                                   {"yellow", 0xFFFF00FFu},
                                                   {"yellowgreen", 0x9ACD32FFu}};

float cupidimage_svg_clamp01(float v) {
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

static float svg_hue_to_rgb_component(float p, float q, float t) {
    if (t < 0.0f) {
        t += 1.0f;
    } else if (t > 1.0f) {
        t -= 1.0f;
    }
    if (t < (1.0f / 6.0f)) {
        return p + (q - p) * 6.0f * t;
    }
    if (t < 0.5f) {
        return q;
    }
    if (t < (2.0f / 3.0f)) {
        return p + (q - p) * ((2.0f / 3.0f) - t) * 6.0f;
    }
    return p;
}

static void svg_hsl_to_rgb(float h_deg, float s, float l, float *r, float *g, float *b) {
    if (!r || !g || !b) {
        return;
    }
    s = cupidimage_svg_clamp01(s);
    l = cupidimage_svg_clamp01(l);
    float h = h_deg / 360.0f;
    h = h - floorf(h);
    if (s <= 1e-6f) {
        *r = l;
        *g = l;
        *b = l;
        return;
    }
    float q = (l < 0.5f) ? (l * (1.0f + s)) : (l + s - l * s);
    float p = 2.0f * l - q;
    *r = svg_hue_to_rgb_component(p, q, h + (1.0f / 3.0f));
    *g = svg_hue_to_rgb_component(p, q, h);
    *b = svg_hue_to_rgb_component(p, q, h - (1.0f / 3.0f));
}

static int svg_parse_color_component(const char **p, float *out, int *is_percent) {
    if (!p || !*p || !out) {
        return 0;
    }
    cupidimage_svg_skip_separators(p);
    if (!**p) {
        return 0;
    }
    char *end = NULL;
    double v = strtod(*p, &end);
    if (end == *p) {
        return 0;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    int percent = 0;
    if (*end == '%') {
        percent = 1;
        end++;
    }
    *out = (float)v;
    if (is_percent) {
        *is_percent = percent;
    }
    *p = end;
    return 1;
}

static int svg_parse_hue_component(const char **p, float *out_deg) {
    if (!p || !*p || !out_deg) {
        return 0;
    }
    cupidimage_svg_skip_separators(p);
    if (!**p) {
        return 0;
    }
    char *end = NULL;
    double v = strtod(*p, &end);
    if (end == *p) {
        return 0;
    }
    while (isspace((unsigned char)*end)) {
        end++;
    }
    if (svg_strncasecmp(end, "deg", 3) == 0) {
        end += 3;
    } else if (svg_strncasecmp(end, "grad", 4) == 0) {
        v *= 0.9;
        end += 4;
    } else if (svg_strncasecmp(end, "rad", 3) == 0) {
        v = v * (180.0 / M_PI);
        end += 3;
    } else if (svg_strncasecmp(end, "turn", 4) == 0) {
        v *= 360.0;
        end += 4;
    }
    *out_deg = (float)v;
    *p = end;
    return 1;
}

static int svg_parse_alpha_component(const char **p, float *out_alpha) {
    float v = 0.0f;
    int is_percent = 0;
    if (!svg_parse_color_component(p, &v, &is_percent)) {
        return 0;
    }
    float alpha = 0.0f;
    if (is_percent) {
        alpha = v / 100.0f;
    } else if (v <= 1.0f) {
        alpha = v;
    } else {
        alpha = v / 255.0f;
    }
    *out_alpha = cupidimage_svg_clamp01(alpha);
    return 1;
}

int cupidimage_svg_parse_color(const char *s, uint32_t *out, int *is_none) {
    if (is_none) {
        *is_none = 0;
    }
    if (!s) {
        return 0;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (!*s) {
        return 0;
    }
    if (svg_strcasecmp(s, "none") == 0) {
        if (is_none) {
            *is_none = 1;
        }
        return 1;
    }
    if (*s == '#') {
        s++;
        size_t len = strlen(s);
        if (len == 3 || len == 4) {
            int r = isxdigit((unsigned char)s[0]) ? (int)strtol((char[]){s[0], 0}, NULL, 16) : -1;
            int g = isxdigit((unsigned char)s[1]) ? (int)strtol((char[]){s[1], 0}, NULL, 16) : -1;
            int b = isxdigit((unsigned char)s[2]) ? (int)strtol((char[]){s[2], 0}, NULL, 16) : -1;
            int a = (len == 4)
                        ? (isxdigit((unsigned char)s[3]) ? (int)strtol((char[]){s[3], 0}, NULL, 16)
                                                         : -1)
                        : 15;
            if (r < 0 || g < 0 || b < 0 || a < 0) {
                return 0;
            }
            r = r * 17;
            g = g * 17;
            b = b * 17;
            a = a * 17;
            *out = ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a;
            return 1;
        }
        if (len == 6 || len == 8) {
            unsigned long v = strtoul(s, NULL, 16);
            if (len == 6) {
                v = (v << 8) | 0xFFu;
            }
            *out = (uint32_t)v;
            return 1;
        }
        return 0;
    }
    if (svg_strncasecmp(s, "rgb(", 4) == 0 || svg_strncasecmp(s, "rgba(", 5) == 0) {
        int has_alpha_func = (tolower((unsigned char)s[3]) == 'a');
        const char *p = s + (has_alpha_func ? 5 : 4);
        float rgb[3] = {0, 0, 0};
        for (int i = 0; i < 3; i++) {
            float v = 0.0f;
            int percent = 0;
            if (!svg_parse_color_component(&p, &v, &percent)) {
                return 0;
            }
            if (percent) {
                v = v * 2.55f;
            }
            rgb[i] = v;
        }
        float alpha = 1.0f;
        cupidimage_svg_skip_separators(&p);
        if (*p == '/') {
            p++;
            if (!svg_parse_alpha_component(&p, &alpha)) {
                return 0;
            }
        } else if (has_alpha_func) {
            if (!svg_parse_alpha_component(&p, &alpha)) {
                return 0;
            }
        }
        int r = (int)lroundf(fmaxf(0.0f, fminf(255.0f, rgb[0])));
        int g = (int)lroundf(fmaxf(0.0f, fminf(255.0f, rgb[1])));
        int b = (int)lroundf(fmaxf(0.0f, fminf(255.0f, rgb[2])));
        int a = (int)lroundf(cupidimage_svg_clamp01(alpha) * 255.0f);
        *out =
            ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)(a & 0xFF);
        return 1;
    }
    if (svg_strncasecmp(s, "hsl(", 4) == 0 || svg_strncasecmp(s, "hsla(", 5) == 0) {
        int has_alpha_func = (tolower((unsigned char)s[3]) == 'a');
        const char *p = s + (has_alpha_func ? 5 : 4);
        float h = 0.0f;
        if (!svg_parse_hue_component(&p, &h)) {
            return 0;
        }
        float s_comp = 0.0f;
        float l_comp = 0.0f;
        int s_percent = 0;
        int l_percent = 0;
        if (!svg_parse_color_component(&p, &s_comp, &s_percent)) {
            return 0;
        }
        if (!svg_parse_color_component(&p, &l_comp, &l_percent)) {
            return 0;
        }
        float sat = s_percent ? (s_comp / 100.0f) : s_comp;
        float lig = l_percent ? (l_comp / 100.0f) : l_comp;
        if (!s_percent && sat > 1.0f) {
            sat /= 100.0f;
        }
        if (!l_percent && lig > 1.0f) {
            lig /= 100.0f;
        }
        float alpha = 1.0f;
        cupidimage_svg_skip_separators(&p);
        if (*p == '/') {
            p++;
            if (!svg_parse_alpha_component(&p, &alpha)) {
                return 0;
            }
        } else if (has_alpha_func) {
            if (!svg_parse_alpha_component(&p, &alpha)) {
                return 0;
            }
        }
        float rf = 0.0f, gf = 0.0f, bf = 0.0f;
        svg_hsl_to_rgb(h, sat, lig, &rf, &gf, &bf);
        int r = (int)lroundf(cupidimage_svg_clamp01(rf) * 255.0f);
        int g = (int)lroundf(cupidimage_svg_clamp01(gf) * 255.0f);
        int b = (int)lroundf(cupidimage_svg_clamp01(bf) * 255.0f);
        int a = (int)lroundf(cupidimage_svg_clamp01(alpha) * 255.0f);
        *out =
            ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)(a & 0xFF);
        return 1;
    }
    for (size_t i = 0; i < sizeof(svg_named_colors) / sizeof(svg_named_colors[0]); i++) {
        if (svg_strcasecmp(s, svg_named_colors[i].name) == 0) {
            *out = svg_named_colors[i].rgba;
            return 1;
        }
    }
    return 0;
}

int cupidimage_svg_property_id(const char *name) {
    if (!name) {
        return -1;
    }
    if (strcmp(name, "fill") == 0)
        return SVG_PROP_FILL;
    if (strcmp(name, "stroke") == 0)
        return SVG_PROP_STROKE;
    if (strcmp(name, "stroke-width") == 0)
        return SVG_PROP_STROKE_WIDTH;
    if (strcmp(name, "stroke-linecap") == 0)
        return SVG_PROP_STROKE_LINECAP;
    if (strcmp(name, "stroke-linejoin") == 0)
        return SVG_PROP_STROKE_LINEJOIN;
    if (strcmp(name, "stroke-miterlimit") == 0)
        return SVG_PROP_STROKE_MITERLIMIT;
    if (strcmp(name, "stroke-dasharray") == 0)
        return SVG_PROP_STROKE_DASHARRAY;
    if (strcmp(name, "stroke-dashoffset") == 0)
        return SVG_PROP_STROKE_DASHOFFSET;
    if (strcmp(name, "clip-path") == 0)
        return SVG_PROP_CLIP_PATH;
    if (strcmp(name, "mask") == 0)
        return SVG_PROP_MASK;
    if (strcmp(name, "marker") == 0)
        return SVG_PROP_MARKER;
    if (strcmp(name, "marker-start") == 0)
        return SVG_PROP_MARKER_START;
    if (strcmp(name, "marker-mid") == 0)
        return SVG_PROP_MARKER_MID;
    if (strcmp(name, "marker-end") == 0)
        return SVG_PROP_MARKER_END;
    if (strcmp(name, "filter") == 0)
        return SVG_PROP_FILTER;
    if (strcmp(name, "opacity") == 0)
        return SVG_PROP_OPACITY;
    if (strcmp(name, "fill-opacity") == 0)
        return SVG_PROP_FILL_OPACITY;
    if (strcmp(name, "stroke-opacity") == 0)
        return SVG_PROP_STROKE_OPACITY;
    if (strcmp(name, "fill-rule") == 0)
        return SVG_PROP_FILL_RULE;
    if (strcmp(name, "color") == 0)
        return SVG_PROP_COLOR;
    if (strcmp(name, "font-size") == 0)
        return SVG_PROP_FONT_SIZE;
    if (strcmp(name, "text-anchor") == 0)
        return SVG_PROP_TEXT_ANCHOR;
    if (strcmp(name, "font-weight") == 0)
        return SVG_PROP_FONT_WEIGHT;
    if (strcmp(name, "font-style") == 0)
        return SVG_PROP_FONT_STYLE;
    if (strcmp(name, "dominant-baseline") == 0)
        return SVG_PROP_DOMINANT_BASELINE;
    if (strcmp(name, "alignment-baseline") == 0)
        return SVG_PROP_DOMINANT_BASELINE;
    if (strcmp(name, "letter-spacing") == 0)
        return SVG_PROP_LETTER_SPACING;
    if (strcmp(name, "word-spacing") == 0)
        return SVG_PROP_WORD_SPACING;
    if (strcmp(name, "display") == 0)
        return SVG_PROP_DISPLAY;
    if (strcmp(name, "visibility") == 0)
        return SVG_PROP_VISIBILITY;
    return -1;
}

static int svg_parse_url_id(const char *value, char *out, size_t outcap) {
    if (!value || !out || outcap == 0) {
        return 0;
    }
    const char *p = value;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (svg_strncasecmp(p, "url(", 4) != 0) {
        return 0;
    }
    p += 4;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '#') {
        p++;
    }
    const char *start = p;
    while (*p && *p != ')' && !isspace((unsigned char)*p)) {
        p++;
    }
    size_t len = (size_t)(p - start);
    if (len == 0 || len >= outcap) {
        return 0;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return 1;
}

int cupidimage_svg_parse_href_id(const char *value, char *out, size_t outcap) {
    if (!value || !out || outcap == 0) {
        return 0;
    }
    const char *p = value;
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (!*p) {
        return 0;
    }
    if (*p == '#') {
        p++;
        const char *start = p;
        while (*p && !isspace((unsigned char)*p)) {
            p++;
        }
        size_t len = (size_t)(p - start);
        if (len == 0 || len >= outcap) {
            return 0;
        }
        memcpy(out, start, len);
        out[len] = '\0';
        return 1;
    }
    if (svg_parse_url_id(value, out, outcap)) {
        return 1;
    }
    const char *hash = strchr(p, '#');
    if (hash && hash[1]) {
        hash++;
        const char *start = hash;
        while (*hash && !isspace((unsigned char)*hash)) {
            hash++;
        }
        size_t len = (size_t)(hash - start);
        if (len == 0 || len >= outcap) {
            return 0;
        }
        memcpy(out, start, len);
        out[len] = '\0';
        return 1;
    }
    return 0;
}

static int svg_should_skip_use_wrapper_attr(const char *name) {
    if (!name) {
        return 1;
    }
    const char *local = svg_local_name(name);
    if (!local) {
        return 1;
    }
    return strcmp(local, "href") == 0 || strcmp(local, "x") == 0 || strcmp(local, "y") == 0 ||
           strcmp(local, "width") == 0 || strcmp(local, "height") == 0 ||
           strcmp(local, "transform") == 0;
}

char *cupidimage_svg_build_use_wrapper_attrs(const svg_tag *tag) {
    if (!tag) {
        return NULL;
    }
    size_t cap = 256;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        return NULL;
    }
    buf[0] = '\0';
    for (int i = 0; i < tag->attr_count; i++) {
        const char *name = tag->attrs[i].name;
        const char *value = tag->attrs[i].value ? tag->attrs[i].value : "";
        if (svg_should_skip_use_wrapper_attr(name)) {
            continue;
        }
        size_t need = strlen(name) + strlen(value) + 4u;
        if (len + need + 1u > cap) {
            size_t new_cap = cap * 2u;
            while (len + need + 1u > new_cap) {
                new_cap *= 2u;
            }
            char *n = (char *)realloc(buf, new_cap);
            if (!n) {
                free(buf);
                return NULL;
            }
            buf = n;
            cap = new_cap;
        }
        int n = snprintf(buf + len, cap - len, " %s=\"%s\"", name, value);
        if (n < 0) {
            free(buf);
            return NULL;
        }
        len += (size_t)n;
    }
    return buf;
}

char *cupidimage_svg_wrap_with_group_attrs(const char *content, const char *attrs) {
    if (!content) {
        return NULL;
    }
    if (!attrs || !*attrs) {
        return svg_strdup(content);
    }
    const char *prefix = "<g";
    const char *mid = ">";
    const char *suffix = "</g>";
    size_t clen = strlen(content);
    size_t alen = strlen(attrs);
    size_t total = strlen(prefix) + alen + strlen(mid) + clen + strlen(suffix) + 1u;
    char *out = (char *)malloc(total);
    if (!out) {
        return NULL;
    }
    snprintf(out, total, "<g%s>%s</g>", attrs, content);
    return out;
}

void cupidimage_svg_style_apply_property_id(svg_style *style, int prop, const char *value,
                                            float base_len, float dpi, const svg_defs *defs) {
    if (!style || !value) {
        return;
    }
    if (prop == SVG_PROP_FILL || prop == SVG_PROP_STROKE) {
        svg_paint_type *ptype = (prop == SVG_PROP_FILL) ? &style->fill_type : &style->stroke_type;
        uint32_t *pcolor = (prop == SVG_PROP_FILL) ? &style->fill_color : &style->stroke_color;
        const svg_gradient **pgrad =
            (prop == SVG_PROP_FILL) ? &style->fill_gradient : &style->stroke_gradient;
        svg_pattern **ppat =
            (prop == SVG_PROP_FILL) ? &style->fill_pattern : &style->stroke_pattern;
        if (svg_strcasecmp(value, "currentcolor") == 0) {
            *ptype = SVG_PAINT_COLOR;
            *pcolor = style->color;
            *pgrad = NULL;
            *ppat = NULL;
            return;
        }
        char url_id[96];
        if (svg_parse_url_id(value, url_id, sizeof(url_id))) {
            const svg_gradient *grad =
                defs ? cupidimage_svg_defs_find_gradient(defs, url_id) : NULL;
            if (grad) {
                *ptype = grad->type;
                *pgrad = grad;
                *ppat = NULL;
            } else {
                const svg_pattern *pat =
                    defs ? cupidimage_svg_defs_find_pattern(defs, url_id) : NULL;
                if (pat) {
                    *ptype = SVG_PAINT_PATTERN;
                    *ppat = (svg_pattern *)pat;
                    *pgrad = NULL;
                } else {
                    *ptype = SVG_PAINT_NONE;
                    *pgrad = NULL;
                    *ppat = NULL;
                }
            }
            return;
        }
        int is_none = 0;
        uint32_t color = 0;
        if (cupidimage_svg_parse_color(value, &color, &is_none)) {
            if (is_none) {
                *ptype = SVG_PAINT_NONE;
                *pgrad = NULL;
                *ppat = NULL;
            } else {
                *ptype = SVG_PAINT_COLOR;
                *pcolor = color;
                *pgrad = NULL;
                *ppat = NULL;
            }
        }
        return;
    }
    if (prop == SVG_PROP_STROKE_WIDTH) {
        int ok = 0;
        float v = cupidimage_svg_parse_length(value, base_len, dpi, &ok);
        if (ok) {
            if (v < 0.0f) {
                v = 0.0f;
            }
            style->stroke_width = v;
        }
        return;
    }
    if (prop == SVG_PROP_STROKE_LINECAP) {
        if (svg_strcasecmp(value, "round") == 0) {
            style->stroke_linecap = SVG_LINECAP_ROUND;
        } else if (svg_strcasecmp(value, "square") == 0) {
            style->stroke_linecap = SVG_LINECAP_SQUARE;
        } else if (svg_strcasecmp(value, "butt") == 0) {
            style->stroke_linecap = SVG_LINECAP_BUTT;
        }
        return;
    }
    if (prop == SVG_PROP_STROKE_LINEJOIN) {
        if (svg_strcasecmp(value, "round") == 0) {
            style->stroke_linejoin = SVG_LINEJOIN_ROUND;
        } else if (svg_strcasecmp(value, "bevel") == 0) {
            style->stroke_linejoin = SVG_LINEJOIN_BEVEL;
        } else if (svg_strcasecmp(value, "miter") == 0) {
            style->stroke_linejoin = SVG_LINEJOIN_MITER;
        }
        return;
    }
    if (prop == SVG_PROP_STROKE_MITERLIMIT) {
        char *end = NULL;
        double v = strtod(value, &end);
        if (end != value) {
            if (v < 1.0)
                v = 1.0;
            style->stroke_miterlimit = (float)v;
        }
        return;
    }
    if (prop == SVG_PROP_STROKE_DASHARRAY) {
        if (svg_strcasecmp(value, "none") == 0) {
            style->stroke_dashcount = 0;
            return;
        }
        const char *p = value;
        int count = 0;
        while (*p) {
            float v = 0.0f;
            if (!cupidimage_svg_parse_length_value(&p, base_len, dpi, &v)) {
                break;
            }
            if (v < 0.0f) {
                v = 0.0f;
            }
            if (count < SVG_MAX_DASH) {
                style->stroke_dasharray[count++] = v;
            }
            cupidimage_svg_skip_separators(&p);
            if (!*p) {
                break;
            }
            if (*p == ',') {
                p++;
            }
        }
        if (count > 0 && (count & 1)) {
            int orig = count;
            for (int i = 0; i < orig && count < SVG_MAX_DASH; i++) {
                style->stroke_dasharray[count++] = style->stroke_dasharray[i];
            }
        }
        float sum = 0.0f;
        for (int i = 0; i < count; i++) {
            sum += style->stroke_dasharray[i];
        }
        style->stroke_dashcount = (sum > 0.0f) ? count : 0;
        return;
    }
    if (prop == SVG_PROP_STROKE_DASHOFFSET) {
        int ok = 0;
        float v = cupidimage_svg_parse_length(value, base_len, dpi, &ok);
        if (ok) {
            style->stroke_dashoffset = v;
        }
        return;
    }
    if (prop == SVG_PROP_CLIP_PATH) {
        if (svg_strcasecmp(value, "none") == 0) {
            style->clip_path = NULL;
            return;
        }
        char url_id[96];
        if (svg_parse_url_id(value, url_id, sizeof(url_id))) {
            style->clip_path =
                defs ? cupidimage_svg_defs_find_clip((svg_defs *)defs, url_id) : NULL;
        }
        return;
    }
    if (prop == SVG_PROP_MASK) {
        if (svg_strcasecmp(value, "none") == 0) {
            style->mask = NULL;
            return;
        }
        char url_id[96];
        if (svg_parse_url_id(value, url_id, sizeof(url_id))) {
            style->mask = defs ? cupidimage_svg_defs_find_mask((svg_defs *)defs, url_id) : NULL;
        }
        return;
    }
    if (prop == SVG_PROP_MARKER || prop == SVG_PROP_MARKER_START || prop == SVG_PROP_MARKER_MID ||
        prop == SVG_PROP_MARKER_END) {
        svg_marker *mk = NULL;
        if (svg_strcasecmp(value, "none") != 0) {
            char url_id[96];
            if (svg_parse_url_id(value, url_id, sizeof(url_id))) {
                mk = defs ? cupidimage_svg_defs_find_marker((svg_defs *)defs, url_id) : NULL;
            }
        }
        if (prop == SVG_PROP_MARKER) {
            style->marker_start = mk;
            style->marker_mid = mk;
            style->marker_end = mk;
        } else if (prop == SVG_PROP_MARKER_START) {
            style->marker_start = mk;
        } else if (prop == SVG_PROP_MARKER_MID) {
            style->marker_mid = mk;
        } else {
            style->marker_end = mk;
        }
        return;
    }
    if (prop == SVG_PROP_FILTER) {
        if (svg_strcasecmp(value, "none") == 0) {
            style->filter = NULL;
            return;
        }
        char url_id[96];
        if (svg_parse_url_id(value, url_id, sizeof(url_id))) {
            style->filter = defs ? cupidimage_svg_defs_find_filter(defs, url_id) : NULL;
        }
        return;
    }
    if (prop == SVG_PROP_OPACITY) {
        float o = 0.0f;
        if (cupidimage_svg_parse_opacity_value(value, &o)) {
            style->opacity = o;
        }
        return;
    }
    if (prop == SVG_PROP_FILL_OPACITY) {
        float o = 0.0f;
        if (cupidimage_svg_parse_opacity_value(value, &o)) {
            style->fill_opacity = o;
        }
        return;
    }
    if (prop == SVG_PROP_STROKE_OPACITY) {
        float o = 0.0f;
        if (cupidimage_svg_parse_opacity_value(value, &o)) {
            style->stroke_opacity = o;
        }
        return;
    }
    if (prop == SVG_PROP_FILL_RULE) {
        if (svg_strcasecmp(value, "evenodd") == 0) {
            style->fill_rule_evenodd = 1;
        } else if (svg_strcasecmp(value, "nonzero") == 0) {
            style->fill_rule_evenodd = 0;
        }
        return;
    }
    if (prop == SVG_PROP_COLOR) {
        uint32_t color = 0;
        int is_none = 0;
        if (cupidimage_svg_parse_color(value, &color, &is_none) && !is_none) {
            style->color = color;
        }
        return;
    }
    if (prop == SVG_PROP_FONT_SIZE) {
        int ok = 0;
        float v = cupidimage_svg_parse_length(value, base_len, dpi, &ok);
        if (ok && v > 0.0f) {
            style->font_size = v;
        }
        return;
    }
    if (prop == SVG_PROP_TEXT_ANCHOR) {
        if (svg_strcasecmp(value, "middle") == 0) {
            style->text_anchor = 1;
        } else if (svg_strcasecmp(value, "end") == 0) {
            style->text_anchor = 2;
        } else if (svg_strcasecmp(value, "start") == 0) {
            style->text_anchor = 0;
        }
        return;
    }
    if (prop == SVG_PROP_FONT_WEIGHT) {
        if (svg_strcasecmp(value, "bold") == 0) {
            style->bold = 1;
        } else if (isdigit((unsigned char)value[0])) {
            int weight = (int)strtol(value, NULL, 10);
            style->bold = weight >= 600;
        } else {
            style->bold = 0;
        }
        return;
    }
    if (prop == SVG_PROP_FONT_STYLE) {
        if (svg_strcasecmp(value, "italic") == 0 || svg_strcasecmp(value, "oblique") == 0) {
            style->italic = 1;
        } else if (svg_strcasecmp(value, "normal") == 0) {
            style->italic = 0;
        }
        return;
    }
    if (prop == SVG_PROP_DOMINANT_BASELINE) {
        if (svg_strcasecmp(value, "middle") == 0 || svg_strcasecmp(value, "central") == 0) {
            style->dominant_baseline = 1;
        } else if (svg_strcasecmp(value, "hanging") == 0 ||
                   svg_strcasecmp(value, "text-before-edge") == 0 ||
                   svg_strcasecmp(value, "before-edge") == 0) {
            style->dominant_baseline = 2;
        } else if (svg_strcasecmp(value, "alphabetic") == 0 || svg_strcasecmp(value, "auto") == 0 ||
                   svg_strcasecmp(value, "baseline") == 0 ||
                   svg_strcasecmp(value, "text-after-edge") == 0 ||
                   svg_strcasecmp(value, "after-edge") == 0) {
            style->dominant_baseline = 0;
        }
        return;
    }
    if (prop == SVG_PROP_LETTER_SPACING) {
        int ok = 0;
        float v = cupidimage_svg_parse_length(value, base_len, dpi, &ok);
        if (ok) {
            style->letter_spacing = v;
        }
        return;
    }
    if (prop == SVG_PROP_WORD_SPACING) {
        int ok = 0;
        float v = cupidimage_svg_parse_length(value, base_len, dpi, &ok);
        if (ok) {
            style->word_spacing = v;
        }
        return;
    }
    if (prop == SVG_PROP_DISPLAY) {
        if (svg_strcasecmp(value, "none") == 0) {
            style->display_none = 1;
        } else {
            style->display_none = 0;
        }
        return;
    }
    if (prop == SVG_PROP_VISIBILITY) {
        if (svg_strcasecmp(value, "hidden") == 0 || svg_strcasecmp(value, "collapse") == 0) {
            style->visibility_hidden = 1;
        } else if (svg_strcasecmp(value, "visible") == 0) {
            style->visibility_hidden = 0;
        }
        return;
    }
}

static void svg_style_apply_property(svg_style *style, const char *name, const char *value,
                                     float base_len, float dpi, const svg_defs *defs) {
    if (!style || !name || !value) {
        return;
    }
    int prop = cupidimage_svg_property_id(name);
    if (prop < 0) {
        return;
    }
    cupidimage_svg_style_apply_property_id(style, prop, value, base_len, dpi, defs);
}

static void svg_style_apply_style_attr(svg_style *style, const char *value, float base_len,
                                       float dpi, const svg_defs *defs) {
    if (!style || !value) {
        return;
    }
    const char *p = value;
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
            svg_style_apply_property(style, name_buf, value_buf, base_len, dpi, defs);
        }
        if (*p == ';') {
            p++;
        }
    }
}

void cupidimage_svg_blend_premul(uint8_t *dst, uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa) {
    uint32_t inv = 255u - sa;
    dst[0] = (uint8_t)(sr + (uint32_t)dst[0] * inv / 255u);
    dst[1] = (uint8_t)(sg + (uint32_t)dst[1] * inv / 255u);
    dst[2] = (uint8_t)(sb + (uint32_t)dst[2] * inv / 255u);
    dst[3] = (uint8_t)(sa + (uint32_t)dst[3] * inv / 255u);
}

void cupidimage_svg_apply_presentation_attrs(svg_style *style, const svg_tag *tag, float base_len,
                                             float dpi, const svg_defs *defs) {
    for (int i = 0; i < tag->attr_count; i++) {
        const char *name = svg_local_name(tag->attrs[i].name);
        const char *value = tag->attrs[i].value;
        if (!name || !value) {
            continue;
        }
        if (strcmp(name, "style") == 0) {
            continue;
        }
        svg_style_apply_property(style, name, value, base_len, dpi, defs);
    }
}

void cupidimage_svg_apply_inline_style_attr(svg_style *style, const svg_tag *tag, float base_len,
                                            float dpi, const svg_defs *defs) {
    const char *style_attr = cupidimage_svg_get_attr(tag, "style");
    if (style_attr) {
        svg_style_apply_style_attr(style, style_attr, base_len, dpi, defs);
    }
}

int cupidimage_svg_parse_transform(const char *value, svg_matrix *out) {
    if (!value || !out) {
        return 0;
    }
    svg_matrix m;
    svg_matrix_identity(&m);
    const char *p = value;
    while (*p) {
        while (isspace((unsigned char)*p) || *p == ',') {
            p++;
        }
        if (!*p) {
            break;
        }
        if (strncmp(p, "matrix", 6) == 0) {
            p += 6;
            while (*p && *p != '(')
                p++;
            if (*p != '(')
                break;
            p++;
            float v[6];
            for (int i = 0; i < 6; i++) {
                if (!cupidimage_svg_parse_transform_number(&p, &v[i])) {
                    return 0;
                }
            }
            svg_matrix t = {v[0], v[1], v[2], v[3], v[4], v[5]};
            svg_matrix_multiply(&t, &m, &m);
        } else if (strncmp(p, "translate", 9) == 0) {
            p += 9;
            while (*p && *p != '(')
                p++;
            if (*p != '(')
                break;
            p++;
            float tx = 0.0f, ty = 0.0f;
            cupidimage_svg_parse_transform_number(&p, &tx);
            if (!cupidimage_svg_parse_transform_number(&p, &ty)) {
                ty = 0.0f;
            }
            svg_matrix t;
            svg_matrix_identity(&t);
            t.e = tx;
            t.f = ty;
            svg_matrix_multiply(&t, &m, &m);
        } else if (strncmp(p, "scale", 5) == 0) {
            p += 5;
            while (*p && *p != '(')
                p++;
            if (*p != '(')
                break;
            p++;
            float sx = 1.0f, sy = 1.0f;
            cupidimage_svg_parse_transform_number(&p, &sx);
            if (!cupidimage_svg_parse_transform_number(&p, &sy)) {
                sy = sx;
            }
            svg_matrix t;
            svg_matrix_identity(&t);
            t.a = sx;
            t.d = sy;
            svg_matrix_multiply(&t, &m, &m);
        } else if (strncmp(p, "rotate", 6) == 0) {
            p += 6;
            while (*p && *p != '(')
                p++;
            if (*p != '(')
                break;
            p++;
            float angle = 0.0f;
            if (!cupidimage_svg_parse_transform_number(&p, &angle)) {
                return 0;
            }
            float cx = 0.0f, cy = 0.0f;
            int has_center = cupidimage_svg_parse_transform_number(&p, &cx);
            if (has_center) {
                if (!cupidimage_svg_parse_transform_number(&p, &cy)) {
                    cy = 0.0f;
                }
            }
            svg_matrix t;
            svg_matrix_identity(&t);
            if (has_center) {
                svg_matrix_translate(&t, cx, cy);
                svg_matrix_rotate(&t, angle);
                svg_matrix_translate(&t, -cx, -cy);
            } else {
                svg_matrix_rotate(&t, angle);
            }
            svg_matrix_multiply(&t, &m, &m);
        } else if (strncmp(p, "skewx", 5) == 0) {
            p += 5;
            while (*p && *p != '(')
                p++;
            if (*p != '(')
                break;
            p++;
            float angle = 0.0f;
            if (!cupidimage_svg_parse_transform_number(&p, &angle)) {
                return 0;
            }
            svg_matrix t;
            svg_matrix_identity(&t);
            svg_matrix_skewx(&t, angle);
            svg_matrix_multiply(&t, &m, &m);
        } else if (strncmp(p, "skewy", 5) == 0) {
            p += 5;
            while (*p && *p != '(')
                p++;
            if (*p != '(')
                break;
            p++;
            float angle = 0.0f;
            if (!cupidimage_svg_parse_transform_number(&p, &angle)) {
                return 0;
            }
            svg_matrix t;
            svg_matrix_identity(&t);
            svg_matrix_skewy(&t, angle);
            svg_matrix_multiply(&t, &m, &m);
        } else {
            break;
        }
        while (*p && *p != ')')
            p++;
        if (*p == ')') {
            p++;
        }
    }
    *out = m;
    return 1;
}

int cupidimage_svg_next_tag(char **p, svg_tag *tag) {
    char *s = *p;
    while (s && *s) {
        char *lt = strchr(s, '<');
        if (!lt) {
            *p = s + strlen(s);
            return 0;
        }
        s = lt + 1;
        if (*s == '!') {
            if (strncmp(s, "!--", 3) == 0) {
                char *end = strstr(s + 3, "-->");
                if (!end) {
                    *p = s + strlen(s);
                    return 0;
                }
                s = end + 3;
                continue;
            }
            if (strncmp(s, "![CDATA[", 8) == 0) {
                char *end = strstr(s + 8, "]]>");
                if (!end) {
                    *p = s + strlen(s);
                    return 0;
                }
                s = end + 3;
                continue;
            }
            char *end = strchr(s, '>');
            if (!end) {
                *p = s + strlen(s);
                return 0;
            }
            s = end + 1;
            continue;
        }
        if (*s == '?') {
            char *end = strstr(s, "?>");
            if (!end) {
                *p = s + strlen(s);
                return 0;
            }
            s = end + 2;
            continue;
        }
        break;
    }
    if (!s || !*s) {
        *p = s;
        return 0;
    }
    memset(tag, 0, sizeof(*tag));
    if (*s == '/') {
        tag->is_end = 1;
        s++;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    char *name_start = s;
    while (*s && svg_is_name_char(*s)) {
        s++;
    }
    if (s == name_start) {
        *p = s;
        return 0;
    }
    char *name_end = s;
    if (tag->is_end) {
        char *end = name_end;
        while (*end && *end != '>') {
            end++;
        }
        if (!*end) {
            *p = end;
            return 0;
        }
        *name_end = '\0';
        svg_lowercase(name_start);
        tag->name = name_start;
        *p = end + 1;
        return 1;
    }
    if (*s) {
        *s++ = '\0';
    }
    svg_lowercase(name_start);
    tag->name = name_start;
    while (*s) {
        while (isspace((unsigned char)*s)) {
            s++;
        }
        if (*s == '/' || *s == '>') {
            break;
        }
        if (!*s) {
            break;
        }
        if (tag->attr_count >= SVG_MAX_ATTR) {
            while (*s && *s != '>' && *s != '/') {
                s++;
            }
            break;
        }
        char *attr_name = s;
        while (*s && svg_is_name_char(*s)) {
            s++;
        }
        if (s == attr_name) {
            break;
        }
        char *attr_name_end = s;
        int had_eq = (*s == '=');
        if (*s) {
            *s++ = '\0';
        }
        svg_lowercase(attr_name);
        while (isspace((unsigned char)*s)) {
            s++;
        }
        char *attr_value = (char *)"";
        if (had_eq || *s == '=') {
            if (!had_eq) {
                s++;
            }
            while (isspace((unsigned char)*s)) {
                s++;
            }
            if (*s == '"' || *s == '\'') {
                char quote = *s++;
                attr_value = s;
                while (*s && *s != quote) {
                    s++;
                }
                if (*s) {
                    *s++ = '\0';
                }
            } else {
                attr_value = s;
                while (*s && !isspace((unsigned char)*s) && *s != '>' && *s != '/') {
                    s++;
                }
                if (*s) {
                    *s++ = '\0';
                }
            }
        }
        tag->attrs[tag->attr_count].name = attr_name;
        tag->attrs[tag->attr_count].value = attr_value;
        tag->attr_count++;
        if (s == attr_name_end) {
            break;
        }
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '/') {
        tag->is_self_closing = 1;
        s++;
    }
    if (*s == '>') {
        s++;
    }
    *p = s;
    return 1;
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
