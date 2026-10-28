#include "pdf_content_syntax.h"
#include "pdf_render_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INLINE_DICT_LIMIT 65536u
#define INLINE_DATA_LIMIT (64u * 1024u * 1024u)

typedef struct {
    const unsigned char *data;
    size_t size, pos, used;
    char *output;
} inline_parser;

static int inline_append(inline_parser *p, const void *bytes, size_t size) {
    if (size > INLINE_DICT_LIMIT - p->used)
        return 0;
    memcpy(p->output + p->used, bytes, size);
    p->used += size;
    return 1;
}

static int inline_text(inline_parser *p, const char *text) {
    return inline_append(p, text, strlen(text));
}

static int inline_hex(unsigned char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                  : -1;
}

static int inline_name(inline_parser *p, char name[128]) {
    if (p->pos == p->size || p->data[p->pos++] != '/')
        return 0;
    size_t n = 0;
    while (p->pos < p->size && !cupidimage_pdf_is_ws(p->data[p->pos]) &&
           !cupidimage_pdf_is_delim(p->data[p->pos])) {
        unsigned char c = p->data[p->pos++];
        if (c == '#') {
            if (p->size - p->pos < 2)
                return 0;
            int a = inline_hex(p->data[p->pos]), b = inline_hex(p->data[p->pos + 1]);
            if (a < 0 || b < 0)
                return 0;
            c = (unsigned char)(16 * a + b);
            p->pos += 2;
        }
        if (!c || n == 127)
            return 0;
        name[n++] = (char)c;
    }
    name[n] = 0;
    return n != 0;
}

static const char *inline_key(const char *name) {
    static const char *keys[][2] = {
        {"BPC", "BitsPerComponent"}, {"CS", "ColorSpace"}, {"D", "Decode"},
        {"DP", "DecodeParms"},       {"F", "Filter"},      {"H", "Height"},
        {"IM", "ImageMask"},         {"I", "Interpolate"}, {"W", "Width"},
        {"Intent", "Intent"}};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        if (!strcmp(name, keys[i][0]) || !strcmp(name, keys[i][1]))
            return keys[i][1];
    return NULL;
}

static const char *inline_alias(const char *name, int kind) {
    static const char *colors[][2] = {
        {"G", "DeviceGray"}, {"RGB", "DeviceRGB"}, {"CMYK", "DeviceCMYK"}, {"I", "Indexed"}};
    static const char *filters[][2] = {{"AHx", "ASCIIHexDecode"}, {"A85", "ASCII85Decode"},
                                       {"LZW", "LZWDecode"},      {"Fl", "FlateDecode"},
                                       {"RL", "RunLengthDecode"}, {"CCF", "CCITTFaxDecode"},
                                       {"DCT", "DCTDecode"}};
    if (kind == 1)
        for (size_t i = 0; i < sizeof(colors) / sizeof(colors[0]); i++)
            if (!strcmp(name, colors[i][0]))
                return colors[i][1];
    if (kind == 2)
        for (size_t i = 0; i < sizeof(filters) / sizeof(filters[0]); i++)
            if (!strcmp(name, filters[i][0]))
                return filters[i][1];
    return name;
}

/* Copy one direct PDF value. Strings remain opaque, so binary palettes cannot
   be mistaken for delimiters or have their contents expanded as names. */
static int inline_value(inline_parser *p, unsigned depth, int aliases) {
    if (depth > 16)
        return 0;
    cupidimage_pdf_skip_ws_and_comments(p->data, p->size, &p->pos);
    if (p->pos == p->size)
        return 0;
    size_t start = p->pos;
    unsigned char c = p->data[p->pos];
    if (c == '/') {
        char name[128];
        if (!inline_name(p, name))
            return 0;
        const char *expanded = inline_alias(name, aliases);
        if (expanded == name)
            return inline_append(p, p->data + start, p->pos - start) && inline_text(p, " ");
        return inline_text(p, "/") && inline_text(p, expanded) && inline_text(p, " ");
    }
    if (c == '(') {
        unsigned nesting = 1;
        p->pos++;
        while (p->pos < p->size && nesting) {
            c = p->data[p->pos++];
            if (c == '\\') {
                if (p->pos == p->size)
                    return 0;
                p->pos++;
            } else if (c == '(') {
                if (++nesting > 64)
                    return 0;
            } else if (c == ')')
                nesting--;
        }
        return !nesting && inline_append(p, p->data + start, p->pos - start) && inline_text(p, " ");
    }
    if (c == '[' || (c == '<' && p->size - p->pos >= 2 && p->data[p->pos + 1] == '<')) {
        int dict = c == '<';
        p->pos += dict ? 2 : 1;
        if (!inline_text(p, dict ? "<< " : "[ "))
            return 0;
        for (;;) {
            cupidimage_pdf_skip_ws_and_comments(p->data, p->size, &p->pos);
            if (p->pos == p->size)
                return 0;
            if ((!dict && p->data[p->pos] == ']') ||
                (dict && p->size - p->pos >= 2 && p->data[p->pos] == '>' &&
                 p->data[p->pos + 1] == '>')) {
                p->pos += dict ? 2 : 1;
                return inline_text(p, dict ? ">> " : "] ");
            }
            if (!inline_value(p, depth + 1, aliases))
                return 0;
        }
    }
    if (c == '<') {
        p->pos++;
        while (p->pos < p->size && p->data[p->pos] != '>') {
            if (!cupidimage_pdf_is_ws(p->data[p->pos]) && inline_hex(p->data[p->pos]) < 0)
                return 0;
            p->pos++;
        }
        if (p->pos == p->size)
            return 0;
        p->pos++;
    } else {
        while (p->pos < p->size && !cupidimage_pdf_is_ws(p->data[p->pos]) &&
               !cupidimage_pdf_is_delim(p->data[p->pos]))
            p->pos++;
        if (p->pos == start)
            return 0;
    }
    return inline_append(p, p->data + start, p->pos - start) && inline_text(p, " ");
}

/* Resource names are legal for inline ColorSpace entries, but are not
   inherited by image XObjects. Resolve only this inline dictionary value. */
static int inline_color_resource(inline_parser *p, pdf_render_state *st, size_t start) {
    inline_parser value = {(const unsigned char *)p->output, p->used, start, 0, NULL};
    if (value.pos == value.size || value.data[value.pos] != '/')
        return 1;
    char name[128];
    if (!inline_name(&value, name))
        return 0;
    if (!strcmp(name, "DeviceGray") || !strcmp(name, "DeviceRGB") || !strcmp(name, "DeviceCMYK"))
        return 1;
    pdf_image_colors colors = {0};
    if (!cupidimage_pdf_color_space(st, name, 0, &colors))
        return 0;
    if (colors.calibrated || colors.icc || colors.tint) {
        pdf_object_view resource = {0};
        if (!cupidimage_pdf_find_resource(st, "ColorSpace", name, &resource, NULL))
            return 0;
        pdf_object_view exact = {0};
        int ok =
            cupidimage_pdf_resolve_value(st->document, st->document_size, &resource, 0, &exact, 0);
        char *array = NULL;
        size_t at = 0, length = 0;
        cupidimage_pdf_skip_ws_and_comments(exact.data, exact.len, &at);
        p->used = start;
        if (ok && at < exact.len && exact.data[at] == '/') {
            char device[128];
            ok = cupidimage_pdf_read_name(&exact, &at, device) &&
                 (!strcmp(device, "DeviceGray") || !strcmp(device, "DeviceRGB") ||
                  !strcmp(device, "DeviceCMYK"));
            ok = ok && inline_text(p, "/") && inline_text(p, device) && inline_text(p, " ");
        } else {
            ok = ok && cupidimage_pdf_content_array(exact.data, exact.len, &at, &array, &length);
            ok = ok && inline_text(p, "[") && inline_append(p, array, length) &&
                 inline_text(p, "] ");
        }
        free(array);
        cupidimage_pdf_object_view_release(&exact);
        cupidimage_pdf_object_view_release(&resource);
        return ok;
    }
    const char *base = colors.components == 1   ? "/DeviceGray "
                       : colors.components == 3 ? "/DeviceRGB "
                       : colors.components == 4 ? "/DeviceCMYK "
                                                : NULL;
    if (!base)
        return 0;
    p->used = start;
    if (!colors.indexed)
        return inline_text(p, base);
    char number[24];
    snprintf(number, sizeof(number), "%d <", colors.high);
    if (!inline_text(p, "[/Indexed ") || !inline_text(p, base) || !inline_text(p, number))
        return 0;
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < (colors.high + 1) * colors.components; i++) {
        char pair[2] = {hex[colors.palette[i] >> 4], hex[colors.palette[i] & 15]};
        if (!inline_append(p, pair, 2))
            return 0;
    }
    return inline_text(p, ">] ");
}

static int inline_raw_size(pdf_render_state *st, const pdf_object_view *view, size_t *bytes) {
    int width, height, bits = 1, stencil = 0;
    size_t pos;
    if (cupidimage_pdf_page_value(view, "ImageMask", &pos)) {
        size_t n = view->len - pos;
        stencil = n >= 4 && !memcmp(view->data + pos, "true", 4) &&
                  (n == 4 || cupidimage_pdf_is_ws(view->data[pos + 4]) ||
                   cupidimage_pdf_is_delim(view->data[pos + 4]));
    }
    pdf_image_colors colors = {0};
    if (!cupidimage_pdf_image_integer(st, view, "Width", &width) ||
        !cupidimage_pdf_image_integer(st, view, "Height", &height) || width < 1 || height < 1)
        return 0;
    if (!stencil && (!cupidimage_pdf_image_integer(st, view, "BitsPerComponent", &bits) ||
                     !cupidimage_pdf_page_value(view, "ColorSpace", &pos) ||
                     !cupidimage_pdf_image_color_space(st, view, &pos, 0, &colors)))
        return 0;
    if (bits != 1 && bits != 2 && bits != 4 && bits != 8 && bits != 16)
        return 0;
    int components = stencil || colors.indexed ? 1 : colors.components;
    if (components < 1 || components > PDF_COLOR_COMPONENTS ||
        (size_t)width > INLINE_DATA_LIMIT / (unsigned)components / (unsigned)bits)
        return 0;
    size_t row = ((size_t)width * components * bits + 7) / 8;
    if ((size_t)height > INLINE_DATA_LIMIT / row)
        return 0;
    *bytes = row * (size_t)height;
    return 1;
}

/* Walk JPEG segments and entropy escapes; marker-looking bytes inside APP or
   comment payloads are data, not an image boundary. */
static int inline_jpeg_end(const unsigned char *data, size_t size, size_t *end) {
    if (size < 2 || data[0] != 255 || data[1] != 216)
        return 0;
    size_t pos = 2;
    int entropy = 0;
    while (pos < size) {
        if (entropy) {
            while (pos < size && data[pos] != 255)
                pos++;
            if (size - pos < 2)
                return 0;
            size_t marker = pos + 1;
            while (marker < size && data[marker] == 255)
                marker++;
            if (marker == size)
                return 0;
            if (data[marker] == 0 || (data[marker] >= 208 && data[marker] <= 215)) {
                pos = marker + 1;
                continue;
            }
            entropy = 0;
        }
        if (data[pos++] != 255)
            return 0;
        while (pos < size && data[pos] == 255)
            pos++;
        if (pos == size)
            return 0;
        unsigned marker = data[pos++];
        if (marker == 217) {
            *end = pos;
            return 1;
        }
        if (marker == 0 || marker == 216 || (marker >= 208 && marker <= 215))
            return 0;
        if (marker == 1)
            continue;
        if (size - pos < 2)
            return 0;
        size_t length = (size_t)data[pos] * 256 + data[pos + 1];
        if (length < 2 || length > size - pos)
            return 0;
        pos += length;
        if (marker == 218)
            entropy = 1;
    }
    return 0;
}

int cupidimage_pdf_draw_inline_image(pdf_render_state *st, const unsigned char *data, size_t length,
                                     size_t *position) {
    char *dictionary = malloc(INLINE_DICT_LIMIT);
    unsigned char *object = NULL;
    int ok = 0;
    inline_parser p = {data, length, *position, 0, dictionary};
    if (!dictionary || !inline_text(&p, "<< /Subtype /Image "))
        goto done;
    for (unsigned entries = 0;; entries++) {
        if (entries == 128)
            goto done;
        cupidimage_pdf_skip_ws_and_comments(data, length, &p.pos);
        if (length - p.pos >= 2 && !memcmp(data + p.pos, "ID", 2) && length - p.pos > 2 &&
            cupidimage_pdf_is_ws(data[p.pos + 2])) {
            p.pos += 2;
            break;
        }
        char name[128];
        if (!inline_name(&p, name))
            goto done;
        const char *key = inline_key(name);
        size_t saved = p.used;
        if (key && (!inline_text(&p, "/") || !inline_text(&p, key) || !inline_text(&p, " ")))
            goto done;
        size_t value_start = p.used;
        if (!inline_value(&p, 0,
                          key && !strcmp(key, "ColorSpace") ? 1
                          : key && !strcmp(key, "Filter")   ? 2
                                                            : 0))
            goto done;
        if (key && !strcmp(key, "ColorSpace") && !inline_color_resource(&p, st, value_start))
            goto done;
        if (!key)
            p.used = saved;
    }
    size_t dict_end = p.used;
    if (!inline_text(&p, ">>"))
        goto done;
    pdf_object_view view = {(const unsigned char *)dictionary, p.used, NULL, st->xref};
    size_t decoded_size;
    if (!inline_raw_size(st, &view, &decoded_size))
        goto done;
    /* Consume exactly the separator, preserving whitespace-valued samples. */
    if (data[p.pos++] == '\r' && p.pos < length && data[p.pos] == '\n')
        p.pos++;
    size_t start = p.pos, encoded_size = decoded_size, filter_pos;
    if (cupidimage_pdf_page_value(&view, "Filter", &filter_pos)) {
        inline_parser f = {view.data, view.len, filter_pos, 0, NULL};
        if (f.pos < f.size && f.data[f.pos] == '[') {
            f.pos++;
            cupidimage_pdf_skip_ws_and_comments(f.data, f.size, &f.pos);
        }
        char filter[128];
        if (!inline_name(&f, filter))
            goto done;
        if (!strcmp(filter, "ASCIIHexDecode")) {
            while (p.pos < length && data[p.pos] != '>')
                p.pos++;
            if (p.pos == length)
                goto done;
            p.pos++;
        } else if (!strcmp(filter, "ASCII85Decode")) {
            while (length - p.pos >= 2 && !(data[p.pos] == '~' && data[p.pos + 1] == '>'))
                p.pos++;
            if (length - p.pos < 2)
                goto done;
            p.pos += 2;
        } else if (!strcmp(filter, "RunLengthDecode")) {
            for (;;) {
                if (p.pos == length)
                    goto done;
                unsigned code = data[p.pos++];
                if (code == 128)
                    break;
                size_t bytes = code < 128 ? code + 1 : 1;
                if (bytes > length - p.pos)
                    goto done;
                p.pos += bytes;
            }
        } else if (!strcmp(filter, "DCTDecode")) {
            size_t consumed = 0;
            if (!inline_jpeg_end(data + start, length - start, &consumed))
                goto done;
            p.pos = start + consumed;
        } else if (!strcmp(filter, "FlateDecode")) {
            size_t capacity = 4096, decoded = 0, consumed = 0;
            int found = 0;
            for (;;) {
                unsigned char *scratch = malloc(capacity);
                if (!scratch)
                    goto done;
                found = cupidimage_inflate_zlib_prefix(data + start, length - start, scratch,
                                                       capacity, &decoded, &consumed);
                free(scratch);
                if (found || capacity == INLINE_DATA_LIMIT)
                    break;
                capacity *= 2;
            }
            if (!found)
                goto done;
            p.pos = start + consumed;
        } else
            goto done; /* Other encoded boundary formats need explicit parsing. */
        encoded_size = p.pos - start;
    } else {
        if (encoded_size > length - start)
            goto done;
        p.pos += encoded_size;
    }
    if (encoded_size > INLINE_DATA_LIMIT || p.pos == length || !cupidimage_pdf_is_ws(data[p.pos]))
        goto done;
    cupidimage_pdf_skip_ws_and_comments(data, length, &p.pos);
    if (length - p.pos < 2 || memcmp(data + p.pos, "EI", 2) ||
        (length - p.pos > 2 && !cupidimage_pdf_is_ws(data[p.pos + 2]) &&
         !cupidimage_pdf_is_delim(data[p.pos + 2])))
        goto done;
    p.pos += 2;
    p.used = dict_end;
    char suffix[80];
    int n = snprintf(suffix, sizeof(suffix), "/Length %zu >>\nstream\n", encoded_size);
    if (n < 0 || (size_t)n >= sizeof(suffix) || !inline_text(&p, suffix))
        goto done;
    size_t prefix = p.used;
    object = malloc(prefix + encoded_size + 11);
    if (!object)
        goto done;
    memcpy(object, dictionary, prefix);
    memcpy(object + prefix, data + start, encoded_size);
    memcpy(object + prefix + encoded_size, "\nendstream", 10);
    view.data = object;
    view.len = prefix + encoded_size + 10;
    ok = cupidimage_pdf_draw_image_view(st, &view);
    if (ok)
        *position = p.pos;
done:
    free(object);
    free(dictionary);
    return ok;
}
