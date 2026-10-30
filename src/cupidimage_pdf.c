#include "cupidimage.h"
#include "cupidimage_internal.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t errcap, const char *msg) {
    if (err && errcap) {
        snprintf(err, errcap, "%s", msg);
    }
}

typedef struct {
    int obj_num;
    int gen_num;
    size_t offset;
} pdf_xref_entry;

typedef struct {
    pdf_xref_entry *entries;
    int count;
    int root_obj;
    int info_obj;
} pdf_xref;

typedef struct {
    const unsigned char *data;
    size_t len;
    unsigned char *owned;
} pdf_object_view;

typedef struct {
    double x0, y0, x1, y1;
    int rotation;
    int width, height;
} pdf_page_geometry;

typedef struct {
    uint32_t src_code;
    uint32_t dst_codepoint;
    uint8_t src_len;
} pdf_tounicode_entry;

typedef struct {
    char name[32];
    pdf_tounicode_entry *entries;
    int count;
    int cap;
    int max_src_len;
} pdf_font_map;

typedef struct {
    pdf_font_map *fonts;
    int count;
    int cap;
} pdf_font_table;

static int pdf_inflate_auto(const unsigned char *data, size_t size, unsigned char **out_data,
                            size_t *out_len);
static int extract_dict_int(const unsigned char *data, size_t start, size_t end, const char *key,
                            int *value);
static int buffer_contains(const unsigned char *data, size_t start, size_t end, const char *needle);
static int extract_stream(const unsigned char *data, size_t obj_start, size_t obj_end,
                          const unsigned char **stream_data, size_t *stream_len,
                          size_t *stream_keyword_pos);
static int pdf_is_delim(unsigned char c);
static void pdf_skip_ws_and_comments(const unsigned char *data, size_t size, size_t *pos);

static int pdf_parse_decimal(const unsigned char *data, size_t end, size_t *pos, size_t limit,
                             size_t *out) {
    size_t p = *pos, value = 0;
    if (p >= end || data[p] < '0' || data[p] > '9')
        return 0;
    while (p < end && data[p] >= '0' && data[p] <= '9') {
        unsigned digit = data[p++] - '0';
        if (value > limit / 10 || (value == limit / 10 && digit > limit % 10))
            return 0;
        value = value * 10 + digit;
    }
    *out = value;
    *pos = p;
    return 1;
}

static int find_eof_marker(const unsigned char *data, size_t size, size_t *eof_pos) {
    if (size < 6)
        return 0;
    for (size_t i = size - 6; i > 0; i--) {
        if (memcmp(&data[i], "%%EOF", 5) == 0) {
            *eof_pos = i;
            return 1;
        }
    }
    return 0;
}

static int find_startxref(const unsigned char *data, size_t eof_pos, size_t *xref_offset) {
    if (eof_pos < 9)
        return 0;
    size_t i = eof_pos - 9;
    do {
        if (memcmp(&data[i], "startxref", 9) == 0) {
            size_t j = i + 9;
            if (j == eof_pos || !isspace(data[j]))
                return 0;
            while (j < eof_pos && isspace(data[j]))
                j++;
            if (!pdf_parse_decimal(data, eof_pos, &j, SIZE_MAX, xref_offset))
                return 0;
            while (j < eof_pos && isspace(data[j]))
                j++;
            return j == eof_pos;
        }
    } while (i-- > 0);
    return 0;
}

static int pdf_check_header(const unsigned char *data, size_t size) {
    if (size < 8)
        return 0;
    if (memcmp(data, "%PDF-", 5) != 0)
        return 0;
    return 1;
}

static int parse_xref_table(const unsigned char *data, size_t size, size_t xref_pos, pdf_xref *xref,
                            char *err, size_t errcap) {
    if (xref_pos > size || size - xref_pos < 4) {
        set_err(err, errcap, "pdf: xref offset out of bounds");
        return 0;
    }

    if (memcmp(&data[xref_pos], "xref", 4) != 0) {
        xref->entries = (pdf_xref_entry *)calloc(20, sizeof(pdf_xref_entry));
        if (!xref->entries) {
            set_err(err, errcap, "pdf: out of memory");
            return 0;
        }
        xref->count = 20;
        for (int i = 0; i < 20; i++) {
            xref->entries[i].obj_num = i;
            xref->entries[i].gen_num = 0;
            xref->entries[i].offset = 0;
        }
        return 1;
    }

    size_t pos = xref_pos + 4;
    while (pos < size && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r'))
        pos++;

    size_t first_obj, count;
    if (!pdf_parse_decimal(data, size, &pos, INT_MAX, &first_obj)) {
        set_err(err, errcap, "pdf: invalid xref object number");
        return 0;
    }
    while (pos < size && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r'))
        pos++;
    if (!pdf_parse_decimal(data, size, &pos, 100000, &count)) {
        set_err(err, errcap, "pdf: invalid xref count");
        return 0;
    }
    while (pos < size && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r'))
        pos++;

    if (count == 0 || count - 1 > INT_MAX - first_obj) {
        set_err(err, errcap, "pdf: invalid xref count");
        return 0;
    }

    xref->entries = (pdf_xref_entry *)calloc((size_t)count, sizeof(pdf_xref_entry));
    if (!xref->entries) {
        set_err(err, errcap, "pdf: out of memory");
        return 0;
    }
    xref->count = (int)count;

    for (int i = 0; i < xref->count; i++) {
        size_t offset, gen;
        char inuse = 'f';

        if (!pdf_parse_decimal(data, size, &pos, SIZE_MAX, &offset))
            goto invalid_entry;
        while (pos < size && data[pos] == ' ')
            pos++;

        if (!pdf_parse_decimal(data, size, &pos, 65535, &gen))
            goto invalid_entry;
        while (pos < size && data[pos] == ' ')
            pos++;

        if (pos == size || (data[pos] != 'f' && data[pos] != 'n'))
            goto invalid_entry;
        inuse = (char)data[pos++];
        while (pos < size && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r'))
            pos++;

        xref->entries[i].obj_num = (int)first_obj + i;
        xref->entries[i].gen_num = (int)gen;
        xref->entries[i].offset = (inuse == 'n') ? offset : 0;
    }

    return 1;
invalid_entry:
    free(xref->entries);
    xref->entries = NULL;
    xref->count = 0;
    set_err(err, errcap, "pdf: invalid xref entry");
    return 0;
}

static int parse_trailer(const unsigned char *data, size_t size, size_t xref_pos, pdf_xref *xref,
                         char *err, size_t errcap) {
    size_t pos = xref_pos;
    size_t trailer_pos = 0;
    size_t search_limit = xref_pos + (size - xref_pos < 10000 ? size - xref_pos : 10000);

    while (pos + 7 <= search_limit) {
        if (memcmp(&data[pos], "trailer", 7) == 0) {
            trailer_pos = pos;
            break;
        }
        pos++;
    }

    if (trailer_pos == 0) {
        pos = xref_pos;
        while (pos < search_limit && data[pos] != '<')
            pos++;
        if (pos + 1 >= search_limit || data[pos + 1] != '<') {
            set_err(err, errcap, "pdf: trailer dictionary not found");
            return 0;
        }
        pos += 2;
    } else {
        pos = trailer_pos + 7;
        while (pos < size && data[pos] != '<')
            pos++;
        if (pos + 1 >= size || data[pos + 1] != '<') {
            set_err(err, errcap, "pdf: trailer dictionary not found");
            return 0;
        }
        pos += 2;
    }

    xref->root_obj = 1;
    while (pos + 1 < size) {
        if (data[pos] == '>' && data[pos + 1] == '>')
            break;

        if (pos + 5 <= size && memcmp(&data[pos], "/Root", 5) == 0) {
            pos += 5;
            while (pos < size && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r'))
                pos++;
            size_t obj_num;
            if (!pdf_parse_decimal(data, size, &pos, INT_MAX, &obj_num) || obj_num == 0) {
                set_err(err, errcap, "pdf: invalid catalog object number");
                return 0;
            }
            xref->root_obj = (int)obj_num;
        }
        pos++;
    }

    return 1;
}

static void pdf_object_view_release(pdf_object_view *view) {
    if (!view)
        return;
    free(view->owned);
    view->owned = NULL;
    view->data = NULL;
    view->len = 0;
}

static int parse_obj_header_at(const unsigned char *data, size_t size, size_t pos, int *obj_num,
                               size_t *header_end) {
    size_t p = pos;
    if (p >= size || data[p] < '0' || data[p] > '9') {
        return 0;
    }
    if (p > 0 && data[p - 1] != '\n' && data[p - 1] != '\r') {
        return 0;
    }
    int num = 0;
    while (p < size && data[p] >= '0' && data[p] <= '9') {
        if (num > (INT_MAX - (data[p] - '0')) / 10)
            return 0;
        num = num * 10 + (data[p] - '0');
        p++;
    }
    if (p >= size || data[p] != ' ') {
        return 0;
    }
    while (p < size && data[p] == ' ')
        p++;
    if (p >= size || data[p] < '0' || data[p] > '9') {
        return 0;
    }
    while (p < size && data[p] >= '0' && data[p] <= '9')
        p++;
    if (p >= size || data[p] != ' ') {
        return 0;
    }
    while (p < size && data[p] == ' ')
        p++;
    if (p + 3 > size || memcmp(&data[p], "obj", 3) != 0) {
        return 0;
    }
    p += 3;
    if (p < size && !isspace((unsigned char)data[p])) {
        return 0;
    }
    *obj_num = num;
    *header_end = p;
    return 1;
}

static int find_endobj(const unsigned char *data, size_t size, size_t from, size_t *endobj_pos) {
    size_t p = from;
    while (p + 6 <= size) {
        if (memcmp(&data[p], "stream", 6) == 0) {
            int preceded_ok =
                (p == from) || isspace((unsigned char)data[p - 1]) || data[p - 1] == '>';
            if (preceded_ok) {
                size_t stream_start = p + 6;
                if (stream_start < size && data[stream_start] == '\r')
                    stream_start++;
                if (stream_start < size && data[stream_start] == '\n')
                    stream_start++;

                int declared_len = 0;
                if (extract_dict_int(data, from, p, "/Length", &declared_len) && declared_len > 0 &&
                    stream_start + (size_t)declared_len <= size) {
                    p = stream_start + (size_t)declared_len;
                    while (p < size && (data[p] == ' ' || data[p] == '\t' || data[p] == '\r' ||
                                        data[p] == '\n'))
                        p++;
                    if (p + 9 <= size && memcmp(&data[p], "endstream", 9) == 0) {
                        p += 9;
                    }
                    continue;
                }

                size_t q = stream_start;
                while (q + 9 <= size && memcmp(&data[q], "endstream", 9) != 0)
                    q++;
                if (q + 9 <= size) {
                    p = q + 9;
                    continue;
                }
                return 0;
            }
        }
        if (memcmp(&data[p], "endobj", 6) == 0) {
            *endobj_pos = p;
            return 1;
        }
        p++;
    }
    return 0;
}

static int extract_dict_int(const unsigned char *data, size_t start, size_t end, const char *key,
                            int *value) {
    size_t key_len = strlen(key);
    if (end <= start || end - start <= key_len) {
        return 0;
    }
    for (size_t pos = start; pos + key_len <= end; pos++) {
        if (memcmp(&data[pos], key, key_len) != 0) {
            continue;
        }
        pos += key_len;
        while (pos < end &&
               (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' || data[pos] == '\t'))
            pos++;
        int sign = 1;
        int v = 0;
        if (pos < end && data[pos] == '-') {
            sign = -1;
            pos++;
        }
        int got = 0;
        while (pos < end && data[pos] >= '0' && data[pos] <= '9') {
            got = 1;
            if (v > (INT_MAX - (data[pos] - '0')) / 10)
                return 0;
            v = v * 10 + (data[pos] - '0');
            pos++;
        }
        if (got) {
            size_t look = pos;
            while (look < end && (data[look] == ' ' || data[look] == '\n' || data[look] == '\r' ||
                                  data[look] == '\t')) {
                look++;
            }
            if (look < end && data[look] >= '0' && data[look] <= '9') {
                while (look < end && data[look] >= '0' && data[look] <= '9') {
                    look++;
                }
                while (look < end && (data[look] == ' ' || data[look] == '\n' ||
                                      data[look] == '\r' || data[look] == '\t')) {
                    look++;
                }
                if (look < end && data[look] == 'R') {
                    continue;
                }
            }
            *value = v * sign;
            return 1;
        }
    }
    return 0;
}

static int parse_int_token(const unsigned char *data, size_t size, size_t *pos, int *out) {
    while (*pos < size &&
           (data[*pos] == ' ' || data[*pos] == '\n' || data[*pos] == '\r' || data[*pos] == '\t')) {
        (*pos)++;
    }
    if (*pos >= size || data[*pos] < '0' || data[*pos] > '9') {
        return 0;
    }
    int v = 0;
    while (*pos < size && data[*pos] >= '0' && data[*pos] <= '9') {
        if (v > (INT_MAX - (data[*pos] - '0')) / 10)
            return 0;
        v = v * 10 + (data[*pos] - '0');
        (*pos)++;
    }
    *out = v;
    return 1;
}

static int extract_stream_from_object(const unsigned char *obj_data, size_t obj_len,
                                      const unsigned char **stream_data, size_t *stream_len,
                                      size_t *stream_keyword_pos) {
    size_t pos = 0;
    while (pos + 6 <= obj_len && memcmp(&obj_data[pos], "stream", 6) != 0)
        pos++;
    if (pos + 6 > obj_len)
        return 0;

    if (stream_keyword_pos) {
        *stream_keyword_pos = pos;
    }

    pos += 6;
    if (pos < obj_len && obj_data[pos] == '\r')
        pos++;
    if (pos < obj_len && obj_data[pos] == '\n')
        pos++;

    *stream_data = &obj_data[pos];

    int declared_len = 0;
    if (extract_dict_int(obj_data, 0, pos, "/Length", &declared_len) && declared_len > 0 &&
        (size_t)declared_len <= obj_len - pos) {
        *stream_len = (size_t)declared_len;
        return 1;
    }

    size_t end_pos = pos;
    while (end_pos + 9 <= obj_len && memcmp(&obj_data[end_pos], "endstream", 9) != 0)
        end_pos++;
    if (end_pos + 9 > obj_len)
        return 0;

    *stream_len = end_pos - pos;
    return 1;
}

static int extract_object_from_objstm(const unsigned char *data, size_t size, int target_obj,
                                      pdf_object_view *view) {
    for (size_t pos = 0; pos < size; pos++) {
        int obj_num = 0;
        size_t obj_start = 0;
        if (!parse_obj_header_at(data, size, pos, &obj_num, &obj_start)) {
            continue;
        }

        size_t obj_end = 0;
        if (!find_endobj(data, size, obj_start, &obj_end)) {
            continue;
        }

        if (!buffer_contains(data, obj_start, obj_end, "/Type") ||
            !buffer_contains(data, obj_start, obj_end, "/ObjStm")) {
            pos = obj_end;
            continue;
        }

        int n_objects = 0;
        int first_offset = 0;
        if (!extract_dict_int(data, obj_start, obj_end, "/N", &n_objects) ||
            !extract_dict_int(data, obj_start, obj_end, "/First", &first_offset) ||
            n_objects <= 0 || n_objects > 200000 || first_offset < 0) {
            pos = obj_end;
            continue;
        }

        const unsigned char *stream_data = NULL;
        size_t stream_len = 0;
        if (!extract_stream(data, obj_start, obj_end, &stream_data, &stream_len, NULL)) {
            pos = obj_end;
            continue;
        }

        unsigned char *decoded = NULL;
        size_t decoded_len = 0;
        if (buffer_contains(data, obj_start, obj_end, "/FlateDecode")) {
            if (!pdf_inflate_auto(stream_data, stream_len, &decoded, &decoded_len)) {
                pos = obj_end;
                continue;
            }
        } else {
            decoded = (unsigned char *)malloc(stream_len);
            if (!decoded) {
                return 0;
            }
            memcpy(decoded, stream_data, stream_len);
            decoded_len = stream_len;
        }

        if ((size_t)first_offset >= decoded_len) {
            free(decoded);
            pos = obj_end;
            continue;
        }

        int *ids = (int *)malloc((size_t)n_objects * sizeof(int));
        int *offs = (int *)malloc((size_t)n_objects * sizeof(int));
        if (!ids || !offs) {
            free(ids);
            free(offs);
            free(decoded);
            return 0;
        }

        size_t hp = 0;
        int ok_hdr = 1;
        for (int i = 0; i < n_objects; i++) {
            if (!parse_int_token(decoded, decoded_len, &hp, &ids[i]) ||
                !parse_int_token(decoded, decoded_len, &hp, &offs[i])) {
                ok_hdr = 0;
                break;
            }
        }
        if (!ok_hdr) {
            free(ids);
            free(offs);
            free(decoded);
            pos = obj_end;
            continue;
        }

        for (int i = 0; i < n_objects; i++) {
            if (ids[i] != target_obj) {
                continue;
            }
            size_t s = (size_t)first_offset + (size_t)offs[i];
            size_t e = decoded_len;
            if (i + 1 < n_objects) {
                e = (size_t)first_offset + (size_t)offs[i + 1];
            }
            if (s >= decoded_len || e > decoded_len || e <= s) {
                continue;
            }
            unsigned char *obj_copy = (unsigned char *)malloc(e - s);
            if (!obj_copy) {
                free(ids);
                free(offs);
                free(decoded);
                return 0;
            }
            memcpy(obj_copy, decoded + s, e - s);
            free(ids);
            free(offs);
            free(decoded);
            view->data = obj_copy;
            view->len = e - s;
            view->owned = obj_copy;
            return 1;
        }

        free(ids);
        free(offs);
        free(decoded);
        pos = obj_end;
    }

    return 0;
}

static int get_object_content(const unsigned char *data, size_t size, int obj_num,
                              pdf_object_view *view) {
    view->data = NULL;
    view->len = 0;
    view->owned = NULL;

    for (size_t pos = 0; pos < size; pos++) {
        int num = 0;
        size_t obj_start = 0;
        if (!parse_obj_header_at(data, size, pos, &num, &obj_start)) {
            continue;
        }
        size_t obj_end = 0;
        if (!find_endobj(data, size, obj_start, &obj_end)) {
            continue;
        }
        if (num == obj_num) {
            view->data = &data[obj_start];
            view->len = obj_end - obj_start;
            view->owned = NULL;
            return 1;
        }
        pos = obj_end;
    }

    return extract_object_from_objstm(data, size, obj_num, view);
}

static int extract_dict_ref(const unsigned char *data, size_t start, size_t end, const char *key,
                            int *obj_num) {
    size_t key_len = strlen(key);
    if (end <= start || end - start <= key_len) {
        return 0;
    }
    for (size_t pos = start; pos < end - key_len; pos++) {
        if (memcmp(&data[pos], key, key_len) == 0) {
            pos += key_len;
            while (pos < end && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' ||
                                 data[pos] == '\t'))
                pos++;
            if (pos < end && data[pos] == '[') {
                pos++;
                while (pos < end && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' ||
                                     data[pos] == '\t'))
                    pos++;
            }
            size_t number;
            if (!pdf_parse_decimal(data, end, &pos, INT_MAX, &number))
                return 0;
            *obj_num = (int)number;
            return *obj_num > 0;
        }
    }
    return 0;
}

static int extract_dict_name(const unsigned char *data, size_t start, size_t end, const char *key,
                             char *out, size_t out_cap) {
    size_t key_len = strlen(key);
    if (!out || out_cap == 0 || end <= start || end - start <= key_len) {
        return 0;
    }
    for (size_t pos = start; pos + key_len < end; pos++) {
        if (memcmp(&data[pos], key, key_len) != 0) {
            continue;
        }
        pos += key_len;
        while (pos < end &&
               (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' || data[pos] == '\t'))
            pos++;
        if (pos >= end || data[pos] != '/') {
            return 0;
        }
        pos++;
        size_t n = 0;
        while (pos < end && !isspace((unsigned char)data[pos]) && data[pos] != '/' &&
               data[pos] != '<' && data[pos] != '>' && data[pos] != '[' && data[pos] != ']' &&
               data[pos] != '(' && data[pos] != ')') {
            if (n + 1 < out_cap) {
                out[n++] = (char)data[pos];
            }
            pos++;
        }
        out[n] = '\0';
        return n > 0;
    }
    return 0;
}

static int extract_dict_refs(const unsigned char *data, size_t start, size_t end, const char *key,
                             int *objs, int max_objs, int *out_count) {
    size_t key_len = strlen(key);
    if (!objs || max_objs <= 0 || !out_count || end <= start || end - start <= key_len) {
        return 0;
    }
    *out_count = 0;

    for (size_t pos = start; pos + key_len < end; pos++) {
        if (memcmp(&data[pos], key, key_len) != 0) {
            continue;
        }
        pos += key_len;
        while (pos < end &&
               (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' || data[pos] == '\t'))
            pos++;

        int in_array = 0;
        if (pos < end && data[pos] == '[') {
            in_array = 1;
            pos++;
        }

        while (pos < end) {
            while (pos < end && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' ||
                                 data[pos] == '\t'))
                pos++;
            if (pos >= end) {
                break;
            }
            if (in_array && data[pos] == ']') {
                break;
            }
            if (data[pos] < '0' || data[pos] > '9') {
                if (!in_array) {
                    break;
                }
                pos++;
                continue;
            }

            size_t obj;
            if (!pdf_parse_decimal(data, end, &pos, INT_MAX, &obj))
                return 0;
            while (pos < end && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' ||
                                 data[pos] == '\t'))
                pos++;

            size_t gen;
            int have_gen = pdf_parse_decimal(data, end, &pos, 65535, &gen);
            if (!have_gen)
                return 0;
            while (pos < end && (data[pos] == ' ' || data[pos] == '\n' || data[pos] == '\r' ||
                                 data[pos] == '\t'))
                pos++;
            if (have_gen && pos < end && data[pos] == 'R') {
                pos++;
            }

            if (obj > 0 && *out_count < max_objs) {
                objs[*out_count] = (int)obj;
                (*out_count)++;
            }

            if (!in_array) {
                break;
            }
        }

        return *out_count > 0;
    }

    return 0;
}

static int get_pages_obj(const unsigned char *data, size_t size, pdf_xref *xref, int *pages_obj,
                         char *err, size_t errcap) {
    (void)xref;
    pdf_object_view catalog = {0};
    if (!get_object_content(data, size, xref->root_obj, &catalog)) {
        set_err(err, errcap, "pdf: catalog object not found");
        return 0;
    }

    if (!extract_dict_ref(catalog.data, 0, catalog.len, "/Pages", pages_obj)) {
        pdf_object_view_release(&catalog);
        set_err(err, errcap, "pdf: /Pages not found in catalog");
        return 0;
    }

    pdf_object_view_release(&catalog);
    return 1;
}

static int get_pages_count_from_obj(const unsigned char *data, size_t size, int pages_obj,
                                    int *count, char *err, size_t errcap) {
    if (!count) {
        set_err(err, errcap, "pdf: invalid pages count output");
        return 0;
    }
    *count = 0;

    pdf_object_view pages = {0};
    if (!get_object_content(data, size, pages_obj, &pages)) {
        set_err(err, errcap, "pdf: pages object not found");
        return 0;
    }

    int c = 0;
    if (extract_dict_int(pages.data, 0, pages.len, "/Count", &c) && c > 0) {
        *count = c;
        pdf_object_view_release(&pages);
        return 1;
    }

    pdf_object_view_release(&pages);
    set_err(err, errcap, "pdf: /Count not found in pages");
    return 0;
}

static int find_page_in_tree(const unsigned char *data, size_t size, int obj_num, int target_index,
                             int *scan_index, int *page_obj, int depth, char *err, size_t errcap) {
    if (depth > 64) {
        set_err(err, errcap, "pdf: pages tree too deep");
        return 0;
    }

    pdf_object_view node = {0};
    if (!get_object_content(data, size, obj_num, &node)) {
        set_err(err, errcap, "pdf: pages tree object not found");
        return 0;
    }

    char type_name[32];
    type_name[0] = '\0';
    (void)extract_dict_name(node.data, 0, node.len, "/Type", type_name, sizeof(type_name));

    if (strcmp(type_name, "Page") == 0) {
        if (*scan_index == target_index) {
            *page_obj = obj_num;
            pdf_object_view_release(&node);
            return 1;
        }
        (*scan_index)++;
        pdf_object_view_release(&node);
        return 1;
    }

    int kids[256];
    int kid_count = 0;
    if (!extract_dict_refs(node.data, 0, node.len, "/Kids", kids,
                           (int)(sizeof(kids) / sizeof(kids[0])), &kid_count) ||
        kid_count <= 0) {
        pdf_object_view_release(&node);
        set_err(err, errcap, "pdf: /Kids not found");
        return 0;
    }

    for (int i = 0; i < kid_count; i++) {
        if (!find_page_in_tree(data, size, kids[i], target_index, scan_index, page_obj, depth + 1,
                               err, errcap)) {
            pdf_object_view_release(&node);
            return 0;
        }
        if (*page_obj > 0) {
            pdf_object_view_release(&node);
            return 1;
        }
    }

    pdf_object_view_release(&node);
    return 1;
}

static int get_page_obj_by_index(const unsigned char *data, size_t size, pdf_xref *xref,
                                 int pages_obj, int page_index, int *page_obj, char *err,
                                 size_t errcap) {
    (void)xref;
    if (page_index < 0) {
        set_err(err, errcap, "pdf: invalid page index");
        return 0;
    }

    int page_count = 0;
    if (!get_pages_count_from_obj(data, size, pages_obj, &page_count, err, errcap)) {
        return 0;
    }
    if (page_index >= page_count) {
        set_err(err, errcap, "pdf: page index out of range");
        return 0;
    }

    int scan_index = 0;
    *page_obj = 0;
    if (!find_page_in_tree(data, size, pages_obj, page_index, &scan_index, page_obj, 0, err,
                           errcap)) {
        return 0;
    }

    if (*page_obj <= 0) {
        set_err(err, errcap, "pdf: page not found");
        return 0;
    }

    return 1;
}

static int parse_pdf_number(const unsigned char *data, size_t end, size_t *pos, double *out) {
    size_t p = *pos;
    int saw_digit = 0;
    int saw_dot = 0;

    if (p < end && (data[p] == '+' || data[p] == '-')) {
        p++;
    }
    while (p < end) {
        if (data[p] >= '0' && data[p] <= '9') {
            saw_digit = 1;
            p++;
            continue;
        }
        if (data[p] == '.' && !saw_dot) {
            saw_dot = 1;
            p++;
            continue;
        }
        break;
    }
    if (!saw_digit) {
        return 0;
    }

    char tmp[64];
    size_t n = p - *pos;
    if (n >= sizeof(tmp))
        return 0;
    memcpy(tmp, &data[*pos], n);
    tmp[n] = '\0';
    *out = strtod(tmp, NULL);
    *pos = p;
    return isfinite(*out);
}

/* Only inspect entries in the outer dictionary, not names in nested values. */
static int pdf_page_value(const pdf_object_view *view, const char *key, size_t *value_pos) {
    size_t pos = 0;
    int depth = 0, arrays = 0, expect_key = 1;
    while (pos < view->len) {
        pdf_skip_ws_and_comments(view->data, view->len, &pos);
        if (pos >= view->len)
            break;
        unsigned char c = view->data[pos++];
        if (c == '(') {
            int strings = 1;
            while (pos < view->len && strings) {
                c = view->data[pos++];
                if (c == '\\' && pos < view->len)
                    pos++;
                else if (c == '(')
                    strings++;
                else if (c == ')')
                    strings--;
            }
            if (depth == 1 && arrays == 0)
                expect_key = 1;
        } else if (c == '<') {
            if (pos < view->len && view->data[pos] == '<') {
                pos++;
                depth++;
            } else {
                while (pos < view->len && view->data[pos++] != '>') {
                }
                if (depth == 1 && arrays == 0)
                    expect_key = 1;
            }
        } else if (c == '>' && pos < view->len && view->data[pos] == '>') {
            pos++;
            if (--depth <= 0)
                break;
            if (depth == 1 && arrays == 0)
                expect_key = 1;
        } else if (c == '[')
            arrays++;
        else if (c == ']') {
            arrays--;
            if (depth == 1 && arrays == 0)
                expect_key = 1;
        } else if (c == '/') {
            size_t start = pos;
            while (pos < view->len && !pdf_is_delim(view->data[pos]) &&
                   !isspace((unsigned char)view->data[pos]))
                pos++;
            size_t len = pos - start;
            if (depth == 1 && arrays == 0) {
                if (expect_key && len == strlen(key) && memcmp(view->data + start, key, len) == 0) {
                    pdf_skip_ws_and_comments(view->data, view->len, &pos);
                    *value_pos = pos;
                    return 1;
                }
                expect_key = !expect_key;
            }
        } else if (depth == 1 && arrays == 0 && !expect_key) {
            while (pos < view->len && !pdf_is_delim(view->data[pos]) &&
                   !isspace((unsigned char)view->data[pos]))
                pos++;
            expect_key = 1;
        }
    }
    return 0;
}

static int pdf_page_reference(const pdf_object_view *view, size_t pos, int *object) {
    double number, generation;
    pdf_skip_ws_and_comments(view->data, view->len, &pos);
    if (!parse_pdf_number(view->data, view->len, &pos, &number) || number < 1 || number > INT_MAX ||
        floor(number) != number)
        return 0;
    pdf_skip_ws_and_comments(view->data, view->len, &pos);
    if (!parse_pdf_number(view->data, view->len, &pos, &generation) || generation < 0 ||
        generation > 65535 || floor(generation) != generation)
        return 0;
    pdf_skip_ws_and_comments(view->data, view->len, &pos);
    if (pos >= view->len || view->data[pos] != 'R')
        return 0;
    *object = (int)number;
    return 1;
}

static int pdf_page_rectangle(const unsigned char *data, size_t size, const pdf_object_view *view,
                              size_t pos, double box[4]) {
    pdf_object_view indirect = {0};
    int object = 0;
    if (pos < view->len && view->data[pos] != '[') {
        if (!pdf_page_reference(view, pos, &object) ||
            !get_object_content(data, size, object, &indirect))
            return 0;
        view = &indirect;
        pos = 0;
        pdf_skip_ws_and_comments(view->data, view->len, &pos);
    }
    int ok = pos < view->len && view->data[pos++] == '[';
    for (int i = 0; ok && i < 4; i++) {
        pdf_skip_ws_and_comments(view->data, view->len, &pos);
        ok = parse_pdf_number(view->data, view->len, &pos, &box[i]);
    }
    pdf_skip_ws_and_comments(view->data, view->len, &pos);
    ok = ok && pos < view->len && view->data[pos] == ']';
    pdf_object_view_release(&indirect);
    if (!ok)
        return 0;
    double x0 = fmin(box[0], box[2]), y0 = fmin(box[1], box[3]);
    box[2] = fmax(box[0], box[2]);
    box[3] = fmax(box[1], box[3]);
    box[0] = x0;
    box[1] = y0;
    return box[2] > box[0] && box[3] > box[1];
}

static int get_page_geometry(const unsigned char *data, size_t size, int page_obj,
                             pdf_page_geometry *geometry, char *err, size_t errcap) {
    double media[4] = {0, 0, 612, 792}, crop[4] = {0};
    int have_media = 0, have_crop = 0, have_rotation = 0;
    int visited[64], depth = 0;
    geometry->rotation = 0;
    while (page_obj > 0) {
        if (depth == 64)
            goto invalid;
        for (int i = 0; i < depth; i++)
            if (visited[i] == page_obj)
                goto invalid;
        visited[depth++] = page_obj;
        pdf_object_view node = {0};
        if (!get_object_content(data, size, page_obj, &node))
            goto invalid;
        size_t pos = 0;
        int ok = 1;
        if (!have_media && pdf_page_value(&node, "MediaBox", &pos)) {
            ok = pdf_page_rectangle(data, size, &node, pos, media);
            have_media = 1;
        }
        if (ok && !have_crop && pdf_page_value(&node, "CropBox", &pos)) {
            ok = pdf_page_rectangle(data, size, &node, pos, crop);
            have_crop = 1;
        }
        if (ok && !have_rotation && pdf_page_value(&node, "Rotate", &pos)) {
            pdf_object_view indirect = {0};
            const pdf_object_view *value = &node;
            int object = 0;
            if (pdf_page_reference(&node, pos, &object)) {
                ok = get_object_content(data, size, object, &indirect);
                value = &indirect;
                pos = 0;
            }
            double angle = 0;
            pdf_skip_ws_and_comments(value->data, value->len, &pos);
            ok = ok && parse_pdf_number(value->data, value->len, &pos, &angle) &&
                 angle >= INT_MIN && angle <= INT_MAX && fmod(angle, 90.0) == 0;
            if (ok)
                geometry->rotation = (((int)angle % 360) + 360) % 360;
            pdf_object_view_release(&indirect);
            have_rotation = 1;
        }
        int parent = 0;
        if (ok && !(have_media && have_crop && have_rotation) &&
            pdf_page_value(&node, "Parent", &pos)) {
            ok = pdf_page_reference(&node, pos, &parent);
        }
        pdf_object_view_release(&node);
        if (!ok)
            goto invalid;
        page_obj = parent;
    }
    if (!have_crop)
        memcpy(crop, media, sizeof(crop));
    geometry->x0 = fmax(media[0], crop[0]);
    geometry->y0 = fmax(media[1], crop[1]);
    geometry->x1 = fmin(media[2], crop[2]);
    geometry->y1 = fmin(media[3], crop[3]);
    double width = floor((geometry->x1 - geometry->x0) * (96.0 / 72.0));
    double height = floor((geometry->y1 - geometry->y0) * (96.0 / 72.0));
    /* Bound preview memory and validate before converting to integer dimensions. */
    if (!isfinite(width) || !isfinite(height) || width < 1 || height < 1 || width > INT_MAX ||
        height > INT_MAX || width * height > 64.0 * 1024 * 1024)
        goto invalid;
    geometry->width = (int)width;
    geometry->height = (int)height;
    return 1;
invalid:
    set_err(err, errcap, "pdf: invalid page geometry or parent tree");
    return 0;
}

static int extract_stream(const unsigned char *data, size_t obj_start, size_t obj_end,
                          const unsigned char **stream_data, size_t *stream_len,
                          size_t *stream_keyword_pos) {
    size_t pos = obj_start;
    while (pos + 6 <= obj_end && memcmp(&data[pos], "stream", 6) != 0)
        pos++;
    if (pos + 6 > obj_end)
        return 0;

    if (stream_keyword_pos) {
        *stream_keyword_pos = pos;
    }

    pos += 6;
    if (pos < obj_end && data[pos] == '\r')
        pos++;
    if (pos < obj_end && data[pos] == '\n')
        pos++;

    *stream_data = &data[pos];

    int declared_len = 0;
    if (extract_dict_int(data, obj_start, pos, "/Length", &declared_len) && declared_len > 0 &&
        (size_t)declared_len <= obj_end - pos) {
        *stream_len = (size_t)declared_len;
        return 1;
    }

    size_t end_pos = pos;
    while (end_pos + 9 <= obj_end && memcmp(&data[end_pos], "endstream", 9) != 0)
        end_pos++;
    if (end_pos + 9 > obj_end)
        return 0;

    *stream_len = end_pos - pos;
    return 1;
}

static int buffer_contains(const unsigned char *data, size_t start, size_t end,
                           const char *needle) {
    size_t needle_len = strlen(needle);
    if (end <= start || end - start < needle_len) {
        return 0;
    }
    for (size_t i = start; i + needle_len <= end; i++) {
        if (memcmp(&data[i], needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int get_page_content_stream(const unsigned char *data, size_t size, pdf_xref *xref,
                                   int page_obj, unsigned char **stream_data, size_t *stream_len,
                                   int *is_flate, char *err, size_t errcap) {
    (void)xref;
    pdf_object_view page = {0};
    if (!get_object_content(data, size, page_obj, &page)) {
        set_err(err, errcap, "pdf: page object not found");
        return 0;
    }

    int content_objs[128];
    int content_count = 0;
    if (!extract_dict_refs(page.data, 0, page.len, "/Contents", content_objs,
                           (int)(sizeof(content_objs) / sizeof(content_objs[0])), &content_count)) {
        size_t pos;
        if (pdf_page_value(&page, "Contents", &pos)) {
            int empty = page.len - pos >= 4 && memcmp(page.data + pos, "null", 4) == 0;
            if (pos < page.len && page.data[pos] == '[') {
                pos++;
                pdf_skip_ws_and_comments(page.data, page.len, &pos);
                empty = pos < page.len && page.data[pos] == ']';
            }
            if (!empty) {
                pdf_object_view_release(&page);
                set_err(err, errcap, "pdf: invalid contents reference");
                return 0;
            }
        }
        pdf_object_view_release(&page);
        *stream_data = NULL;
        *stream_len = 0;
        *is_flate = 0;
        return 1;
    }
    pdf_object_view_release(&page);

    unsigned char *combined = NULL;
    size_t combined_len = 0;

    for (int i = 0; i < content_count; i++) {
        pdf_object_view contents = {0};
        if (!get_object_content(data, size, content_objs[i], &contents)) {
            free(combined);
            set_err(err, errcap, "pdf: contents object not found");
            return 0;
        }

        size_t stream_kw_pos = 0;
        const unsigned char *stream_ptr = NULL;
        size_t raw_len = 0;
        if (!extract_stream_from_object(contents.data, contents.len, &stream_ptr, &raw_len,
                                        &stream_kw_pos)) {
            pdf_object_view_release(&contents);
            continue;
        }

        int stream_is_flate = 0;
        if (buffer_contains(contents.data, 0, stream_kw_pos, "/FlateDecode") ||
            buffer_contains(contents.data, 0, stream_kw_pos, "/Fl")) {
            stream_is_flate = 1;
        }

        const unsigned char *decoded_ptr = stream_ptr;
        size_t decoded_len = raw_len;
        unsigned char *decoded_owned = NULL;
        if (stream_is_flate) {
            if (!pdf_inflate_auto(stream_ptr, raw_len, &decoded_owned, &decoded_len)) {
                pdf_object_view_release(&contents);
                free(combined);
                set_err(err, errcap, "pdf: failed to decode Flate stream");
                return 0;
            }
            decoded_ptr = decoded_owned;
        }

        if (decoded_len > 0) {
            size_t extra = decoded_len + ((combined_len > 0) ? 1u : 0u);
            unsigned char *tmp = (unsigned char *)realloc(combined, combined_len + extra);
            if (!tmp) {
                free(decoded_owned);
                pdf_object_view_release(&contents);
                free(combined);
                set_err(err, errcap, "pdf: out of memory");
                return 0;
            }
            combined = tmp;
            if (combined_len > 0) {
                combined[combined_len++] = '\n';
            }
            memcpy(combined + combined_len, decoded_ptr, decoded_len);
            combined_len += decoded_len;
        }

        free(decoded_owned);
        pdf_object_view_release(&contents);
    }

    *stream_data = combined;
    *stream_len = combined_len;
    *is_flate = 0;

    return 1;
}

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t bitbuf;
    int bitcount;
} pdf_bitstream;

static int pdf_bs_fill(pdf_bitstream *bs, int need) {
    while (bs->bitcount < need && bs->pos < bs->size) {
        bs->bitbuf |= (uint32_t)bs->data[bs->pos++] << bs->bitcount;
        bs->bitcount += 8;
    }
    return bs->bitcount >= need;
}

static uint32_t pdf_bs_read(pdf_bitstream *bs, int bits) {
    if (!pdf_bs_fill(bs, bits)) {
        return 0;
    }
    uint32_t val = bs->bitbuf & ((1u << bits) - 1u);
    bs->bitbuf >>= bits;
    bs->bitcount -= bits;
    return val;
}

static void pdf_bs_align(pdf_bitstream *bs) {
    /* Stored blocks read bytes directly, including complete lookahead bytes. */
    bs->pos -= (size_t)(bs->bitcount / 8);
    bs->bitbuf = 0;
    bs->bitcount = 0;
}

static unsigned pdf_reverse_bits(unsigned v, int bits) {
    unsigned r = 0;
    for (int i = 0; i < bits; i++) {
        r = (r << 1) | (v & 1u);
        v >>= 1;
    }
    return r;
}

typedef struct {
    int maxbits;
    uint16_t *table;
} pdf_huff_table;

static void pdf_huff_free(pdf_huff_table *ht) {
    free(ht->table);
    ht->table = NULL;
    ht->maxbits = 0;
}

static int pdf_huff_build(pdf_huff_table *ht, const uint8_t *lengths, int num, int maxbits) {
    int count[16] = {0};
    int maxlen = 0;
    for (int i = 0; i < num; i++) {
        if (lengths[i] > maxbits) {
            return 0;
        }
        if (lengths[i]) {
            count[lengths[i]]++;
            if (lengths[i] > maxlen) {
                maxlen = lengths[i];
            }
        }
    }
    if (maxlen == 0) {
        return 0;
    }

    int next_code[16];
    int code = 0;
    count[0] = 0;
    for (int bits = 1; bits <= maxbits; bits++) {
        code = (code + count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    ht->maxbits = maxlen;
    ht->table = (uint16_t *)malloc(((size_t)1u << maxlen) * sizeof(uint16_t));
    if (!ht->table) {
        return 0;
    }
    size_t table_size = (size_t)1u << maxlen;
    for (size_t i = 0; i < table_size; i++) {
        ht->table[i] = 0xFFFFu;
    }

    for (int sym = 0; sym < num; sym++) {
        int len = lengths[sym];
        if (!len) {
            continue;
        }
        int sym_code = next_code[len]++;
        unsigned rev = pdf_reverse_bits((unsigned)sym_code, len);
        unsigned fill = 1u << (maxlen - len);
        for (unsigned i = 0; i < fill; i++) {
            unsigned idx = (i << len) | rev;
            ht->table[idx] = (uint16_t)((len << 9) | sym);
        }
    }

    return 1;
}

static int pdf_huff_decode(pdf_bitstream *bs, const pdf_huff_table *ht, int *sym) {
    if (!pdf_bs_fill(bs, ht->maxbits)) {
        return 0;
    }
    uint32_t idx = bs->bitbuf & ((1u << ht->maxbits) - 1u);
    uint16_t val = ht->table[idx];
    if (val == 0xFFFFu) {
        return 0;
    }
    int len = val >> 9;
    *sym = val & 0x1FF;
    bs->bitbuf >>= len;
    bs->bitcount -= len;
    return 1;
}

static int pdf_build_fixed_tables(pdf_huff_table *litlen, pdf_huff_table *dist) {
    uint8_t litlen_lengths[288];
    uint8_t dist_lengths[32];

    for (int i = 0; i <= 143; i++)
        litlen_lengths[i] = 8;
    for (int i = 144; i <= 255; i++)
        litlen_lengths[i] = 9;
    for (int i = 256; i <= 279; i++)
        litlen_lengths[i] = 7;
    for (int i = 280; i <= 287; i++)
        litlen_lengths[i] = 8;
    for (int i = 0; i < 32; i++)
        dist_lengths[i] = 5;

    if (!pdf_huff_build(litlen, litlen_lengths, 288, 15)) {
        return 0;
    }
    if (!pdf_huff_build(dist, dist_lengths, 32, 15)) {
        pdf_huff_free(litlen);
        return 0;
    }
    return 1;
}

static int pdf_build_dynamic_tables(pdf_bitstream *bs, pdf_huff_table *litlen,
                                    pdf_huff_table *dist) {
    int hlit = (int)pdf_bs_read(bs, 5) + 257;
    int hdist = (int)pdf_bs_read(bs, 5) + 1;
    int hclen = (int)pdf_bs_read(bs, 4) + 4;
    if (hlit > 286 || hdist > 30) {
        return 0;
    }

    static const int order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    uint8_t clen_lengths[19];
    for (int i = 0; i < 19; i++) {
        clen_lengths[i] = 0;
    }
    for (int i = 0; i < hclen; i++) {
        clen_lengths[order[i]] = (uint8_t)pdf_bs_read(bs, 3);
    }

    pdf_huff_table clen_table = {0, NULL};
    if (!pdf_huff_build(&clen_table, clen_lengths, 19, 7)) {
        return 0;
    }

    int total = hlit + hdist;
    uint8_t lengths[316];
    int idx = 0;
    while (idx < total) {
        int sym = 0;
        if (!pdf_huff_decode(bs, &clen_table, &sym)) {
            pdf_huff_free(&clen_table);
            return 0;
        }
        if (sym <= 15) {
            lengths[idx++] = (uint8_t)sym;
        } else if (sym == 16) {
            if (idx == 0) {
                pdf_huff_free(&clen_table);
                return 0;
            }
            int repeat = (int)pdf_bs_read(bs, 2) + 3;
            uint8_t prev = lengths[idx - 1];
            for (int i = 0; i < repeat && idx < total; i++) {
                lengths[idx++] = prev;
            }
        } else if (sym == 17) {
            int repeat = (int)pdf_bs_read(bs, 3) + 3;
            for (int i = 0; i < repeat && idx < total; i++) {
                lengths[idx++] = 0;
            }
        } else if (sym == 18) {
            int repeat = (int)pdf_bs_read(bs, 7) + 11;
            for (int i = 0; i < repeat && idx < total; i++) {
                lengths[idx++] = 0;
            }
        } else {
            pdf_huff_free(&clen_table);
            return 0;
        }
    }

    pdf_huff_free(&clen_table);

    if (!pdf_huff_build(litlen, lengths, hlit, 15)) {
        return 0;
    }
    if (!pdf_huff_build(dist, lengths + hlit, hdist, 15)) {
        pdf_huff_free(litlen);
        return 0;
    }
    return 1;
}

static int pdf_deflate_decode(pdf_bitstream *bs, uint8_t *out, size_t outcap, size_t *outlen) {
    static const int length_base[29] = {3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                        15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                        67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const int length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                         2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const int dist_base[30] = {
        1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const int dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                       6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

    size_t outpos = 0;
    int final_block = 0;

    while (!final_block) {
        if (!pdf_bs_fill(bs, 3)) {
            return 0;
        }
        final_block = (int)pdf_bs_read(bs, 1);
        int btype = (int)pdf_bs_read(bs, 2);

        if (btype == 0) {
            pdf_bs_align(bs);
            if (bs->pos + 4 > bs->size) {
                return 0;
            }
            uint16_t len = (uint16_t)(bs->data[bs->pos] | (bs->data[bs->pos + 1] << 8));
            uint16_t nlen = (uint16_t)(bs->data[bs->pos + 2] | (bs->data[bs->pos + 3] << 8));
            bs->pos += 4;
            if ((uint16_t)~len != nlen) {
                return 0;
            }
            if (bs->pos + len > bs->size) {
                return 0;
            }
            if (outpos + len > outcap) {
                return 0;
            }
            memcpy(out + outpos, bs->data + bs->pos, len);
            bs->pos += len;
            outpos += len;
        } else if (btype == 1 || btype == 2) {
            pdf_huff_table litlen = {0, NULL};
            pdf_huff_table dist = {0, NULL};
            int ok = 0;
            if (btype == 1) {
                ok = pdf_build_fixed_tables(&litlen, &dist);
            } else {
                ok = pdf_build_dynamic_tables(bs, &litlen, &dist);
            }
            if (!ok) {
                pdf_huff_free(&litlen);
                pdf_huff_free(&dist);
                return 0;
            }

            while (1) {
                int sym = 0;
                if (!pdf_huff_decode(bs, &litlen, &sym)) {
                    pdf_huff_free(&litlen);
                    pdf_huff_free(&dist);
                    return 0;
                }
                if (sym < 256) {
                    if (outpos >= outcap) {
                        pdf_huff_free(&litlen);
                        pdf_huff_free(&dist);
                        return 0;
                    }
                    out[outpos++] = (uint8_t)sym;
                } else if (sym == 256) {
                    break;
                } else if (sym <= 285) {
                    int len_idx = sym - 257;
                    int length = length_base[len_idx];
                    int extra = length_extra[len_idx];
                    if (extra) {
                        length += (int)pdf_bs_read(bs, extra);
                    }

                    int dist_sym = 0;
                    if (!pdf_huff_decode(bs, &dist, &dist_sym)) {
                        pdf_huff_free(&litlen);
                        pdf_huff_free(&dist);
                        return 0;
                    }
                    if (dist_sym > 29) {
                        pdf_huff_free(&litlen);
                        pdf_huff_free(&dist);
                        return 0;
                    }
                    int distance = dist_base[dist_sym];
                    int dist_ext = dist_extra[dist_sym];
                    if (dist_ext) {
                        distance += (int)pdf_bs_read(bs, dist_ext);
                    }
                    if (distance <= 0 || (size_t)distance > outpos) {
                        pdf_huff_free(&litlen);
                        pdf_huff_free(&dist);
                        return 0;
                    }
                    if (outpos + (size_t)length > outcap) {
                        pdf_huff_free(&litlen);
                        pdf_huff_free(&dist);
                        return 0;
                    }
                    for (int i = 0; i < length; i++) {
                        out[outpos] = out[outpos - (size_t)distance];
                        outpos++;
                    }
                } else {
                    pdf_huff_free(&litlen);
                    pdf_huff_free(&dist);
                    return 0;
                }
            }

            pdf_huff_free(&litlen);
            pdf_huff_free(&dist);
        } else {
            return 0;
        }
    }

    *outlen = outpos;
    return 1;
}

static int pdf_zlib_decompress(const uint8_t *data, size_t size, uint8_t *out, size_t outcap,
                               size_t *outlen) {
    if (size < 2) {
        return 0;
    }
    uint8_t cmf = data[0];
    uint8_t flg = data[1];
    if ((cmf & 0x0F) != 8) {
        return 0;
    }
    if (((cmf << 8) + flg) % 31 != 0) {
        return 0;
    }
    if (flg & 0x20) {
        return 0;
    }

    pdf_bitstream bs;
    bs.data = data + 2;
    bs.size = size - 2;
    bs.pos = 0;
    bs.bitbuf = 0;
    bs.bitcount = 0;

    return pdf_deflate_decode(&bs, out, outcap, outlen);
}

static int pdf_inflate(const uint8_t *data, size_t size, uint8_t *out, size_t outcap,
                       size_t *outlen) {
    if (pdf_zlib_decompress(data, size, out, outcap, outlen)) {
        return 1;
    }

    pdf_bitstream bs;
    bs.data = data;
    bs.size = size;
    bs.pos = 0;
    bs.bitbuf = 0;
    bs.bitcount = 0;
    return pdf_deflate_decode(&bs, out, outcap, outlen);
}

static int pdf_inflate_auto(const unsigned char *data, size_t size, unsigned char **out_data,
                            size_t *out_len) {
    const size_t max_cap = 64u * 1024u * 1024u;
    size_t cap = size * 4u + 1024u;
    if (cap < 4096u) {
        cap = 4096u;
    }

    while (cap <= max_cap) {
        unsigned char *buf = (unsigned char *)malloc(cap);
        if (!buf) {
            return 0;
        }

        size_t len = 0;
        if (pdf_inflate(data, size, buf, cap, &len)) {
            *out_data = buf;
            *out_len = len;
            return 1;
        }

        free(buf);
        if (cap > max_cap / 2u) {
            break;
        }
        cap *= 2u;
    }

    return 0;
}

static void pdf_font_table_init(pdf_font_table *table) {
    if (!table)
        return;
    table->fonts = NULL;
    table->count = 0;
    table->cap = 0;
}

static void pdf_font_table_free(pdf_font_table *table) {
    if (!table)
        return;
    for (int i = 0; i < table->count; i++) {
        free(table->fonts[i].entries);
        table->fonts[i].entries = NULL;
        table->fonts[i].count = 0;
        table->fonts[i].cap = 0;
        table->fonts[i].max_src_len = 1;
    }
    free(table->fonts);
    table->fonts = NULL;
    table->count = 0;
    table->cap = 0;
}

static pdf_font_map *pdf_font_table_add(pdf_font_table *table, const char *name) {
    if (!table || !name || !*name) {
        return NULL;
    }
    for (int i = 0; i < table->count; i++) {
        if (strcmp(table->fonts[i].name, name) == 0) {
            return &table->fonts[i];
        }
    }

    if (table->count >= table->cap) {
        int new_cap = (table->cap > 0) ? table->cap * 2 : 8;
        pdf_font_map *new_fonts =
            (pdf_font_map *)realloc(table->fonts, (size_t)new_cap * sizeof(pdf_font_map));
        if (!new_fonts) {
            return NULL;
        }
        table->fonts = new_fonts;
        table->cap = new_cap;
    }

    pdf_font_map *font = &table->fonts[table->count++];
    memset(font, 0, sizeof(*font));
    snprintf(font->name, sizeof(font->name), "%s", name);
    font->max_src_len = 1;
    return font;
}

static const pdf_font_map *pdf_font_table_find(const pdf_font_table *table, const char *name) {
    if (!table || !name || !*name) {
        return NULL;
    }
    for (int i = 0; i < table->count; i++) {
        if (strcmp(table->fonts[i].name, name) == 0) {
            return &table->fonts[i];
        }
    }
    return NULL;
}

static int pdf_font_add_tounicode(pdf_font_map *font, uint32_t src_code, uint8_t src_len,
                                  uint32_t dst_codepoint) {
    if (!font || src_len == 0) {
        return 0;
    }
    for (int i = 0; i < font->count; i++) {
        if (font->entries[i].src_code == src_code && font->entries[i].src_len == src_len) {
            font->entries[i].dst_codepoint = dst_codepoint;
            return 1;
        }
    }
    if (font->count >= font->cap) {
        int new_cap = (font->cap > 0) ? font->cap * 2 : 256;
        pdf_tounicode_entry *new_entries = (pdf_tounicode_entry *)realloc(
            font->entries, (size_t)new_cap * sizeof(pdf_tounicode_entry));
        if (!new_entries) {
            return 0;
        }
        font->entries = new_entries;
        font->cap = new_cap;
    }
    font->entries[font->count].src_code = src_code;
    font->entries[font->count].src_len = src_len;
    font->entries[font->count].dst_codepoint = dst_codepoint;
    font->count++;
    if ((int)src_len > font->max_src_len) {
        font->max_src_len = (int)src_len;
    }
    return 1;
}

typedef enum {
    PDF_CMAP_TOK_EOF = 0,
    PDF_CMAP_TOK_WORD = 1,
    PDF_CMAP_TOK_HEX = 2,
    PDF_CMAP_TOK_LBRACK = 3,
    PDF_CMAP_TOK_RBRACK = 4
} pdf_cmap_tok_type;

static pdf_cmap_tok_type pdf_cmap_next_token(const unsigned char *data, size_t len, size_t *pos,
                                             char *tok, size_t tok_cap) {
    if (!data || !pos || !tok || tok_cap == 0) {
        return PDF_CMAP_TOK_EOF;
    }
    while (*pos < len) {
        unsigned char c = data[*pos];
        if (c == '%') {
            while (*pos < len && data[*pos] != '\n' && data[*pos] != '\r') {
                (*pos)++;
            }
            continue;
        }
        if (isspace(c)) {
            (*pos)++;
            continue;
        }
        break;
    }
    if (*pos >= len) {
        return PDF_CMAP_TOK_EOF;
    }

    unsigned char c = data[*pos];
    if (c == '[') {
        (*pos)++;
        tok[0] = '[';
        tok[1] = '\0';
        return PDF_CMAP_TOK_LBRACK;
    }
    if (c == ']') {
        (*pos)++;
        tok[0] = ']';
        tok[1] = '\0';
        return PDF_CMAP_TOK_RBRACK;
    }
    if (c == '<') {
        (*pos)++;
        size_t n = 0;
        while (*pos < len && data[*pos] != '>') {
            if (n + 1 < tok_cap) {
                tok[n++] = (char)data[*pos];
            }
            (*pos)++;
        }
        if (*pos < len && data[*pos] == '>') {
            (*pos)++;
        }
        tok[n] = '\0';
        return PDF_CMAP_TOK_HEX;
    }

    size_t n = 0;
    while (*pos < len) {
        c = data[*pos];
        if (isspace(c) || c == '[' || c == ']' || c == '<' || c == '>' || c == '%') {
            break;
        }
        if (n + 1 < tok_cap) {
            tok[n++] = (char)c;
        }
        (*pos)++;
    }
    tok[n] = '\0';
    return (n > 0) ? PDF_CMAP_TOK_WORD : PDF_CMAP_TOK_EOF;
}

static int pdf_hex_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int pdf_cmap_hex_to_code(const char *hex, uint32_t *code, uint8_t *code_len,
                                uint32_t *first_u16) {
    if (!hex || !*hex || !code || !code_len || !first_u16) {
        return 0;
    }
    size_t nhex = strlen(hex);
    if ((nhex & 1u) != 0u) {
        return 0;
    }
    size_t nbytes = nhex / 2u;
    if (nbytes == 0 || nbytes > 4u) {
        return 0;
    }

    uint8_t bytes[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < nbytes; i++) {
        int hi = pdf_hex_value(hex[i * 2u]);
        int lo = pdf_hex_value(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            return 0;
        }
        bytes[i] = (uint8_t)((hi << 4) | lo);
    }

    uint32_t v = 0;
    for (size_t i = 0; i < nbytes; i++) {
        v = (v << 8u) | bytes[i];
    }
    *code = v;
    *code_len = (uint8_t)nbytes;

    uint32_t u = 0;
    if (nbytes >= 2u) {
        u = ((uint32_t)bytes[0] << 8u) | bytes[1];
    } else {
        u = (uint32_t)bytes[0];
    }
    *first_u16 = u;
    return 1;
}

static int pdf_parse_tounicode_stream(const unsigned char *stream, size_t stream_len,
                                      pdf_font_map *font) {
    if (!stream || !font) {
        return 0;
    }

    size_t pos = 0;
    char tok[256];
    while (1) {
        pdf_cmap_tok_type t = pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
        if (t == PDF_CMAP_TOK_EOF) {
            break;
        }

        if (t == PDF_CMAP_TOK_WORD && strcmp(tok, "beginbfchar") == 0) {
            while (1) {
                pdf_cmap_tok_type a =
                    pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                if (a == PDF_CMAP_TOK_EOF) {
                    return 1;
                }
                if (a == PDF_CMAP_TOK_WORD && strcmp(tok, "endbfchar") == 0) {
                    break;
                }
                if (a != PDF_CMAP_TOK_HEX) {
                    continue;
                }
                char src_hex[256];
                snprintf(src_hex, sizeof(src_hex), "%s", tok);

                pdf_cmap_tok_type b =
                    pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                if (b != PDF_CMAP_TOK_HEX) {
                    continue;
                }

                uint32_t src_code = 0;
                uint8_t src_len = 0;
                uint8_t dst_len = 0;
                uint32_t dst = 0;
                uint32_t dummy = 0;
                if (pdf_cmap_hex_to_code(src_hex, &src_code, &src_len, &dummy) &&
                    pdf_cmap_hex_to_code(tok, &dummy, &dst_len, &dst)) {
                    (void)pdf_font_add_tounicode(font, src_code, src_len, dst);
                }
            }
            continue;
        }

        if (t == PDF_CMAP_TOK_WORD && strcmp(tok, "beginbfrange") == 0) {
            while (1) {
                pdf_cmap_tok_type a =
                    pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                if (a == PDF_CMAP_TOK_EOF) {
                    return 1;
                }
                if (a == PDF_CMAP_TOK_WORD && strcmp(tok, "endbfrange") == 0) {
                    break;
                }
                if (a != PDF_CMAP_TOK_HEX) {
                    continue;
                }
                char start_hex[256];
                snprintf(start_hex, sizeof(start_hex), "%s", tok);

                pdf_cmap_tok_type b =
                    pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                if (b != PDF_CMAP_TOK_HEX) {
                    continue;
                }
                char end_hex[256];
                snprintf(end_hex, sizeof(end_hex), "%s", tok);

                uint32_t start_code = 0, end_code = 0, dst_first = 0, dummy = 0;
                uint8_t start_len = 0, end_len = 0, dst_len = 0;
                if (!pdf_cmap_hex_to_code(start_hex, &start_code, &start_len, &dummy) ||
                    !pdf_cmap_hex_to_code(end_hex, &end_code, &end_len, &dummy) ||
                    start_len != end_len || end_code < start_code) {
                    continue;
                }

                pdf_cmap_tok_type c =
                    pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                if (c == PDF_CMAP_TOK_HEX) {
                    if (!pdf_cmap_hex_to_code(tok, &dummy, &dst_len, &dst_first)) {
                        continue;
                    }
                    uint32_t count = end_code - start_code + 1u;
                    for (uint32_t i = 0; i < count; i++) {
                        (void)pdf_font_add_tounicode(font, start_code + i, start_len,
                                                     dst_first + i);
                    }
                } else if (c == PDF_CMAP_TOK_LBRACK) {
                    uint32_t current = start_code;
                    while (current <= end_code) {
                        pdf_cmap_tok_type d =
                            pdf_cmap_next_token(stream, stream_len, &pos, tok, sizeof(tok));
                        if (d == PDF_CMAP_TOK_EOF || d == PDF_CMAP_TOK_RBRACK) {
                            break;
                        }
                        if (d != PDF_CMAP_TOK_HEX) {
                            continue;
                        }
                        if (!pdf_cmap_hex_to_code(tok, &dummy, &dst_len, &dst_first)) {
                            continue;
                        }
                        (void)pdf_font_add_tounicode(font, current, start_len, dst_first);
                        current++;
                    }
                }
            }
            continue;
        }
    }

    return 1;
}

static int pdf_parse_font_tounicode(const unsigned char *data, size_t size, int font_obj,
                                    pdf_font_map *font, char *err, size_t errcap) {
    pdf_object_view font_view = {0};
    if (!get_object_content(data, size, font_obj, &font_view)) {
        set_err(err, errcap, "pdf: font object not found");
        return 0;
    }

    int tounicode_obj = 0;
    if (!extract_dict_ref(font_view.data, 0, font_view.len, "/ToUnicode", &tounicode_obj)) {
        pdf_object_view_release(&font_view);
        return 1;
    }
    pdf_object_view_release(&font_view);

    pdf_object_view cmap_obj = {0};
    if (!get_object_content(data, size, tounicode_obj, &cmap_obj)) {
        set_err(err, errcap, "pdf: ToUnicode object not found");
        return 0;
    }

    size_t stream_kw_pos = 0;
    const unsigned char *stream_ptr = NULL;
    size_t stream_len = 0;
    if (!extract_stream_from_object(cmap_obj.data, cmap_obj.len, &stream_ptr, &stream_len,
                                    &stream_kw_pos)) {
        pdf_object_view_release(&cmap_obj);
        return 1;
    }

    const unsigned char *decoded = stream_ptr;
    size_t decoded_len = stream_len;
    unsigned char *decoded_owned = NULL;
    if (buffer_contains(cmap_obj.data, 0, stream_kw_pos, "/FlateDecode") ||
        buffer_contains(cmap_obj.data, 0, stream_kw_pos, "/Fl")) {
        if (!pdf_inflate_auto(stream_ptr, stream_len, &decoded_owned, &decoded_len)) {
            pdf_object_view_release(&cmap_obj);
            set_err(err, errcap, "pdf: failed to decode ToUnicode stream");
            return 0;
        }
        decoded = decoded_owned;
    }

    int ok = pdf_parse_tounicode_stream(decoded, decoded_len, font);
    free(decoded_owned);
    pdf_object_view_release(&cmap_obj);
    if (!ok) {
        set_err(err, errcap, "pdf: failed to parse ToUnicode map");
        return 0;
    }
    return 1;
}

static int pdf_find_inline_dict(const unsigned char *data, size_t start, size_t end,
                                const char *key, size_t *dict_start, size_t *dict_end) {
    size_t key_len = strlen(key);
    if (!data || !key || !dict_start || !dict_end || key_len == 0 || end <= start) {
        return 0;
    }
    for (size_t pos = start; pos + key_len < end; pos++) {
        if (memcmp(&data[pos], key, key_len) != 0) {
            continue;
        }
        size_t p = pos + key_len;
        while (p < end && (data[p] == ' ' || data[p] == '\n' || data[p] == '\r' || data[p] == '\t'))
            p++;
        if (p + 1 >= end || data[p] != '<' || data[p + 1] != '<') {
            continue;
        }
        int depth = 1;
        size_t q = p + 2;
        while (q + 1 < end && depth > 0) {
            if (data[q] == '<' && data[q + 1] == '<') {
                depth++;
                q += 2;
                continue;
            }
            if (data[q] == '>' && data[q + 1] == '>') {
                depth--;
                q += 2;
                continue;
            }
            q++;
        }
        if (depth == 0) {
            *dict_start = p + 2;
            *dict_end = q - 2;
            return 1;
        }
    }
    return 0;
}

static int pdf_collect_fonts_from_dict(const unsigned char *data, size_t size,
                                       const unsigned char *dict_data, size_t dict_start,
                                       size_t dict_end, pdf_font_table *table, char *err,
                                       size_t errcap) {
    size_t pos = dict_start;
    while (pos < dict_end) {
        if (dict_data[pos] != '/') {
            pos++;
            continue;
        }
        pos++;
        char name[32];
        size_t n = 0;
        while (pos < dict_end && !isspace((unsigned char)dict_data[pos]) && dict_data[pos] != '/' &&
               dict_data[pos] != '<' && dict_data[pos] != '>' && dict_data[pos] != '[' &&
               dict_data[pos] != ']' && dict_data[pos] != '(' && dict_data[pos] != ')') {
            if (n + 1 < sizeof(name)) {
                name[n++] = (char)dict_data[pos];
            }
            pos++;
        }
        name[n] = '\0';
        if (n == 0) {
            continue;
        }

        while (pos < dict_end && (dict_data[pos] == ' ' || dict_data[pos] == '\n' ||
                                  dict_data[pos] == '\r' || dict_data[pos] == '\t'))
            pos++;
        if (pos >= dict_end || dict_data[pos] < '0' || dict_data[pos] > '9') {
            continue;
        }

        size_t obj_num;
        if (!pdf_parse_decimal(dict_data, dict_end, &pos, INT_MAX, &obj_num)) {
            set_err(err, errcap, "pdf: invalid font object number");
            return 0;
        }
        while (pos < dict_end && (dict_data[pos] == ' ' || dict_data[pos] == '\n' ||
                                  dict_data[pos] == '\r' || dict_data[pos] == '\t'))
            pos++;
        if (pos >= dict_end || dict_data[pos] < '0' || dict_data[pos] > '9') {
            continue;
        }
        while (pos < dict_end && dict_data[pos] >= '0' && dict_data[pos] <= '9') {
            pos++;
        }
        while (pos < dict_end && (dict_data[pos] == ' ' || dict_data[pos] == '\n' ||
                                  dict_data[pos] == '\r' || dict_data[pos] == '\t'))
            pos++;
        if (pos >= dict_end || dict_data[pos] != 'R' || obj_num <= 0) {
            continue;
        }
        pos++;

        pdf_font_map *font = pdf_font_table_add(table, name);
        if (!font) {
            set_err(err, errcap, "pdf: out of memory");
            return 0;
        }
        if (!pdf_parse_font_tounicode(data, size, (int)obj_num, font, err, errcap)) {
            return 0;
        }
    }
    return 1;
}

static int pdf_collect_page_fonts(const unsigned char *data, size_t size, int page_obj,
                                  pdf_font_table *table, char *err, size_t errcap) {
    if (!table) {
        set_err(err, errcap, "pdf: null font table");
        return 0;
    }

    pdf_object_view page = {0};
    if (!get_object_content(data, size, page_obj, &page)) {
        set_err(err, errcap, "pdf: page object not found");
        return 0;
    }

    pdf_object_view resources = {0};
    const unsigned char *res_data = page.data;
    size_t res_len = page.len;
    int resources_obj = 0;
    if (extract_dict_ref(page.data, 0, page.len, "/Resources", &resources_obj) &&
        resources_obj > 0) {
        if (!get_object_content(data, size, resources_obj, &resources)) {
            pdf_object_view_release(&page);
            set_err(err, errcap, "pdf: resources object not found");
            return 0;
        }
        res_data = resources.data;
        res_len = resources.len;
    }

    int font_dict_obj = 0;
    if (extract_dict_ref(res_data, 0, res_len, "/Font", &font_dict_obj) && font_dict_obj > 0) {
        pdf_object_view font_dict = {0};
        if (!get_object_content(data, size, font_dict_obj, &font_dict)) {
            pdf_object_view_release(&resources);
            pdf_object_view_release(&page);
            set_err(err, errcap, "pdf: font dictionary object not found");
            return 0;
        }
        int ok = pdf_collect_fonts_from_dict(data, size, font_dict.data, 0, font_dict.len, table,
                                             err, errcap);
        pdf_object_view_release(&font_dict);
        pdf_object_view_release(&resources);
        pdf_object_view_release(&page);
        return ok;
    }

    size_t dict_start = 0;
    size_t dict_end = 0;
    int found = pdf_find_inline_dict(res_data, 0, res_len, "/Font", &dict_start, &dict_end);
    int ok = 1;
    if (found) {
        ok = pdf_collect_fonts_from_dict(data, size, res_data, dict_start, dict_end, table, err,
                                         errcap);
    }

    pdf_object_view_release(&resources);
    pdf_object_view_release(&page);
    return ok;
}

static const uint8_t pdf_font8x8_basic[128][8] = {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                                                  {0x7E, 0x81, 0xA5, 0x81, 0xBD, 0x99, 0x81, 0x7E},
                                                  {0x7E, 0xFF, 0xDB, 0xFF, 0xC3, 0xE7, 0xFF, 0x7E},
                                                  {0x6C, 0xFE, 0xFE, 0xFE, 0x7C, 0x38, 0x10, 0x00},
                                                  {0x10, 0x38, 0x7C, 0xFE, 0x7C, 0x38, 0x10, 0x00},
                                                  {0x38, 0x7C, 0x38, 0xFE, 0xFE, 0xD6, 0x10, 0x38},
                                                  {0x10, 0x38, 0x7C, 0xFE, 0xFE, 0x7C, 0x10, 0x38},
                                                  {0x00, 0x00, 0x18, 0x3C, 0x3C, 0x18, 0x00, 0x00},
                                                  {0xFF, 0xFF, 0xE7, 0xC3, 0xC3, 0xE7, 0xFF, 0xFF},
                                                  {0x00, 0x3C, 0x66, 0x42, 0x42, 0x66, 0x3C, 0x00},
                                                  {0xFF, 0xC3, 0x99, 0xBD, 0xBD, 0x99, 0xC3, 0xFF},
                                                  {0x0F, 0x07, 0x0F, 0x7D, 0xCC, 0xCC, 0xCC, 0x78},
                                                  {0x3C, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x7E, 0x18},
                                                  {0x3F, 0x33, 0x3F, 0x30, 0x30, 0x70, 0xF0, 0xE0},
                                                  {0x7F, 0x63, 0x7F, 0x63, 0x63, 0x67, 0xE6, 0xC0},
                                                  {0x99, 0x5A, 0x3C, 0xE7, 0xE7, 0x3C, 0x5A, 0x99},
                                                  {0x80, 0xE0, 0xF8, 0xFE, 0xF8, 0xE0, 0x80, 0x00},
                                                  {0x02, 0x0E, 0x3E, 0xFE, 0x3E, 0x0E, 0x02, 0x00},
                                                  {0x18, 0x3C, 0x7E, 0x18, 0x18, 0x7E, 0x3C, 0x18},
                                                  {0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x66, 0x00},
                                                  {0x7F, 0xDB, 0xDB, 0x7B, 0x1B, 0x1B, 0x1B, 0x00},
                                                  {0x3E, 0x63, 0x38, 0x6C, 0x6C, 0x38, 0xCC, 0x7F},
                                                  {0x00, 0x00, 0x00, 0x00, 0x7E, 0x7E, 0x7E, 0x00},
                                                  {0x18, 0x3C, 0x7E, 0x18, 0x7E, 0x3C, 0x18, 0xFF},
                                                  {0x18, 0x3C, 0x7E, 0x18, 0x18, 0x18, 0x18, 0x00},
                                                  {0x18, 0x18, 0x18, 0x18, 0x7E, 0x3C, 0x18, 0x00},
                                                  {0x00, 0x18, 0x0C, 0xFE, 0x0C, 0x18, 0x00, 0x00},
                                                  {0x00, 0x30, 0x60, 0xFE, 0x60, 0x30, 0x00, 0x00},
                                                  {0x00, 0x00, 0xC0, 0xC0, 0xC0, 0xFE, 0x00, 0x00},
                                                  {0x00, 0x24, 0x66, 0xFF, 0x66, 0x24, 0x00, 0x00},
                                                  {0x00, 0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x00, 0x00},
                                                  {0x00, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00, 0x00},
                                                  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                                                  {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},
                                                  {0x36, 0x36, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00},
                                                  {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},
                                                  {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},
                                                  {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},
                                                  {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},
                                                  {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},
                                                  {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},
                                                  {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},
                                                  {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
                                                  {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},
                                                  {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},
                                                  {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},
                                                  {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},
                                                  {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},
                                                  {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},
                                                  {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},
                                                  {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},
                                                  {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},
                                                  {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},
                                                  {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},
                                                  {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},
                                                  {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},
                                                  {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},
                                                  {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},
                                                  {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},
                                                  {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},
                                                  {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},
                                                  {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},
                                                  {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},
                                                  {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},
                                                  {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},
                                                  {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},
                                                  {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},
                                                  {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},
                                                  {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},
                                                  {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},
                                                  {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},
                                                  {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},
                                                  {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},
                                                  {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
                                                  {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},
                                                  {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},
                                                  {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},
                                                  {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},
                                                  {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},
                                                  {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},
                                                  {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},
                                                  {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},
                                                  {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},
                                                  {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},
                                                  {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
                                                  {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},
                                                  {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
                                                  {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
                                                  {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},
                                                  {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},
                                                  {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},
                                                  {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},
                                                  {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00},
                                                  {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},
                                                  {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00},
                                                  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
                                                  {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
                                                  {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},
                                                  {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00},
                                                  {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},
                                                  {0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00},
                                                  {0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00},
                                                  {0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00},
                                                  {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},
                                                  {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00},
                                                  {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
                                                  {0x18, 0x00, 0x1C, 0x18, 0x18, 0x18, 0x1B, 0x0E},
                                                  {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},
                                                  {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
                                                  {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},
                                                  {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00},
                                                  {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},
                                                  {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F},
                                                  {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},
                                                  {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00},
                                                  {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},
                                                  {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00},
                                                  {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},
                                                  {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
                                                  {0x00, 0x00, 0x63, 0x63, 0x6B, 0x7F, 0x36, 0x00},
                                                  {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00},
                                                  {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},
                                                  {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00},
                                                  {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},
                                                  {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},
                                                  {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},
                                                  {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                                                  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

typedef struct {
    float x0;
    float y0;
    float x1;
    float y1;
    int starts_subpath;
} pdf_seg;

typedef struct {
    pdf_seg segs[4096];
    int seg_count;
    int subpath_first_seg;
    float current_x;
    float current_y;
    float subpath_start_x;
    float subpath_start_y;
    int has_current;
    int has_subpath;
    int overflow;
} pdf_path;

typedef struct {
    unsigned references;
    size_t bytes;
    unsigned char bits[];
} pdf_clip_mask;

typedef struct {
    size_t bytes_used;
    size_t tests_left;
    unsigned calls_left;
} pdf_clip_budget;

typedef struct {
    float ctm[6];
    uint8_t fill_r;
    uint8_t fill_g;
    uint8_t fill_b;
    uint8_t stroke_r;
    uint8_t stroke_g;
    uint8_t stroke_b;
    float line_width_pts;
    int fill_space;
    int stroke_space;
    pdf_clip_mask *clip;
} pdf_graphics_state;

typedef struct {
    double inverse[6];
    double box[4];
} pdf_form_clip;

typedef struct {
    size_t bytes_left;
    unsigned calls_left;
} pdf_form_budget;

typedef struct {
    cupidimage_image *img;
    const unsigned char *document;
    size_t document_size;
    int page_object;
    int image_failed;
    char *error;
    size_t error_capacity;
    float page_x0_pts;
    float page_top_pts;
    float line_x_pts;
    float line_y_pts;
    float text_x_pts;
    float text_y_pts;
    float font_size_pts;
    float leading_pts;
    pdf_graphics_state gs;
    pdf_graphics_state gs_stack[32];
    int gs_stack_len;
    pdf_path path;
    int pending_clip;
    pdf_clip_budget *clip_budget;
    int in_text;
    const pdf_font_table *font_table;
    const pdf_font_map *current_font;
    int form_objects[16];
    pdf_form_clip form_clips[16];
    int form_depth;
    pdf_form_budget *form_budget;
} pdf_render_state;

static int pdf_draw_form(pdf_render_state *st, int object, const pdf_object_view *form);

static void pdf_clip_retain(pdf_clip_mask *clip) {
    if (clip)
        clip->references++;
}

static void pdf_clip_release(pdf_render_state *st, pdf_clip_mask *clip) {
    if (clip && --clip->references == 0) {
        st->clip_budget->bytes_used -= clip->bytes;
        free(clip);
    }
}

static void pdf_render_state_release(pdf_render_state *st) {
    pdf_clip_release(st, st->gs.clip);
    for (int i = 0; i < st->gs_stack_len; i++)
        pdf_clip_release(st, st->gs_stack[i].clip);
}

static int pdf_is_ws(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\0';
}

static int pdf_is_delim(unsigned char c) {
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' ||
           c == '}' || c == '/' || c == '%';
}

static void pdf_skip_ws_and_comments(const unsigned char *data, size_t size, size_t *pos) {
    while (*pos < size) {
        if (pdf_is_ws(data[*pos])) {
            (*pos)++;
            continue;
        }
        if (data[*pos] == '%') {
            while (*pos < size && data[*pos] != '\n' && data[*pos] != '\r') {
                (*pos)++;
            }
            continue;
        }
        break;
    }
}

static unsigned char pdf_clamp_u8(double v) {
    if (v < 0.0)
        v = 0.0;
    if (v > 1.0)
        v = 1.0;
    return (unsigned char)(v * 255.0 + 0.5);
}

static int pdf_parse_number_token(const unsigned char *data, size_t size, size_t *pos,
                                  double *out) {
    size_t p = *pos;
    int saw_digit = 0;
    int saw_dot = 0;

    if (p < size && (data[p] == '+' || data[p] == '-')) {
        p++;
    }
    while (p < size) {
        if (data[p] >= '0' && data[p] <= '9') {
            saw_digit = 1;
            p++;
            continue;
        }
        if (data[p] == '.' && !saw_dot) {
            saw_dot = 1;
            p++;
            continue;
        }
        break;
    }

    if (!saw_digit) {
        return 0;
    }

    char tmp[64];
    size_t n = p - *pos;
    if (n >= sizeof(tmp)) {
        n = sizeof(tmp) - 1;
    }
    memcpy(tmp, &data[*pos], n);
    tmp[n] = '\0';
    *out = strtod(tmp, NULL);
    *pos = p;
    return 1;
}

static int pdf_parse_name_token(const unsigned char *data, size_t size, size_t *pos, char **out_str,
                                size_t *out_len) {
    if (*pos >= size || data[*pos] != '/') {
        return 0;
    }
    size_t start = *pos + 1;
    size_t p = start;
    while (p < size && !pdf_is_ws(data[p]) && !pdf_is_delim(data[p])) {
        p++;
    }

    size_t len = p - start;
    char *s = (char *)malloc(len + 1);
    if (!s) {
        return 0;
    }
    memcpy(s, &data[start], len);
    s[len] = '\0';

    *out_str = s;
    *out_len = len;
    *pos = p;
    return 1;
}

static int pdf_parse_literal_string(const unsigned char *data, size_t size, size_t *pos,
                                    char **out_str, size_t *out_len) {
    if (*pos >= size || data[*pos] != '(') {
        return 0;
    }

    size_t p = *pos + 1;
    int depth = 1;
    size_t cap = 64;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        return 0;
    }

    while (p < size) {
        unsigned char c = data[p++];

        if (c == '\\') {
            if (p >= size)
                break;
            unsigned char e = data[p++];
            unsigned char outc = e;
            if (e == 'n')
                outc = '\n';
            else if (e == 'r')
                outc = '\r';
            else if (e == 't')
                outc = '\t';
            else if (e == 'b')
                outc = '\b';
            else if (e == 'f')
                outc = '\f';
            else if (e == '\\' || e == '(' || e == ')')
                outc = e;
            else if (e == '\n' || e == '\r') {
                if (e == '\r' && p < size && data[p] == '\n')
                    p++;
                continue;
            } else if (e >= '0' && e <= '7') {
                int v = e - '0';
                for (int i = 0; i < 2 && p < size && data[p] >= '0' && data[p] <= '7'; i++) {
                    v = (v << 3) + (data[p] - '0');
                    p++;
                }
                outc = (unsigned char)v;
            }

            if (len + 1 >= cap) {
                size_t ncap = cap * 2;
                char *n = (char *)realloc(buf, ncap);
                if (!n) {
                    free(buf);
                    return 0;
                }
                buf = n;
                cap = ncap;
            }
            buf[len++] = (char)outc;
            continue;
        }

        if (c == '(') {
            depth++;
            if (len + 1 >= cap) {
                size_t ncap = cap * 2;
                char *n = (char *)realloc(buf, ncap);
                if (!n) {
                    free(buf);
                    return 0;
                }
                buf = n;
                cap = ncap;
            }
            buf[len++] = (char)c;
            continue;
        }

        if (c == ')') {
            depth--;
            if (depth == 0) {
                *pos = p;
                buf[len] = '\0';
                *out_str = buf;
                *out_len = len;
                return 1;
            }
            if (len + 1 >= cap) {
                size_t ncap = cap * 2;
                char *n = (char *)realloc(buf, ncap);
                if (!n) {
                    free(buf);
                    return 0;
                }
                buf = n;
                cap = ncap;
            }
            buf[len++] = (char)c;
            continue;
        }

        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            char *n = (char *)realloc(buf, ncap);
            if (!n) {
                free(buf);
                return 0;
            }
            buf = n;
            cap = ncap;
        }
        buf[len++] = (char)c;
    }

    free(buf);
    return 0;
}

static int pdf_parse_hex_string(const unsigned char *data, size_t size, size_t *pos, char **out_str,
                                size_t *out_len) {
    if (*pos >= size || data[*pos] != '<') {
        return 0;
    }
    if (*pos + 1 < size && data[*pos + 1] == '<') {
        return 0;
    }

    size_t p = *pos + 1;
    size_t cap = 64;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        return 0;
    }

    int have_hi = 0;
    int hi = 0;
    while (p < size) {
        unsigned char c = data[p++];
        if (c == '>') {
            break;
        }
        if (isspace(c)) {
            continue;
        }

        int v = -1;
        if (c >= '0' && c <= '9')
            v = c - '0';
        else if (c >= 'a' && c <= 'f')
            v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            v = c - 'A' + 10;
        if (v < 0) {
            free(buf);
            return 0;
        }

        if (!have_hi) {
            hi = v;
            have_hi = 1;
        } else {
            unsigned char outc = (unsigned char)((hi << 4) | v);
            if (len + 1 >= cap) {
                size_t ncap = cap * 2;
                char *n = (char *)realloc(buf, ncap);
                if (!n) {
                    free(buf);
                    return 0;
                }
                buf = n;
                cap = ncap;
            }
            buf[len++] = (char)outc;
            have_hi = 0;
        }
    }

    if (have_hi) {
        unsigned char outc = (unsigned char)(hi << 4);
        if (len + 1 >= cap) {
            size_t ncap = cap * 2;
            char *n = (char *)realloc(buf, ncap);
            if (!n) {
                free(buf);
                return 0;
            }
            buf = n;
            cap = ncap;
        }
        buf[len++] = (char)outc;
    }

    buf[len] = '\0';
    *out_str = buf;
    *out_len = len;
    *pos = p;
    return 1;
}

static int pdf_parse_array_token(const unsigned char *data, size_t size, size_t *pos,
                                 char **out_str, size_t *out_len) {
    if (*pos >= size || data[*pos] != '[') {
        return 0;
    }

    size_t start = *pos + 1;
    size_t p = start;
    int depth = 1;

    while (p < size) {
        unsigned char c = data[p];
        if (c == '%') {
            while (p < size && data[p] != '\n' && data[p] != '\r')
                p++;
            continue;
        }
        if (c == '(') {
            size_t tmp = p;
            char *dummy = NULL;
            size_t dlen = 0;
            if (!pdf_parse_literal_string(data, size, &tmp, &dummy, &dlen)) {
                return 0;
            }
            free(dummy);
            p = tmp;
            continue;
        }
        if (c == '[') {
            depth++;
            p++;
            continue;
        }
        if (c == ']') {
            depth--;
            if (depth == 0) {
                size_t len = p - start;
                char *s = (char *)malloc(len + 1);
                if (!s) {
                    return 0;
                }
                memcpy(s, &data[start], len);
                s[len] = '\0';
                *out_str = s;
                *out_len = len;
                *pos = p + 1;
                return 1;
            }
            p++;
            continue;
        }
        p++;
    }

    return 0;
}

static void pdf_set_pixel(pdf_render_state *st, int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    cupidimage_image *img = st->img;
    if (!img || !img->rgba) {
        return;
    }
    if (x < 0 || y < 0 || x >= (int)img->width || y >= (int)img->height) {
        return;
    }
    size_t pixel = (size_t)y * img->width + (size_t)x;
    if (st->gs.clip && !(st->gs.clip->bits[pixel / 8] & (1u << (pixel % 8))))
        return;
    double px = st->page_x0_pts + (x + 0.5) * (72.0 / 96.0);
    double py = st->page_top_pts - (y + 0.5) * (72.0 / 96.0);
    for (int i = 0; i < st->form_depth; i++) {
        const pdf_form_clip *clip = &st->form_clips[i];
        const double *m = clip->inverse;
        double u = m[0] * px + m[2] * py + m[4];
        double v = m[1] * px + m[3] * py + m[5];
        if (u < clip->box[0] || u >= clip->box[2] || v < clip->box[1] || v >= clip->box[3])
            return;
    }
    size_t idx = pixel * 4u;
    img->rgba[idx + 0] = r;
    img->rgba[idx + 1] = g;
    img->rgba[idx + 2] = b;
    img->rgba[idx + 3] = 255;
}

static void pdf_matrix_identity(float m[6]) {
    m[0] = 1.0f;
    m[1] = 0.0f;
    m[2] = 0.0f;
    m[3] = 1.0f;
    m[4] = 0.0f;
    m[5] = 0.0f;
}

static void pdf_matrix_concat(float dst[6], const float left[6]) {
    float a = dst[0] * left[0] + dst[2] * left[1];
    float b = dst[1] * left[0] + dst[3] * left[1];
    float c = dst[0] * left[2] + dst[2] * left[3];
    float d = dst[1] * left[2] + dst[3] * left[3];
    float e = dst[0] * left[4] + dst[2] * left[5] + dst[4];
    float f = dst[1] * left[4] + dst[3] * left[5] + dst[5];
    dst[0] = a;
    dst[1] = b;
    dst[2] = c;
    dst[3] = d;
    dst[4] = e;
    dst[5] = f;
}

static void pdf_user_to_pixel(const pdf_render_state *st, float ux, float uy, float *px,
                              float *py) {
    float tx = st->gs.ctm[0] * ux + st->gs.ctm[2] * uy + st->gs.ctm[4];
    float ty = st->gs.ctm[1] * ux + st->gs.ctm[3] * uy + st->gs.ctm[5];
    float s = 96.0f / 72.0f;
    *px = (tx - st->page_x0_pts) * s;
    *py = (st->page_top_pts - ty) * s;
}

/* Borrow an inline dictionary or own a resolved indirect dictionary. The parent
 * view must stay alive until a borrowed result has been consumed. */
static int pdf_image_dictionary(const pdf_render_state *st, const pdf_object_view *parent,
                                const char *key, pdf_object_view *result) {
    size_t pos;
    int object;
    if (!pdf_page_value(parent, key, &pos))
        return 0;
    if (pdf_page_reference(parent, pos, &object)) {
        if (!get_object_content(st->document, st->document_size, object, result))
            return 0;
        pos = 0;
        pdf_skip_ws_and_comments(result->data, result->len, &pos);
        if (result->len - pos < 2 || result->data[pos] != '<' || result->data[pos + 1] != '<') {
            pdf_object_view_release(result);
            return 0;
        }
        return 1;
    }
    if (pos + 1 >= parent->len || parent->data[pos] != '<' || parent->data[pos + 1] != '<')
        return 0;
    result->data = parent->data + pos;
    result->len = parent->len - pos;
    return 1;
}

static int pdf_image_object(const pdf_render_state *st, const char *name, int *object) {
    int page = st->page_object;
    for (int depth = 0; page > 0 && depth < 64; depth++) {
        pdf_object_view node = {0}, resources = {0}, xobjects = {0};
        if (!get_object_content(st->document, st->document_size, page, &node))
            return 0;
        size_t pos;
        int parent = 0, found = 0;
        if (pdf_page_value(&node, "Resources", &pos)) {
            found = pdf_image_dictionary(st, &node, "Resources", &resources) &&
                    pdf_image_dictionary(st, &resources, "XObject", &xobjects) &&
                    pdf_page_value(&xobjects, name, &pos) &&
                    pdf_page_reference(&xobjects, pos, object);
            /* The closest Resources entry replaces the entire ancestor entry. */
            pdf_object_view_release(&xobjects);
            pdf_object_view_release(&resources);
            pdf_object_view_release(&node);
            return found;
        }
        if (pdf_page_value(&node, "Parent", &pos))
            pdf_page_reference(&node, pos, &parent);
        pdf_object_view_release(&node);
        page = parent;
    }
    return 0;
}

static int pdf_image_integer(const pdf_render_state *st, const pdf_object_view *view,
                             const char *key, int *value) {
    size_t pos;
    if (!pdf_page_value(view, key, &pos))
        return 0;
    pdf_object_view indirect = {0};
    int object;
    if (pdf_page_reference(view, pos, &object)) {
        if (!get_object_content(st->document, st->document_size, object, &indirect))
            return 0;
        view = &indirect;
        pos = 0;
        pdf_skip_ws_and_comments(view->data, view->len, &pos);
    }
    double number;
    int ok = parse_pdf_number(view->data, view->len, &pos, &number) && number >= 0 &&
             number <= INT_MAX && floor(number) == number &&
             (pos == view->len || pdf_is_ws(view->data[pos]) || pdf_is_delim(view->data[pos]));
    if (ok)
        *value = (int)number;
    pdf_object_view_release(&indirect);
    return ok;
}

static int pdf_image_name_is(const pdf_object_view *view, const char *key, const char *name) {
    size_t pos, length = strlen(name);
    if (!pdf_page_value(view, key, &pos) || pos >= view->len || view->data[pos++] != '/')
        return 0;
    return length <= view->len - pos && memcmp(view->data + pos, name, length) == 0 &&
           (pos + length == view->len || pdf_is_ws(view->data[pos + length]) ||
            pdf_is_delim(view->data[pos + length]));
}

/* Find the end of the outer dictionary without matching text inside a string. */
static int pdf_image_stream(const pdf_render_state *st, const pdf_object_view *view,
                            const unsigned char **stream, size_t *length) {
    size_t pos = 0;
    int depth = 0, declared;
    if (!pdf_image_integer(st, view, "Length", &declared))
        return 0;
    while (pos < view->len) {
        pdf_skip_ws_and_comments(view->data, view->len, &pos);
        if (pos >= view->len)
            return 0;
        unsigned char c = view->data[pos++];
        if (c == '(') {
            int strings = 1;
            while (pos < view->len && strings) {
                c = view->data[pos++];
                if (c == '\\' && pos < view->len)
                    pos++;
                else if (c == '(')
                    strings++;
                else if (c == ')')
                    strings--;
            }
        } else if (c == '<') {
            if (pos < view->len && view->data[pos] == '<') {
                depth++;
                pos++;
            } else
                while (pos < view->len && view->data[pos++] != '>') {
                }
        } else if (c == '>' && pos < view->len && view->data[pos] == '>') {
            pos++;
            if (--depth == 0)
                break;
        }
    }
    pdf_skip_ws_and_comments(view->data, view->len, &pos);
    if (depth != 0 || view->len - pos < 7 || memcmp(view->data + pos, "stream", 6) != 0)
        return 0;
    pos += 6;
    if (view->data[pos] == '\r') {
        pos++;
        if (pos < view->len && view->data[pos] == '\n')
            pos++;
    } else if (view->data[pos] == '\n')
        pos++;
    else
        return 0;
    if ((size_t)declared > view->len - pos)
        return 0;
    *stream = view->data + pos;
    *length = (size_t)declared;
    pos += *length;
    pdf_skip_ws_and_comments(view->data, view->len, &pos);
    return view->len - pos >= 9 && memcmp(view->data + pos, "endstream", 9) == 0;
}

static unsigned char pdf_image_paeth(int a, int b, int c) {
    int p = a + b - c, da = abs(p - a), db = abs(p - b), dc = abs(p - c);
    return (unsigned char)(da <= db && da <= dc ? a : db <= dc ? b : c);
}

static int pdf_image_filter_is(const pdf_object_view *view, const char *name) {
    if (pdf_image_name_is(view, "Filter", name))
        return 1;
    size_t pos, length = strlen(name);
    if (!pdf_page_value(view, "Filter", &pos) || pos >= view->len || view->data[pos++] != '[')
        return 0;
    pdf_skip_ws_and_comments(view->data, view->len, &pos);
    if (pos >= view->len || view->data[pos++] != '/' || length > view->len - pos ||
        memcmp(view->data + pos, name, length) != 0)
        return 0;
    pos += length;
    pdf_skip_ws_and_comments(view->data, view->len, &pos);
    return pos < view->len && view->data[pos] == ']';
}

/* Check the frame before invoking the decoder so a small image dictionary cannot
 * hide a JPEG frame with enormous dimensions or a different component count. */
static int pdf_image_jpeg_matches(const unsigned char *stream, size_t length, int width, int height,
                                  int components, int transform) {
    if (length < 2 || stream[0] != 0xff || stream[1] != 0xd8)
        return 0;
    size_t pos = 2;
    int have_frame = 0;
    while (pos < length) {
        if (stream[pos++] != 0xff)
            return 0;
        while (pos < length && stream[pos] == 0xff)
            pos++;
        if (pos >= length)
            return 0;
        unsigned marker = stream[pos++];
        if (marker == 0xda)
            return have_frame;
        if (marker == 0xd9 || marker == 0)
            return 0;
        if (marker == 0x01 || (marker >= 0xd0 && marker <= 0xd7))
            continue;
        if (length - pos < 2)
            return 0;
        size_t segment = (size_t)stream[pos] * 256u + stream[pos + 1];
        if (segment < 2 || segment > length - pos)
            return 0;
        if (components == 3 && marker == 0xee && segment >= 14 &&
            memcmp(stream + pos + 2, "Adobe", 5) == 0 && stream[pos + 13] == 0 && transform != 1)
            return 0;
        if (marker == 0xc0 || marker == 0xc2) {
            if (have_frame || !(segment >= 8 && stream[pos + 2] == 8 &&
                                (int)stream[pos + 3] * 256 + stream[pos + 4] == height &&
                                (int)stream[pos + 5] * 256 + stream[pos + 6] == width &&
                                stream[pos + 7] == components))
                return 0;
            have_frame = 1;
        }
        pos += segment;
    }
    return 0;
}

static int pdf_image_jpeg_samples(const pdf_render_state *st, const pdf_object_view *view,
                                  int width, int height, int components, unsigned char **samples) {
    pdf_object_view params = {0};
    size_t pos;
    int transform = -1;
    if (pdf_page_value(view, "DecodeParms", &pos)) {
        int ok = pdf_image_dictionary(st, view, "DecodeParms", &params) &&
                 (!pdf_page_value(&params, "ColorTransform", &pos) ||
                  pdf_image_integer(st, &params, "ColorTransform", &transform));
        pdf_object_view_release(&params);
        if (!ok || (transform != -1 && transform != 1))
            return 0;
    }
    const unsigned char *stream;
    size_t length;
    if (!pdf_image_stream(st, view, &stream, &length) ||
        !pdf_image_jpeg_matches(stream, length, width, height, components, transform))
        return 0;
    cupidimage_image image = {0};
    if (!cupidimage_load_jpeg(stream, length, &image, NULL, 0))
        return 0;
    size_t count = (size_t)width * height;
    unsigned char *decoded = (unsigned char *)malloc(count * components);
    if (!decoded || image.width != (uint32_t)width || image.height != (uint32_t)height) {
        free(decoded);
        cupidimage_free(&image);
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        for (int c = 0; c < components; c++)
            decoded[i * components + c] = image.rgba[i * 4 + c];
    }
    cupidimage_free(&image);
    *samples = decoded;
    return 1;
}

static int pdf_image_samples(const pdf_render_state *st, const pdf_object_view *view, int width,
                             int height, int components, unsigned char **samples) {
    const unsigned char *stream;
    size_t length, row = (size_t)width * components, bytes = row * height, pos;
    if (pdf_image_filter_is(view, "DCTDecode"))
        return pdf_image_jpeg_samples(st, view, width, height, components, samples);
    int predictor = 1, flate = pdf_image_filter_is(view, "FlateDecode");
    if (pdf_page_value(view, "Filter", &pos) && !flate)
        return 0;
    pdf_object_view params = {0};
    if (pdf_page_value(view, "DecodeParms", &pos)) {
        if (!flate || !pdf_image_dictionary(st, view, "DecodeParms", &params))
            return 0;
        int columns = 1, colors = 1, bits = 8;
        int ok = (!pdf_page_value(&params, "Predictor", &pos) ||
                  pdf_image_integer(st, &params, "Predictor", &predictor)) &&
                 (!pdf_page_value(&params, "Columns", &pos) ||
                  pdf_image_integer(st, &params, "Columns", &columns)) &&
                 (!pdf_page_value(&params, "Colors", &pos) ||
                  pdf_image_integer(st, &params, "Colors", &colors)) &&
                 (!pdf_page_value(&params, "BitsPerComponent", &pos) ||
                  pdf_image_integer(st, &params, "BitsPerComponent", &bits));
        pdf_object_view_release(&params);
        if (!ok || (predictor != 1 && (columns != width || colors != components || bits != 8)))
            return 0;
    }
    if (predictor != 1 && predictor != 2 && (predictor < 10 || predictor > 15))
        return 0;
    if (!pdf_image_stream(st, view, &stream, &length))
        return 0;
    size_t encoded_row = row + (predictor >= 10 ? 1u : 0u);
    size_t expected = encoded_row * height;
    unsigned char *decoded = (unsigned char *)malloc(expected);
    if (!decoded)
        return 0;
    size_t actual = 0;
    if (flate) {
        if (!pdf_zlib_decompress(stream, length, decoded, expected, &actual) || actual != expected)
            goto fail;
    } else {
        if (length != bytes)
            goto fail;
        memcpy(decoded, stream, bytes);
    }
    for (int y = 0; y < height; y++) {
        unsigned char *scan = decoded + (size_t)y * encoded_row;
        int filter = predictor >= 10 ? *scan++ : predictor == 2 ? 1 : 0;
        if (filter > 4)
            goto fail;
        for (size_t x = 0; x < row; x++) {
            int left = x >= (size_t)components ? scan[x - components] : 0;
            int up = y ? decoded[(size_t)(y - 1) * row + x] : 0;
            int upper_left =
                y && x >= (size_t)components ? decoded[(size_t)(y - 1) * row + x - components] : 0;
            if (filter == 1)
                scan[x] = (unsigned char)(scan[x] + left);
            else if (filter == 2)
                scan[x] = (unsigned char)(scan[x] + up);
            else if (filter == 3)
                scan[x] = (unsigned char)(scan[x] + (left + up) / 2);
            else if (filter == 4)
                scan[x] = (unsigned char)(scan[x] + pdf_image_paeth(left, up, upper_left));
        }
        memmove(decoded + (size_t)y * row, scan, row);
    }
    *samples = decoded;
    return 1;
fail:
    free(decoded);
    return 0;
}

static int pdf_draw_image(pdf_render_state *st, const char *name) {
    pdf_object_view image = {0};
    unsigned char *samples = NULL;
    int object, width, height, bits, components, ok = 0;
    size_t pos;
    if (!pdf_image_object(st, name, &object) ||
        !get_object_content(st->document, st->document_size, object, &image))
        goto done;
    if (pdf_image_name_is(&image, "Subtype", "Form")) {
        ok = pdf_draw_form(st, object, &image);
        pdf_object_view_release(&image);
        return ok;
    }
    if (!pdf_image_name_is(&image, "Subtype", "Image"))
        goto done;
    components = pdf_image_name_is(&image, "ColorSpace", "DeviceRGB")    ? 3
                 : pdf_image_name_is(&image, "ColorSpace", "DeviceGray") ? 1
                                                                         : 0;
    if (!components || !pdf_image_integer(st, &image, "Width", &width) ||
        !pdf_image_integer(st, &image, "Height", &height) ||
        !pdf_image_integer(st, &image, "BitsPerComponent", &bits) || bits != 8 || width < 1 ||
        height < 1 || (double)width * height * components > 64.0 * 1024 * 1024 ||
        pdf_page_value(&image, "SMask", &pos) || pdf_page_value(&image, "Mask", &pos))
        goto done;
    if (pdf_page_value(&image, "ImageMask", &pos) &&
        (image.len - pos < 5 || memcmp(image.data + pos, "false", 5) != 0))
        goto done;
    double decode[6] = {0, 1, 0, 1, 0, 1};
    if (pdf_page_value(&image, "Decode", &pos)) {
        if (pos >= image.len || image.data[pos++] != '[')
            goto done;
        for (int i = 0; i < 2 * components; i++) {
            pdf_skip_ws_and_comments(image.data, image.len, &pos);
            if (!parse_pdf_number(image.data, image.len, &pos, &decode[i]))
                goto done;
        }
        pdf_skip_ws_and_comments(image.data, image.len, &pos);
        if (pos >= image.len || image.data[pos] != ']')
            goto done;
    }
    if (!pdf_image_samples(st, &image, width, height, components, &samples))
        goto done;
    const float *m = st->gs.ctm;
    double det = (double)m[0] * m[3] - (double)m[1] * m[2];
    if (!isfinite(det))
        goto done;
    if (det == 0) {
        ok = 1;
        goto done;
    }
    double min_x = st->img->width, max_x = 0, min_y = st->img->height, max_y = 0;
    for (int corner = 0; corner < 4; corner++) {
        float x, y;
        pdf_user_to_pixel(st, (float)(corner & 1), (float)(corner >> 1), &x, &y);
        if (!isfinite(x) || !isfinite(y))
            goto done;
        min_x = fmin(min_x, x);
        max_x = fmax(max_x, x);
        min_y = fmin(min_y, y);
        max_y = fmax(max_y, y);
    }
    /* Clamp before integer conversion, including for enormous off-page transforms. */
    int x0 = (int)fmax(0, fmin(st->img->width, floor(min_x)));
    int x1 = (int)fmax(0, fmin(st->img->width, ceil(max_x)));
    int y0 = (int)fmax(0, fmin(st->img->height, floor(min_y)));
    int y1 = (int)fmax(0, fmin(st->img->height, ceil(max_y)));
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            double tx = (x + 0.5) * (72.0 / 96.0) + st->page_x0_pts - m[4];
            double ty = st->page_top_pts - (y + 0.5) * (72.0 / 96.0) - m[5];
            double u = (m[3] * tx - m[2] * ty) / det;
            double v = (m[0] * ty - m[1] * tx) / det;
            if (u < 0 || u >= 1 || v <= 0 || v > 1)
                continue;
            int sx = (int)(u * width), sy = (int)((1 - v) * height);
            if (sx >= width || sy >= height)
                continue;
            const unsigned char *sample = samples + ((size_t)sy * width + sx) * components;
            uint8_t rgb[3];
            for (int c = 0; c < 3; c++) {
                int k = components == 1 ? 0 : c;
                double value =
                    decode[2 * k] + (sample[k] / 255.0) * (decode[2 * k + 1] - decode[2 * k]);
                rgb[c] = pdf_clamp_u8(value);
            }
            pdf_set_pixel(st, x, y, rgb[0], rgb[1], rgb[2]);
        }
    }
    ok = 1;
done:
    free(samples);
    pdf_object_view_release(&image);
    if (!ok)
        set_err(st->error, st->error_capacity, "pdf: invalid or unsupported image XObject");
    return ok;
}

static void pdf_path_clear(pdf_path *p) {
    if (!p)
        return;
    p->seg_count = 0;
    p->subpath_first_seg = 0;
    p->has_current = 0;
    p->has_subpath = 0;
    p->overflow = 0;
    p->current_x = 0.0f;
    p->current_y = 0.0f;
    p->subpath_start_x = 0.0f;
    p->subpath_start_y = 0.0f;
}

static void pdf_path_add_seg(pdf_path *p, float x0, float y0, float x1, float y1) {
    if (!p)
        return;
    if (p->seg_count >= (int)(sizeof(p->segs) / sizeof(p->segs[0]))) {
        p->overflow = 1;
        return;
    }
    p->segs[p->seg_count].x0 = x0;
    p->segs[p->seg_count].y0 = y0;
    p->segs[p->seg_count].x1 = x1;
    p->segs[p->seg_count].y1 = y1;
    p->segs[p->seg_count].starts_subpath = p->seg_count == p->subpath_first_seg;
    p->seg_count++;
}

static void pdf_path_move_to(pdf_path *p, float x, float y) {
    if (!p)
        return;
    p->subpath_first_seg = p->seg_count;
    p->current_x = x;
    p->current_y = y;
    p->subpath_start_x = x;
    p->subpath_start_y = y;
    p->has_current = 1;
    p->has_subpath = 1;
}

static void pdf_path_line_to(pdf_path *p, float x, float y) {
    if (!p)
        return;
    if (!p->has_current) {
        pdf_path_move_to(p, x, y);
        return;
    }
    pdf_path_add_seg(p, p->current_x, p->current_y, x, y);
    p->current_x = x;
    p->current_y = y;
}

static void pdf_path_close(pdf_path *p) {
    if (!p || !p->has_current || !p->has_subpath)
        return;
    if (fabsf(p->current_x - p->subpath_start_x) > 1e-4f ||
        fabsf(p->current_y - p->subpath_start_y) > 1e-4f) {
        pdf_path_add_seg(p, p->current_x, p->current_y, p->subpath_start_x, p->subpath_start_y);
    }
    p->current_x = p->subpath_start_x;
    p->current_y = p->subpath_start_y;
}

/* Paths retain device coordinates when cm or q/Q changes the graphics state. */
static void pdf_path_move_user(pdf_render_state *st, float x, float y) {
    float px, py;
    pdf_user_to_pixel(st, x, y, &px, &py);
    pdf_path_move_to(&st->path, px, py);
}

static void pdf_path_line_user(pdf_render_state *st, float x, float y) {
    float px, py;
    pdf_user_to_pixel(st, x, y, &px, &py);
    pdf_path_line_to(&st->path, px, py);
}

static float pdf_point_to_segment_dist_sq(float px, float py, float x0, float y0, float x1,
                                          float y1) {
    float vx = x1 - x0;
    float vy = y1 - y0;
    float wx = px - x0;
    float wy = py - y0;
    float c1 = vx * wx + vy * wy;
    if (c1 <= 0.0f) {
        float dx = px - x0;
        float dy = py - y0;
        return dx * dx + dy * dy;
    }
    float c2 = vx * vx + vy * vy;
    if (c2 <= 1e-8f) {
        float dx = px - x0;
        float dy = py - y0;
        return dx * dx + dy * dy;
    }
    if (c1 >= c2) {
        float dx = px - x1;
        float dy = py - y1;
        return dx * dx + dy * dy;
    }
    float t = c1 / c2;
    float qx = x0 + t * vx;
    float qy = y0 + t * vy;
    float dx = px - qx;
    float dy = py - qy;
    return dx * dx + dy * dy;
}

static void pdf_flatten_curve(pdf_render_state *st, float x0, float y0, float x1, float y1,
                              float x2, float y2, float x3, float y3, unsigned depth) {
    if (st->path.seg_count >= 4096) {
        st->path.overflow = 1;
        return;
    }
    /* Subdivide until both control points are within a quarter pixel of the chord. */
    if (depth >= 12 || (pdf_point_to_segment_dist_sq(x1, y1, x0, y0, x3, y3) <= 0.0625f &&
                        pdf_point_to_segment_dist_sq(x2, y2, x0, y0, x3, y3) <= 0.0625f)) {
        pdf_path_line_to(&st->path, x3, y3);
        return;
    }
    float ax = x0 * 0.5f + x1 * 0.5f, ay = y0 * 0.5f + y1 * 0.5f;
    float bx = x1 * 0.5f + x2 * 0.5f, by = y1 * 0.5f + y2 * 0.5f;
    float cx = x2 * 0.5f + x3 * 0.5f, cy = y2 * 0.5f + y3 * 0.5f;
    float dx = ax * 0.5f + bx * 0.5f, dy = ay * 0.5f + by * 0.5f;
    float ex = bx * 0.5f + cx * 0.5f, ey = by * 0.5f + cy * 0.5f;
    float mx = dx * 0.5f + ex * 0.5f, my = dy * 0.5f + ey * 0.5f;
    pdf_flatten_curve(st, x0, y0, ax, ay, dx, dy, mx, my, depth + 1);
    pdf_flatten_curve(st, mx, my, ex, ey, cx, cy, x3, y3, depth + 1);
}

static void pdf_path_curve_to(pdf_render_state *st, float x1, float y1, float x2, float y2,
                              float x3, float y3, int first_is_current) {
    if (!st->path.has_current)
        return;
    if (first_is_current) {
        x1 = st->path.current_x;
        y1 = st->path.current_y;
    } else {
        pdf_user_to_pixel(st, x1, y1, &x1, &y1);
    }
    pdf_user_to_pixel(st, x2, y2, &x2, &y2);
    pdf_user_to_pixel(st, x3, y3, &x3, &y3);
    if (!isfinite(x1) || !isfinite(y1) || !isfinite(x2) || !isfinite(y2) || !isfinite(x3) ||
        !isfinite(y3)) {
        st->path.overflow = 1;
        return;
    }
    pdf_flatten_curve(st, st->path.current_x, st->path.current_y, x1, y1, x2, y2, x3, y3, 0);
}

static void pdf_stroke_path(pdf_render_state *st) {
    if (!st || !st->img || st->path.seg_count <= 0)
        return;

    float px_per_pt = 96.0f / 72.0f;
    float half_w = st->gs.line_width_pts * px_per_pt * 0.5f;
    if (half_w < 0.5f)
        half_w = 0.5f;
    float half_w_sq = half_w * half_w;

    for (int i = 0; i < st->path.seg_count; i++) {
        float x0 = st->path.segs[i].x0, y0 = st->path.segs[i].y0;
        float x1 = st->path.segs[i].x1, y1 = st->path.segs[i].y1;

        if (!isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1))
            continue;
        float w = (float)st->img->width - 1, h = (float)st->img->height - 1;
        int min_x = (int)fminf(w, fmaxf(0, floorf(fminf(x0, x1) - half_w - 1)));
        int min_y = (int)fminf(h, fmaxf(0, floorf(fminf(y0, y1) - half_w - 1)));
        int max_x = (int)fminf(w, fmaxf(0, ceilf(fmaxf(x0, x1) + half_w + 1)));
        int max_y = (int)fminf(h, fmaxf(0, ceilf(fmaxf(y0, y1) + half_w + 1)));

        for (int y = min_y; y <= max_y; y++) {
            float py = (float)y + 0.5f;
            for (int x = min_x; x <= max_x; x++) {
                float px = (float)x + 0.5f;
                float d2 = pdf_point_to_segment_dist_sq(px, py, x0, y0, x1, y1);
                if (d2 <= half_w_sq) {
                    pdf_set_pixel(st, x, y, st->gs.stroke_r, st->gs.stroke_g, st->gs.stroke_b);
                }
            }
        }
    }
}

static int pdf_scanline_intersects(float x, float y, const pdf_seg *seg) {
    float y0 = seg->y0;
    float y1 = seg->y1;
    if ((y0 <= y && y1 > y) || (y1 <= y && y0 > y)) {
        /* Skip horizontal edges (approximately) to avoid double-counting. */
        if (fabsf(y0 - y1) <= 1e-6f) {
            return 0;
        }
        float t = (y - y0) / (y1 - y0);
        float ix = seg->x0 + t * (seg->x1 - seg->x0);
        return ix > x ? (y1 > y0 ? 1 : -1) : 0;
    }
    return 0;
}

static int pdf_rasterize_path(pdf_render_state *st, int even_odd, unsigned char *mask) {
    if (st->path.seg_count <= 0)
        return 1;

    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
    pdf_seg xformed[8192];
    int n = 0;
    float start_x = 0, start_y = 0;

    for (int i = 0; i < st->path.seg_count; i++) {
        pdf_seg edge = st->path.segs[i];
        if (!isfinite(edge.x0) || !isfinite(edge.y0) || !isfinite(edge.x1) || !isfinite(edge.y1))
            return 0;
        if (st->path.segs[i].starts_subpath) {
            start_x = edge.x0;
            start_y = edge.y0;
        }
        xformed[n++] = edge;
        min_x = fminf(min_x, fminf(edge.x0, edge.x1));
        max_x = fmaxf(max_x, fmaxf(edge.x0, edge.x1));
        min_y = fminf(min_y, fminf(edge.y0, edge.y1));
        max_y = fmaxf(max_y, fmaxf(edge.y0, edge.y1));
        if (i + 1 == st->path.seg_count || st->path.segs[i + 1].starts_subpath) {
            /* Fill closes each subpath without adding a closing stroke. */
            pdf_seg closing = {edge.x1, edge.y1, start_x, start_y, 0};
            xformed[n++] = closing;
        }
    }

    int x0 = (int)fminf((float)st->img->width, fmaxf(0, floorf(min_x)));
    int y0 = (int)fminf((float)st->img->height, fmaxf(0, floorf(min_y)));
    int x1 = (int)fminf((float)st->img->width, fmaxf(0, ceilf(max_x)));
    int y1 = (int)fminf((float)st->img->height, fmaxf(0, ceilf(max_y)));

    if (mask) {
        size_t area = (size_t)(x1 - x0) * (size_t)(y1 - y0);
        if (area > st->clip_budget->tests_left / (size_t)n)
            return 0;
        st->clip_budget->tests_left -= area * (size_t)n;
    }

    for (int y = y0; y < y1; y++) {
        float py = (float)y + 0.5f;
        for (int x = x0; x < x1; x++) {
            float px = (float)x + 0.5f;
            int crossings = 0;
            for (int i = 0; i < n; i++) {
                crossings += pdf_scanline_intersects(px, py, &xformed[i]);
            }
            if (even_odd ? crossings % 2 != 0 : crossings != 0) {
                if (mask) {
                    size_t pixel = (size_t)y * st->img->width + (size_t)x;
                    mask[pixel / 8] |= (unsigned char)(1u << (pixel % 8));
                } else {
                    pdf_set_pixel(st, x, y, st->gs.fill_r, st->gs.fill_g, st->gs.fill_b);
                }
            }
        }
    }
    return 1;
}

static void pdf_fill_path(pdf_render_state *st, int even_odd) {
    pdf_rasterize_path(st, even_odd, NULL);
}

static int pdf_apply_clip(pdf_render_state *st) {
    size_t bytes = ((size_t)st->img->width * st->img->height + 7) / 8;
    if (st->path.overflow || !st->clip_budget->calls_left ||
        bytes > 64u * 1024u * 1024u - st->clip_budget->bytes_used)
        return 0;
    st->clip_budget->calls_left--;
    pdf_clip_mask *clip = (pdf_clip_mask *)calloc(1, sizeof(*clip) + bytes);
    if (!clip)
        return 0;
    clip->references = 1;
    clip->bytes = bytes;
    if (!pdf_rasterize_path(st, st->pending_clip == 2, clip->bits)) {
        free(clip);
        return 0;
    }
    if (st->gs.clip) {
        for (size_t i = 0; i < bytes; i++)
            clip->bits[i] &= st->gs.clip->bits[i];
    }
    st->clip_budget->bytes_used += bytes;
    pdf_clip_release(st, st->gs.clip);
    st->gs.clip = clip;
    return 1;
}

static void pdf_path_end(pdf_render_state *st) {
    if (st->pending_clip && !pdf_apply_clip(st)) {
        set_err(st->error, st->error_capacity, "pdf: clipping path exceeds limits or is invalid");
        st->image_failed = 1;
    }
    st->pending_clip = 0;
    pdf_path_clear(&st->path);
}

static int pdf_is_nonwhite_pixel(const unsigned char *px) {
    return px[0] < 250 || px[1] < 250 || px[2] < 250 || px[3] < 250;
}

static void pdf_trim_whitespace(cupidimage_image *img) {
    if (!img || !img->rgba || img->width == 0 || img->height == 0) {
        return;
    }

    uint32_t w = img->width;
    uint32_t h = img->height;
    size_t *row_mass = (size_t *)calloc(h, sizeof(size_t));
    size_t *col_mass = (size_t *)calloc(w, sizeof(size_t));
    if (!row_mass || !col_mass) {
        free(row_mass);
        free(col_mass);
        return;
    }

    size_t total_mass = 0;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            const unsigned char *px = img->rgba + ((size_t)y * (size_t)w + (size_t)x) * 4u;
            if (!pdf_is_nonwhite_pixel(px)) {
                continue;
            }
            row_mass[y]++;
            col_mass[x]++;
            total_mass++;
        }
    }

    if (total_mass == 0) {
        free(row_mass);
        free(col_mass);
        return;
    }

    size_t cut_mass_y = total_mass / 300u; /* Trim only sparse edge noise. */
    size_t cut_mass_x = total_mass / 300u;

    uint32_t top = 0;
    size_t acc = 0;
    while (top + 1 < h && acc + row_mass[top] <= cut_mass_y) {
        acc += row_mass[top];
        top++;
    }

    uint32_t bottom = h - 1u;
    acc = 0;
    while (bottom > top && acc + row_mass[bottom] <= cut_mass_y) {
        acc += row_mass[bottom];
        bottom--;
    }

    uint32_t left = 0;
    acc = 0;
    while (left + 1 < w && acc + col_mass[left] <= cut_mass_x) {
        acc += col_mass[left];
        left++;
    }

    uint32_t right = w - 1u;
    acc = 0;
    while (right > left && acc + col_mass[right] <= cut_mass_x) {
        acc += col_mass[right];
        right--;
    }

    /* Keep a page-like framing: don't trim more than ~1/6 from any side. */
    uint32_t max_trim_y = h / 6u;
    uint32_t max_trim_x = w / 6u;
    if (top > max_trim_y) {
        top = max_trim_y;
    }
    if (left > max_trim_x) {
        left = max_trim_x;
    }
    uint32_t trimmed_bottom = (h - 1u) - bottom;
    if (trimmed_bottom > max_trim_y) {
        bottom = (h - 1u) - max_trim_y;
    }
    uint32_t trimmed_right = (w - 1u) - right;
    if (trimmed_right > max_trim_x) {
        right = (w - 1u) - max_trim_x;
    }

    free(row_mass);
    free(col_mass);

    if (right <= left || bottom <= top) {
        return;
    }

    uint32_t pad = 16u;
    if (left > pad)
        left -= pad;
    else
        left = 0;
    if (top > pad)
        top -= pad;
    else
        top = 0;
    if (right + pad < w)
        right += pad;
    else
        right = w - 1u;
    if (bottom + pad < h)
        bottom += pad;
    else
        bottom = h - 1u;

    uint32_t new_w = right - left + 1u;
    uint32_t new_h = bottom - top + 1u;
    if (new_w >= w || new_h >= h) {
        return;
    }

    unsigned char *cropped = (unsigned char *)malloc((size_t)new_w * (size_t)new_h * 4u);
    if (!cropped) {
        return;
    }

    for (uint32_t y = 0; y < new_h; y++) {
        const unsigned char *src = img->rgba + ((size_t)(top + y) * (size_t)w + (size_t)left) * 4u;
        unsigned char *dst = cropped + (size_t)y * (size_t)new_w * 4u;
        memcpy(dst, src, (size_t)new_w * 4u);
    }

    free(img->rgba);
    img->rgba = cropped;
    img->width = new_w;
    img->height = new_h;
}

static void pdf_draw_glyph(pdf_render_state *st, unsigned char c) {
    if (!st || !st->img || !st->img->rgba) {
        return;
    }

    const uint8_t *glyph = pdf_font8x8_basic[(c < 128u) ? c : (unsigned char)'?'];

    float px_per_pt = 96.0f / 72.0f;
    float font_px_f = st->font_size_pts * px_per_pt;
    if (font_px_f < 6.0f) {
        font_px_f = 6.0f;
    }

    int font_px = (int)(font_px_f + 0.5f);
    int scale_x = (int)((font_px_f * 0.6f) / 8.0f + 0.5f);
    int scale_y = (int)(font_px_f / 8.0f + 0.5f);
    if (scale_x < 1)
        scale_x = 1;
    if (scale_y < 1)
        scale_y = 1;

    double base_x = trunc(((double)st->text_x_pts - st->page_x0_pts) * px_per_pt + 0.5);
    double top_y = trunc(((double)st->page_top_pts - st->text_y_pts) * px_per_pt + 0.5) - font_px;
    if (!isfinite(base_x) || !isfinite(top_y))
        return;
    const float *m = st->gs.ctm;
    double det = (double)m[0] * m[3] - (double)m[1] * m[2];
    if (!isfinite(det) || det == 0)
        return;
    double min_x = st->img->width, min_y = st->img->height, max_x = 0, max_y = 0;
    for (int corner = 0; corner < 4; corner++) {
        double ux = st->page_x0_pts + (base_x + ((corner & 1) ? scale_x * 8 : 0)) / px_per_pt;
        double uy = st->page_top_pts - (top_y + ((corner & 2) ? scale_y * 8 : 0)) / px_per_pt;
        double x = (m[0] * ux + m[2] * uy + m[4] - st->page_x0_pts) * px_per_pt;
        double y = (st->page_top_pts - (m[1] * ux + m[3] * uy + m[5])) * px_per_pt;
        if (!isfinite(x) || !isfinite(y))
            return;
        min_x = fmin(min_x, x);
        max_x = fmax(max_x, x);
        min_y = fmin(min_y, y);
        max_y = fmax(max_y, y);
    }
    int x0 = (int)fmax(0, fmin(st->img->width, floor(min_x)));
    int x1 = (int)fmax(0, fmin(st->img->width, ceil(max_x)));
    int y0 = (int)fmax(0, fmin(st->img->height, floor(min_y)));
    int y1 = (int)fmax(0, fmin(st->img->height, ceil(max_y)));
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            double tx = st->page_x0_pts + (x + 0.5) / px_per_pt - m[4];
            double ty = st->page_top_pts - (y + 0.5) / px_per_pt - m[5];
            double ux = (m[3] * tx - m[2] * ty) / det;
            double uy = (m[0] * ty - m[1] * tx) / det;
            double gx = ((ux - st->page_x0_pts) * px_per_pt - base_x) / scale_x;
            double gy = ((st->page_top_pts - uy) * px_per_pt - top_y) / scale_y;
            if (gx >= 0 && gx < 8 && gy >= 0 && gy < 8 && (glyph[(int)gy] & (1u << (int)gx)))
                pdf_set_pixel(st, x, y, st->gs.fill_r, st->gs.fill_g, st->gs.fill_b);
        }
    }
}

static float pdf_char_advance_em(unsigned char c) {
    if (c == ' ')
        return 0.278f;
    if (c >= '0' && c <= '9')
        return 0.556f;
    if (c >= 'A' && c <= 'Z') {
        if (c == 'M' || c == 'W')
            return 0.889f;
        if (c == 'I')
            return 0.333f;
        return 0.667f;
    }
    if (c >= 'a' && c <= 'z') {
        if (c == 'm')
            return 0.833f;
        if (c == 'w')
            return 0.722f;
        if (c == 'i' || c == 'l')
            return 0.278f;
        return 0.5f;
    }
    if (c == '.' || c == ',' || c == ':' || c == ';' || c == '!' || c == '\'')
        return 0.278f;
    if (c == '(' || c == ')' || c == '[' || c == ']')
        return 0.333f;
    if (c == '-')
        return 0.333f;
    if (c == '/')
        return 0.278f;
    if (c == '?')
        return 0.556f;
    return 0.5f;
}

static int pdf_font_lookup_codepoint(const pdf_font_map *font, uint32_t src_code, uint8_t src_len,
                                     uint32_t *dst_codepoint) {
    if (!font || !dst_codepoint || src_len == 0) {
        return 0;
    }
    for (int i = 0; i < font->count; i++) {
        if (font->entries[i].src_code == src_code && font->entries[i].src_len == src_len) {
            *dst_codepoint = font->entries[i].dst_codepoint;
            return 1;
        }
    }
    return 0;
}

static uint32_t pdf_decode_next_codepoint(const pdf_render_state *st, const unsigned char *bytes,
                                          size_t len, size_t *pos) {
    if (!bytes || !pos || *pos >= len) {
        return 0;
    }

    const pdf_font_map *font = st ? st->current_font : NULL;
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

static void pdf_render_text_string(pdf_render_state *st, const char *s, size_t len) {
    if (!st || !s || !st->in_text) {
        return;
    }

    const unsigned char *bytes = (const unsigned char *)s;
    size_t pos = 0;
    while (pos < len) {
        uint32_t cp = pdf_decode_next_codepoint(st, bytes, len, &pos);
        if (cp == '\r' || cp == '\n') {
            float lead = (st->leading_pts > 0.0f) ? st->leading_pts : st->font_size_pts * 1.2f;
            st->line_y_pts -= lead;
            st->text_x_pts = st->line_x_pts;
            st->text_y_pts = st->line_y_pts;
            continue;
        }
        if (cp < 32u) {
            continue;
        }
        unsigned char draw = pdf_codepoint_to_ascii(cp);
        if (draw >= 32u) {
            pdf_draw_glyph(st, draw);
        }
        if (draw == ' ' || cp == 0x00A0u) {
            st->text_x_pts += st->font_size_pts * pdf_char_advance_em(' ');
        } else if (draw == '?' && cp >= 128u) {
            st->text_x_pts += st->font_size_pts * 0.5f;
        } else {
            st->text_x_pts += st->font_size_pts * pdf_char_advance_em(draw);
        }
    }
}

static void pdf_render_tj_array(pdf_render_state *st, const char *arr, size_t len) {
    size_t pos = 0;
    const unsigned char *data = (const unsigned char *)arr;

    while (pos < len) {
        pdf_skip_ws_and_comments(data, len, &pos);
        if (pos >= len) {
            break;
        }

        if (data[pos] == '(') {
            char *txt = NULL;
            size_t txt_len = 0;
            if (pdf_parse_literal_string(data, len, &pos, &txt, &txt_len)) {
                pdf_render_text_string(st, txt, txt_len);
                free(txt);
                continue;
            }
            break;
        }

        if (data[pos] == '<' && !(pos + 1 < len && data[pos + 1] == '<')) {
            char *txt = NULL;
            size_t txt_len = 0;
            if (pdf_parse_hex_string(data, len, &pos, &txt, &txt_len)) {
                pdf_render_text_string(st, txt, txt_len);
                free(txt);
                continue;
            }
            break;
        }

        if (data[pos] == '+' || data[pos] == '-' || data[pos] == '.' ||
            (data[pos] >= '0' && data[pos] <= '9')) {
            double num = 0.0;
            if (pdf_parse_number_token(data, len, &pos, &num)) {
                float kern_advance_pts = (float)(-num / 1000.0);
                st->text_x_pts += kern_advance_pts * st->font_size_pts;
                continue;
            }
        }

        pos++;
    }
}

typedef enum {
    PDF_OPERAND_NUM,
    PDF_OPERAND_NAME,
    PDF_OPERAND_STRING,
    PDF_OPERAND_ARRAY
} pdf_operand_type;

typedef struct {
    pdf_operand_type type;
    double num;
    char *str;
    size_t len;
} pdf_operand;

static void pdf_clear_operands(pdf_operand *ops, int *count) {
    if (!ops || !count) {
        return;
    }
    for (int i = 0; i < *count; i++) {
        free(ops[i].str);
        ops[i].str = NULL;
        ops[i].len = 0;
        ops[i].num = 0.0;
    }
    *count = 0;
}

static int pdf_expect_num(const pdf_operand *op) {
    return op && op->type == PDF_OPERAND_NUM;
}

static int pdf_expect_str(const pdf_operand *op) {
    return op && (op->type == PDF_OPERAND_STRING || op->type == PDF_OPERAND_ARRAY ||
                  op->type == PDF_OPERAND_NAME);
}

static int pdf_color_space(const pdf_render_state *st, const char *name, unsigned depth);

static int pdf_color_space_value(const pdf_render_state *st, const pdf_object_view *view,
                                 size_t pos, unsigned depth) {
    if (depth >= 16)
        return 0;
    pdf_skip_ws_and_comments(view->data, view->len, &pos);
    int object;
    if (pdf_page_reference(view, pos, &object)) {
        pdf_object_view indirect = {0};
        if (!get_object_content(st->document, st->document_size, object, &indirect))
            return 0;
        int space = pdf_color_space_value(st, &indirect, 0, depth + 1);
        pdf_object_view_release(&indirect);
        return space;
    }
    if (pos >= view->len || view->data[pos++] != '/')
        return 0;
    char name[128];
    size_t length = 0;
    while (pos < view->len && !pdf_is_ws(view->data[pos]) && !pdf_is_delim(view->data[pos])) {
        if (length + 1 == sizeof(name))
            return 0;
        name[length++] = (char)view->data[pos++];
    }
    name[length] = '\0';
    return pdf_color_space(st, name, depth + 1);
}

static int pdf_color_space(const pdf_render_state *st, const char *name, unsigned depth) {
    if (strcmp(name, "DeviceGray") == 0)
        return 1;
    if (strcmp(name, "DeviceRGB") == 0)
        return 3;
    if (strcmp(name, "DeviceCMYK") == 0)
        return 4;
    if (depth >= 16)
        return 0;
    int object = st->page_object;
    for (unsigned i = 0; object > 0 && i < 64; i++) {
        pdf_object_view node = {0}, resources = {0}, spaces = {0};
        if (!get_object_content(st->document, st->document_size, object, &node))
            return 0;
        size_t pos;
        int parent = 0, space = 0;
        if (pdf_page_value(&node, "Resources", &pos)) {
            if (pdf_image_dictionary(st, &node, "Resources", &resources) &&
                pdf_image_dictionary(st, &resources, "ColorSpace", &spaces) &&
                pdf_page_value(&spaces, name, &pos)) {
                space = pdf_color_space_value(st, &spaces, pos, depth + 1);
            }
            pdf_object_view_release(&spaces);
            pdf_object_view_release(&resources);
            pdf_object_view_release(&node);
            return space;
        }
        if (pdf_page_value(&node, "Parent", &pos))
            pdf_page_reference(&node, pos, &parent);
        pdf_object_view_release(&node);
        object = parent;
    }
    return 0;
}

static int pdf_set_device_color(pdf_render_state *st, int stroke, int space, const pdf_operand *ops,
                                int count) {
    if ((space != 1 && space != 3 && space != 4) || count != space)
        return 0;
    for (int i = 0; i < count; i++)
        if (!pdf_expect_num(&ops[i]))
            return 0;
    uint8_t r, g, b;
    if (space == 1) {
        r = g = b = pdf_clamp_u8(ops[0].num);
    } else if (space == 3) {
        r = pdf_clamp_u8(ops[0].num);
        g = pdf_clamp_u8(ops[1].num);
        b = pdf_clamp_u8(ops[2].num);
    } else {
        double c = fmin(1, fmax(0, ops[0].num));
        double m = fmin(1, fmax(0, ops[1].num));
        double y = fmin(1, fmax(0, ops[2].num));
        double k = fmin(1, fmax(0, ops[3].num));
        r = pdf_clamp_u8(1 - fmin(1, c + k));
        g = pdf_clamp_u8(1 - fmin(1, m + k));
        b = pdf_clamp_u8(1 - fmin(1, y + k));
    }
    if (stroke) {
        st->gs.stroke_r = r;
        st->gs.stroke_g = g;
        st->gs.stroke_b = b;
        st->gs.stroke_space = space;
    } else {
        st->gs.fill_r = r;
        st->gs.fill_g = g;
        st->gs.fill_b = b;
        st->gs.fill_space = space;
    }
    return 1;
}

static void pdf_execute_operator(pdf_render_state *st, const char *op, pdf_operand *ops,
                                 int *count) {
    int n = *count;

    if (strcmp(op, "Do") == 0) {
        if (n != 1 || ops[0].type != PDF_OPERAND_NAME || !pdf_draw_image(st, ops[0].str)) {
            st->image_failed = 1;
            if (n != 1 || ops[0].type != PDF_OPERAND_NAME)
                set_err(st->error, st->error_capacity, "pdf: invalid image draw operands");
        }
    } else if (strcmp(op, "BT") == 0) {
        st->in_text = 1;
        st->line_x_pts = 0.0f;
        st->line_y_pts = 0.0f;
        st->text_x_pts = 0.0f;
        st->text_y_pts = 0.0f;
    } else if (strcmp(op, "ET") == 0) {
        st->in_text = 0;
    } else if (strcmp(op, "Tf") == 0) {
        if (n >= 2 && pdf_expect_str(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            st->current_font = NULL;
            if (st->font_table && ops[n - 2].str) {
                st->current_font = pdf_font_table_find(st->font_table, ops[n - 2].str);
            }
            float fs = (float)ops[n - 1].num;
            if (fs > 0.1f && fs < 500.0f) {
                st->font_size_pts = fs;
            }
        }
    } else if (strcmp(op, "Td") == 0) {
        if (n >= 2 && pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            st->line_x_pts += (float)ops[n - 2].num;
            st->line_y_pts += (float)ops[n - 1].num;
            st->text_x_pts = st->line_x_pts;
            st->text_y_pts = st->line_y_pts;
        }
    } else if (strcmp(op, "TD") == 0) {
        if (n >= 2 && pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            float ty = (float)ops[n - 1].num;
            st->leading_pts = -ty;
            st->line_x_pts += (float)ops[n - 2].num;
            st->line_y_pts += ty;
            st->text_x_pts = st->line_x_pts;
            st->text_y_pts = st->line_y_pts;
        }
    } else if (strcmp(op, "Tm") == 0) {
        if (n >= 6 && pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            st->line_x_pts = (float)ops[n - 2].num;
            st->line_y_pts = (float)ops[n - 1].num;
            st->text_x_pts = st->line_x_pts;
            st->text_y_pts = st->line_y_pts;
        }
    } else if (strcmp(op, "T*") == 0) {
        float lead = (st->leading_pts > 0.0f) ? st->leading_pts : st->font_size_pts * 1.2f;
        st->line_y_pts -= lead;
        st->text_x_pts = st->line_x_pts;
        st->text_y_pts = st->line_y_pts;
    } else if (strcmp(op, "TL") == 0) {
        if (n >= 1 && pdf_expect_num(&ops[n - 1])) {
            st->leading_pts = (float)ops[n - 1].num;
        }
    } else if (strcmp(op, "cs") == 0 || strcmp(op, "CS") == 0) {
        int space =
            n == 1 && ops[0].type == PDF_OPERAND_NAME ? pdf_color_space(st, ops[0].str, 0) : 0;
        if (!space) {
            set_err(st->error, st->error_capacity, "pdf: invalid or unsupported color space");
            st->image_failed = 1;
        } else if (op[0] == 'C') {
            st->gs.stroke_space = space;
            st->gs.stroke_r = st->gs.stroke_g = st->gs.stroke_b = 0;
        } else {
            st->gs.fill_space = space;
            st->gs.fill_r = st->gs.fill_g = st->gs.fill_b = 0;
        }
    } else if (strcmp(op, "g") == 0 || strcmp(op, "G") == 0 || strcmp(op, "rg") == 0 ||
               strcmp(op, "RG") == 0 || strcmp(op, "k") == 0 || strcmp(op, "K") == 0 ||
               strcmp(op, "sc") == 0 || strcmp(op, "SC") == 0 || strcmp(op, "scn") == 0 ||
               strcmp(op, "SCN") == 0) {
        int stroke = op[0] >= 'A' && op[0] <= 'Z';
        int space = op[0] == 'g' || op[0] == 'G'   ? 1
                    : op[0] == 'r' || op[0] == 'R' ? 3
                    : op[0] == 'k' || op[0] == 'K' ? 4
                    : stroke                       ? st->gs.stroke_space
                                                   : st->gs.fill_space;
        if (!pdf_set_device_color(st, stroke, space, ops, n)) {
            set_err(st->error, st->error_capacity, "pdf: invalid color operands");
            st->image_failed = 1;
        }
    } else if (strcmp(op, "w") == 0) {
        if (n >= 1 && pdf_expect_num(&ops[n - 1])) {
            float w = (float)ops[n - 1].num;
            if (w > 0.0f && w < 1000.0f) {
                st->gs.line_width_pts = w;
            }
        }
    } else if (strcmp(op, "cm") == 0) {
        if (n >= 6 && pdf_expect_num(&ops[n - 6]) && pdf_expect_num(&ops[n - 5]) &&
            pdf_expect_num(&ops[n - 4]) && pdf_expect_num(&ops[n - 3]) &&
            pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            float m[6];
            m[0] = (float)ops[n - 6].num;
            m[1] = (float)ops[n - 5].num;
            m[2] = (float)ops[n - 4].num;
            m[3] = (float)ops[n - 3].num;
            m[4] = (float)ops[n - 2].num;
            m[5] = (float)ops[n - 1].num;
            pdf_matrix_concat(st->gs.ctm, m);
        }
    } else if (strcmp(op, "q") == 0) {
        if (st->gs_stack_len < (int)(sizeof(st->gs_stack) / sizeof(st->gs_stack[0]))) {
            pdf_clip_retain(st->gs.clip);
            st->gs_stack[st->gs_stack_len++] = st->gs;
        } else {
            set_err(st->error, st->error_capacity, "pdf: graphics state stack limit exceeded");
            st->image_failed = 1;
        }
    } else if (strcmp(op, "Q") == 0) {
        if (st->gs_stack_len > 0) {
            pdf_clip_release(st, st->gs.clip);
            st->gs = st->gs_stack[--st->gs_stack_len];
        }
    } else if (strcmp(op, "m") == 0) {
        if (n >= 2 && pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            pdf_path_move_user(st, (float)ops[n - 2].num, (float)ops[n - 1].num);
        }
    } else if (strcmp(op, "l") == 0) {
        if (n >= 2 && pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            pdf_path_line_user(st, (float)ops[n - 2].num, (float)ops[n - 1].num);
        }
    } else if (strcmp(op, "c") == 0) {
        if (n >= 6 && pdf_expect_num(&ops[n - 6]) && pdf_expect_num(&ops[n - 5]) &&
            pdf_expect_num(&ops[n - 4]) && pdf_expect_num(&ops[n - 3]) &&
            pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            pdf_path_curve_to(st, (float)ops[n - 6].num, (float)ops[n - 5].num,
                              (float)ops[n - 4].num, (float)ops[n - 3].num, (float)ops[n - 2].num,
                              (float)ops[n - 1].num, 0);
        }
    } else if (strcmp(op, "v") == 0 || strcmp(op, "y") == 0) {
        if (n >= 4 && pdf_expect_num(&ops[n - 4]) && pdf_expect_num(&ops[n - 3]) &&
            pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            float x = (float)ops[n - 2].num, y = (float)ops[n - 1].num;
            float cx = (float)ops[n - 4].num, cy = (float)ops[n - 3].num;
            if (op[0] == 'v') {
                pdf_path_curve_to(st, 0, 0, cx, cy, x, y, 1);
            } else {
                pdf_path_curve_to(st, cx, cy, x, y, x, y, 0);
            }
        }
    } else if (strcmp(op, "h") == 0) {
        pdf_path_close(&st->path);
    } else if (strcmp(op, "re") == 0) {
        if (n >= 4 && pdf_expect_num(&ops[n - 4]) && pdf_expect_num(&ops[n - 3]) &&
            pdf_expect_num(&ops[n - 2]) && pdf_expect_num(&ops[n - 1])) {
            float x = (float)ops[n - 4].num;
            float y = (float)ops[n - 3].num;
            float w = (float)ops[n - 2].num;
            float h = (float)ops[n - 1].num;
            pdf_path_move_user(st, x, y);
            pdf_path_line_user(st, x + w, y);
            pdf_path_line_user(st, x + w, y + h);
            pdf_path_line_user(st, x, y + h);
            pdf_path_close(&st->path);
        }
    } else if (strcmp(op, "n") == 0) {
        pdf_path_end(st);
    } else if (strcmp(op, "S") == 0) {
        pdf_stroke_path(st);
        pdf_path_end(st);
    } else if (strcmp(op, "s") == 0) {
        pdf_path_close(&st->path);
        pdf_stroke_path(st);
        pdf_path_end(st);
    } else if (strcmp(op, "f") == 0 || strcmp(op, "F") == 0 || strcmp(op, "f*") == 0) {
        pdf_fill_path(st, strcmp(op, "f*") == 0);
        pdf_path_end(st);
    } else if (strcmp(op, "B") == 0 || strcmp(op, "B*") == 0) {
        pdf_fill_path(st, strcmp(op, "B*") == 0);
        pdf_stroke_path(st);
        pdf_path_end(st);
    } else if (strcmp(op, "b") == 0 || strcmp(op, "b*") == 0) {
        pdf_path_close(&st->path);
        pdf_fill_path(st, strcmp(op, "b*") == 0);
        pdf_stroke_path(st);
        pdf_path_end(st);
    } else if (strcmp(op, "W") == 0 || strcmp(op, "W*") == 0) {
        st->pending_clip = op[1] == '*' ? 2 : 1;
    } else if (strcmp(op, "Tj") == 0) {
        if (n >= 1 && ops[n - 1].type == PDF_OPERAND_STRING) {
            pdf_render_text_string(st, ops[n - 1].str, ops[n - 1].len);
        }
    } else if (strcmp(op, "TJ") == 0) {
        if (n >= 1 && ops[n - 1].type == PDF_OPERAND_ARRAY) {
            pdf_render_tj_array(st, ops[n - 1].str, ops[n - 1].len);
        }
    } else if (strcmp(op, "'") == 0) {
        if (n >= 1 && ops[n - 1].type == PDF_OPERAND_STRING) {
            float lead = (st->leading_pts > 0.0f) ? st->leading_pts : st->font_size_pts * 1.2f;
            st->line_y_pts -= lead;
            st->text_x_pts = st->line_x_pts;
            st->text_y_pts = st->line_y_pts;
            pdf_render_text_string(st, ops[n - 1].str, ops[n - 1].len);
        }
    } else if (strcmp(op, "\"") == 0) {
        if (n >= 3 && ops[n - 1].type == PDF_OPERAND_STRING) {
            float lead = (st->leading_pts > 0.0f) ? st->leading_pts : st->font_size_pts * 1.2f;
            st->line_y_pts -= lead;
            st->text_x_pts = st->line_x_pts;
            st->text_y_pts = st->line_y_pts;
            pdf_render_text_string(st, ops[n - 1].str, ops[n - 1].len);
        }
    }

    pdf_clear_operands(ops, count);
}

static int pdf_render_stream(pdf_render_state *st, const unsigned char *stream, size_t stream_len) {
    char *err = st->error;
    size_t errcap = st->error_capacity;
    pdf_operand ops[64];
    int op_count = 0;
    memset(ops, 0, sizeof(ops));

    size_t pos = 0;
    while (pos < stream_len) {
        pdf_skip_ws_and_comments(stream, stream_len, &pos);
        if (pos >= stream_len) {
            break;
        }

        unsigned char c = stream[pos];

        if (c == '(') {
            char *txt = NULL;
            size_t txt_len = 0;
            if (!pdf_parse_literal_string(stream, stream_len, &pos, &txt, &txt_len)) {
                set_err(err, errcap, "pdf: malformed content string");
                pdf_clear_operands(ops, &op_count);
                return 0;
            }
            if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                ops[op_count].type = PDF_OPERAND_STRING;
                ops[op_count].str = txt;
                ops[op_count].len = txt_len;
                op_count++;
            } else {
                free(txt);
            }
            continue;
        }

        if (c == '<' && !(pos + 1 < stream_len && stream[pos + 1] == '<')) {
            char *txt = NULL;
            size_t txt_len = 0;
            if (!pdf_parse_hex_string(stream, stream_len, &pos, &txt, &txt_len)) {
                set_err(err, errcap, "pdf: malformed hex string");
                pdf_clear_operands(ops, &op_count);
                return 0;
            }
            if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                ops[op_count].type = PDF_OPERAND_STRING;
                ops[op_count].str = txt;
                ops[op_count].len = txt_len;
                op_count++;
            } else {
                free(txt);
            }
            continue;
        }

        if (c == '[') {
            char *arr = NULL;
            size_t arr_len = 0;
            if (!pdf_parse_array_token(stream, stream_len, &pos, &arr, &arr_len)) {
                set_err(err, errcap, "pdf: malformed array token");
                pdf_clear_operands(ops, &op_count);
                return 0;
            }
            if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                ops[op_count].type = PDF_OPERAND_ARRAY;
                ops[op_count].str = arr;
                ops[op_count].len = arr_len;
                op_count++;
            } else {
                free(arr);
            }
            continue;
        }

        if (c == '/') {
            char *name = NULL;
            size_t name_len = 0;
            if (!pdf_parse_name_token(stream, stream_len, &pos, &name, &name_len)) {
                set_err(err, errcap, "pdf: malformed name token");
                pdf_clear_operands(ops, &op_count);
                return 0;
            }
            if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                ops[op_count].type = PDF_OPERAND_NAME;
                ops[op_count].str = name;
                ops[op_count].len = name_len;
                op_count++;
            } else {
                free(name);
            }
            continue;
        }

        if (c == '+' || c == '-' || c == '.' || (c >= '0' && c <= '9')) {
            double num = 0.0;
            if (pdf_parse_number_token(stream, stream_len, &pos, &num)) {
                if (op_count < (int)(sizeof(ops) / sizeof(ops[0]))) {
                    ops[op_count].type = PDF_OPERAND_NUM;
                    ops[op_count].num = num;
                    ops[op_count].str = NULL;
                    ops[op_count].len = 0;
                    op_count++;
                }
                continue;
            }
        }

        size_t start = pos;
        while (pos < stream_len && !pdf_is_ws(stream[pos]) && !pdf_is_delim(stream[pos])) {
            pos++;
        }
        if (pos > start) {
            size_t tok_len = pos - start;
            char opbuf[16];
            if (tok_len >= sizeof(opbuf)) {
                tok_len = sizeof(opbuf) - 1;
            }
            memcpy(opbuf, &stream[start], tok_len);
            opbuf[tok_len] = '\0';
            pdf_execute_operator(st, opbuf, ops, &op_count);
            if (st->image_failed)
                return 0;
            continue;
        }

        pos++;
    }

    pdf_clear_operands(ops, &op_count);
    return 1;
}

static int pdf_draw_form(pdf_render_state *st, int object, const pdf_object_view *form) {
    size_t pos, length;
    int type = 1, ok = 0;
    const unsigned char *stream;
    unsigned char *inflated = NULL;
    pdf_render_state *child = NULL;
    pdf_font_table fonts;
    pdf_font_table_init(&fonts);
    if (st->form_depth >= 16 || !st->form_budget->calls_left)
        goto done;
    st->form_budget->calls_left--;
    for (int i = 0; i < st->form_depth; i++) {
        if (st->form_objects[i] == object)
            goto done;
    }
    if ((pdf_page_value(form, "FormType", &pos) &&
         (!pdf_image_integer(st, form, "FormType", &type) || type != 1)) ||
        pdf_page_value(form, "Group", &pos) || pdf_page_value(form, "Ref", &pos))
        goto done;
    child = (pdf_render_state *)malloc(sizeof(*child));
    if (!child)
        goto done;
    *child = *st;
    pdf_clip_retain(child->gs.clip);
    child->gs_stack_len = 0;
    child->pending_clip = 0;
    child->image_failed = 0;
    pdf_path_clear(&child->path);
    pdf_form_clip *clip = &child->form_clips[st->form_depth];
    if (!pdf_page_value(form, "BBox", &pos) ||
        !pdf_page_rectangle(st->document, st->document_size, form, pos, clip->box))
        goto done;
    if (pdf_page_value(form, "Matrix", &pos)) {
        pdf_object_view indirect = {0};
        const pdf_object_view *matrix = form;
        int matrix_obj;
        if (pos < form->len && form->data[pos] != '[') {
            if (!pdf_page_reference(form, pos, &matrix_obj) ||
                !get_object_content(st->document, st->document_size, matrix_obj, &indirect))
                goto done;
            matrix = &indirect;
            pos = 0;
            pdf_skip_ws_and_comments(matrix->data, matrix->len, &pos);
        }
        float m[6];
        int valid = pos < matrix->len && matrix->data[pos++] == '[';
        for (int i = 0; valid && i < 6; i++) {
            double number;
            pdf_skip_ws_and_comments(matrix->data, matrix->len, &pos);
            valid =
                parse_pdf_number(matrix->data, matrix->len, &pos, &number) && fabs(number) <= 1e30;
            if (valid)
                m[i] = (float)number;
        }
        pdf_skip_ws_and_comments(matrix->data, matrix->len, &pos);
        valid = valid && pos < matrix->len && matrix->data[pos] == ']';
        pdf_object_view_release(&indirect);
        if (!valid)
            goto done;
        pdf_matrix_concat(child->gs.ctm, m);
    }
    const float *m = child->gs.ctm;
    for (int i = 0; i < 6; i++)
        if (!isfinite(m[i]))
            goto done;
    double det = (double)m[0] * m[3] - (double)m[1] * m[2];
    if (det == 0) {
        ok = 1;
        goto done;
    }
    double *inv = clip->inverse;
    inv[0] = m[3] / det;
    inv[1] = -m[1] / det;
    inv[2] = -m[2] / det;
    inv[3] = m[0] / det;
    inv[4] = -(inv[0] * m[4] + inv[2] * m[5]);
    inv[5] = -(inv[1] * m[4] + inv[3] * m[5]);
    child->form_objects[child->form_depth++] = object;
    if (pdf_page_value(form, "Resources", &pos)) {
        pdf_object_view resources = {0};
        int valid = pdf_image_dictionary(st, form, "Resources", &resources);
        pdf_object_view_release(&resources);
        if (!valid)
            goto done;
        child->page_object = object;
        if (!pdf_collect_page_fonts(st->document, st->document_size, object, &fonts, st->error,
                                    st->error_capacity)) {
            pdf_font_table_free(&fonts);
            pdf_font_table_init(&fonts);
        }
        child->font_table = &fonts;
    }
    int flate = pdf_image_filter_is(form, "FlateDecode");
    if ((pdf_page_value(form, "Filter", &pos) && !flate) ||
        pdf_page_value(form, "DecodeParms", &pos) || !pdf_image_stream(st, form, &stream, &length))
        goto done;
    if (flate) {
        if (!pdf_inflate_auto(stream, length, &inflated, &length))
            goto done;
        stream = inflated;
    }
    if (length > st->form_budget->bytes_left)
        goto done;
    st->form_budget->bytes_left -= length;
    ok = pdf_render_stream(child, stream, length);
done:
    pdf_font_table_free(&fonts);
    free(inflated);
    if (child)
        pdf_render_state_release(child);
    free(child);
    if (!ok)
        set_err(st->error, st->error_capacity,
                "pdf: invalid, recursive, or unsupported Form XObject");
    return ok;
}

static int pdf_render_content_stream(const unsigned char *stream, size_t stream_len,
                                     const unsigned char *document, size_t document_size,
                                     int page_object, const pdf_font_table *font_table,
                                     cupidimage_image *img, const pdf_page_geometry *geometry,
                                     char *err, size_t errcap) {
    pdf_render_state st = {0};
    pdf_clip_budget clip_budget = {0, 256u * 1024u * 1024u, 4096};
    st.clip_budget = &clip_budget;
    pdf_form_budget form_budget = {64u * 1024u * 1024u, 4096};
    st.form_budget = &form_budget;
    st.img = img;
    st.document = document;
    st.document_size = document_size;
    st.page_object = page_object;
    st.image_failed = 0;
    st.error = err;
    st.error_capacity = errcap;
    st.page_x0_pts = (float)geometry->x0;
    st.page_top_pts = (float)geometry->y1;
    st.line_x_pts = 0.0f;
    st.line_y_pts = 0.0f;
    st.text_x_pts = 0.0f;
    st.text_y_pts = 0.0f;
    st.font_size_pts = 12.0f;
    st.leading_pts = 0.0f;
    pdf_matrix_identity(st.gs.ctm);
    st.gs.fill_r = 0;
    st.gs.fill_g = 0;
    st.gs.fill_b = 0;
    st.gs.stroke_r = 0;
    st.gs.stroke_g = 0;
    st.gs.stroke_b = 0;
    st.gs.line_width_pts = 1.0f;
    st.gs.fill_space = st.gs.stroke_space = 1;
    st.gs_stack_len = 0;
    pdf_path_clear(&st.path);
    st.in_text = 0;
    st.font_table = font_table;
    st.current_font = NULL;

    int ok = pdf_render_stream(&st, stream, stream_len);
    pdf_render_state_release(&st);
    return ok;
}

static int pdf_rotate_page(cupidimage_image *img, int rotation) {
    if (!rotation)
        return 1;
    uint32_t width = img->width, height = img->height;
    uint32_t new_width = rotation == 180 ? width : height;
    uint32_t new_height = rotation == 180 ? height : width;
    uint8_t *pixels = (uint8_t *)malloc((size_t)width * height * 4);
    if (!pixels)
        return 0;
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t dx, dy;
            if (rotation == 90) {
                dx = height - 1 - y;
                dy = x;
            } else if (rotation == 180) {
                dx = width - 1 - x;
                dy = height - 1 - y;
            } else {
                dx = y;
                dy = width - 1 - x;
            }
            memcpy(pixels + ((size_t)dy * new_width + dx) * 4,
                   img->rgba + ((size_t)y * width + x) * 4, 4);
        }
    }
    free(img->rgba);
    img->rgba = pixels;
    img->width = new_width;
    img->height = new_height;
    return 1;
}

static int cupidimage_load_pdf_internal(const unsigned char *data, size_t size,
                                        cupidimage_image *img, int page_index, char *err,
                                        size_t errcap) {
    if (!data || !img) {
        set_err(err, errcap, "pdf: null pointer");
        return 0;
    }
    memset(img, 0, sizeof(*img));
    if (!pdf_check_header(data, size)) {
        set_err(err, errcap, "pdf: invalid header");
        return 0;
    }

    size_t eof_pos = 0;
    if (!find_eof_marker(data, size, &eof_pos)) {
        set_err(err, errcap, "pdf: EOF marker not found");
        return 0;
    }

    size_t xref_offset = 0;
    if (!find_startxref(data, eof_pos, &xref_offset)) {
        set_err(err, errcap, "pdf: startxref not found");
        return 0;
    }

    pdf_xref xref = {0};
    if (!parse_xref_table(data, size, xref_offset, &xref, err, errcap)) {
        return 0;
    }

    if (!parse_trailer(data, size, xref_offset, &xref, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    int pages_obj = 0;
    if (!get_pages_obj(data, size, &xref, &pages_obj, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    int page_obj = 0;
    if (!get_page_obj_by_index(data, size, &xref, pages_obj, page_index, &page_obj, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    pdf_font_table fonts;
    pdf_font_table_init(&fonts);
    if (!pdf_collect_page_fonts(data, size, page_obj, &fonts, err, errcap)) {
        /* Keep rendering even if font metadata parsing fails. */
        pdf_font_table_free(&fonts);
        pdf_font_table_init(&fonts);
    }

    pdf_page_geometry geometry;
    if (!get_page_geometry(data, size, page_obj, &geometry, err, errcap)) {
        pdf_font_table_free(&fonts);
        free(xref.entries);
        return 0;
    }

    int width = geometry.width;
    int height = geometry.height;
    if (width <= 0 || height <= 0) {
        pdf_font_table_free(&fonts);
        free(xref.entries);
        set_err(err, errcap, "pdf: invalid page dimensions");
        return 0;
    }

    size_t pixel_count = (size_t)width * (size_t)height;
    if (height > 0 && pixel_count / (size_t)height != (size_t)width) {
        pdf_font_table_free(&fonts);
        free(xref.entries);
        set_err(err, errcap, "pdf: image too large");
        return 0;
    }
    if (pixel_count > SIZE_MAX / 4u) {
        pdf_font_table_free(&fonts);
        free(xref.entries);
        set_err(err, errcap, "pdf: image too large");
        return 0;
    }

    img->width = (uint32_t)width;
    img->height = (uint32_t)height;
    img->rgba = (unsigned char *)calloc(pixel_count * 4u, 1);
    if (!img->rgba) {
        pdf_font_table_free(&fonts);
        free(xref.entries);
        set_err(err, errcap, "pdf: out of memory");
        return 0;
    }

    memset(img->rgba, 255, pixel_count * 4u);

    unsigned char *stream_data = NULL;
    size_t stream_len = 0;
    int is_flate = 0;
    if (!get_page_content_stream(data, size, &xref, page_obj, &stream_data, &stream_len, &is_flate,
                                 err, errcap)) {
        pdf_font_table_free(&fonts);
        free(xref.entries);
        free(img->rgba);
        img->rgba = NULL;
        return 0;
    }

    if (stream_data && stream_len > 0) {
        const unsigned char *render_data = stream_data;
        size_t render_len = stream_len;
        unsigned char *inflated = NULL;

        if (is_flate) {
            size_t inflated_len = 0;
            if (!pdf_inflate_auto(stream_data, stream_len, &inflated, &inflated_len)) {
                free(stream_data);
                pdf_font_table_free(&fonts);
                free(xref.entries);
                free(img->rgba);
                img->rgba = NULL;
                set_err(err, errcap, "pdf: failed to decode Flate stream");
                return 0;
            }
            render_data = inflated;
            render_len = inflated_len;
        }

        if (!pdf_render_content_stream(render_data, render_len, data, size, page_obj, &fonts, img,
                                       &geometry, err, errcap)) {
            free(inflated);
            free(stream_data);
            pdf_font_table_free(&fonts);
            free(xref.entries);
            free(img->rgba);
            img->rgba = NULL;
            return 0;
        }

        free(inflated);
    }
    free(stream_data);

    if (!pdf_rotate_page(img, geometry.rotation)) {
        pdf_font_table_free(&fonts);
        free(xref.entries);
        cupidimage_free(img);
        set_err(err, errcap, "pdf: out of memory rotating page");
        return 0;
    }
    pdf_trim_whitespace(img);

    img->hotspot_x = 0;
    img->hotspot_y = 0;

    pdf_font_table_free(&fonts);
    free(xref.entries);
    return 1;
}

int cupidimage_load_pdf(const unsigned char *data, size_t size, cupidimage_image *img, char *err,
                        size_t errcap) {
    return cupidimage_load_pdf_internal(data, size, img, 0, err, errcap);
}

int cupidimage_load_pdf_page(const unsigned char *data, size_t size, cupidimage_image *img,
                             int page_index, char *err, size_t errcap) {
    return cupidimage_load_pdf_internal(data, size, img, page_index, err, errcap);
}

int cupidimage_get_pdf_page_count(const unsigned char *data, size_t size, int *count, char *err,
                                  size_t errcap) {
    if (!data || !count) {
        set_err(err, errcap, "pdf: null pointer");
        return 0;
    }
    if (!pdf_check_header(data, size)) {
        set_err(err, errcap, "pdf: invalid header");
        return 0;
    }

    size_t eof_pos = 0;
    if (!find_eof_marker(data, size, &eof_pos)) {
        set_err(err, errcap, "pdf: EOF marker not found");
        return 0;
    }

    size_t xref_offset = 0;
    if (!find_startxref(data, eof_pos, &xref_offset)) {
        set_err(err, errcap, "pdf: startxref not found");
        return 0;
    }

    pdf_xref xref = {0};
    if (!parse_xref_table(data, size, xref_offset, &xref, err, errcap)) {
        return 0;
    }

    if (!parse_trailer(data, size, xref_offset, &xref, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    int pages_obj = 0;
    if (!get_pages_obj(data, size, &xref, &pages_obj, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    int page_count = 0;
    if (!get_pages_count_from_obj(data, size, pages_obj, &page_count, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    *count = page_count;
    free(xref.entries);
    return 1;
}

int cupidimage_load_pdf_page_file(const char *path, cupidimage_image *img, int page_index,
                                  char *err, size_t errcap) {
    unsigned char *buf = NULL;
    size_t size = 0;
    if (!cupidimage_read_file_bytes(path, &buf, &size, err, errcap)) {
        return 0;
    }
    int ok = cupidimage_load_pdf_page(buf, size, img, page_index, err, errcap);
    free(buf);
    return ok;
}

int cupidimage_get_pdf_page_count_file(const char *path, int *count, char *err, size_t errcap) {
    unsigned char *buf = NULL;
    size_t size = 0;
    if (!cupidimage_read_file_bytes(path, &buf, &size, err, errcap)) {
        return 0;
    }
    int ok = cupidimage_get_pdf_page_count(buf, size, count, err, errcap);
    free(buf);
    return ok;
}

int cupidimage_load_pdf_file(const char *path, cupidimage_image *img, char *err, size_t errcap) {
    return cupidimage_load_pdf_page_file(path, img, 0, err, errcap);
}
