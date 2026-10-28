#include "pdf_internal.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PDF_OBJECT_DEPTH 32u
#define PDF_OBJECT_WORK (64u * 1024u * 1024u)

static int object_lookup(const pdf_xref *, pdf_object_id, pdf_object_view *, unsigned, size_t *);

static int object_word(const pdf_object_view *view, size_t pos, const char *word) {
    size_t length = strlen(word);
    return pos <= view->len && length <= view->len - pos &&
           !memcmp(view->data + pos, word, length) &&
           (length == view->len - pos || cupidimage_pdf_is_ws(view->data[pos + length]) ||
            cupidimage_pdf_is_delim(view->data[pos + length]));
}

static int object_uint(const pdf_object_view *view, size_t *pos, size_t limit, size_t *number) {
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    int negative = 0;
    if (*pos < view->len && (view->data[*pos] == '+' || view->data[*pos] == '-'))
        negative = view->data[(*pos)++] == '-';
    if (!cupidimage_pdf_parse_decimal(view->data, view->len, pos, limit, number) ||
        (negative && *number))
        return 0;
    return *pos == view->len || cupidimage_pdf_is_ws(view->data[*pos]) ||
           cupidimage_pdf_is_delim(view->data[*pos]);
}

static int object_value(const pdf_xref *xref, const pdf_object_view *source, size_t pos,
                        pdf_object_view *result, unsigned depth, size_t *work, int uncompressed) {
    pdf_object_view value = {source->data + pos, source->len - pos, NULL, xref};
    for (;;) {
        pos = 0;
        pdf_object_id reference;
        cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &pos);
        if (!cupidimage_pdf_page_reference(&value, pos, &reference)) {
            *result = value;
            return 1;
        }
        const pdf_xref_entry *entry = cupidimage_pdf_xref_find(xref, reference);
        if (!entry) {
            cupidimage_pdf_object_view_release(&value);
            *result = (pdf_object_view){(const unsigned char *)"null", 4, NULL, xref};
            return 1;
        }
        pdf_object_view next = {0};
        if (++depth >= PDF_OBJECT_DEPTH || (uncompressed && entry->kind != 1) ||
            object_lookup(xref, reference, &next, depth, work) != 1) {
            cupidimage_pdf_object_view_release(&value);
            return 0;
        }
        cupidimage_pdf_object_view_release(&value);
        value = next;
    }
}

static int object_property(const pdf_xref *xref, const pdf_object_view *dictionary, const char *key,
                           pdf_object_view *result, unsigned depth, size_t *work,
                           int uncompressed) {
    size_t pos;
    return cupidimage_pdf_page_value(dictionary, key, &pos) &&
           object_value(xref, dictionary, pos, result, depth, work, uncompressed);
}

#define PDF_STREAM_PARAMETERS_LIMIT 65536u

typedef struct {
    unsigned char *data;
    size_t length;
} object_parameters;

static int object_parameter_append(object_parameters *out, const void *data, size_t length) {
    if (length > PDF_STREAM_PARAMETERS_LIMIT - out->length)
        return 0;
    memcpy(out->data + out->length, data, length);
    out->length += length;
    return 1;
}

static int object_parameter_resolve(const pdf_xref *xref, const pdf_image_stream_profile *profile,
                                    const pdf_object_view *view, size_t pos, pdf_object_view *value,
                                    unsigned depth, size_t *work, int uncompressed) {
    if (xref)
        return object_value(xref, view, pos, value, depth, work, uncompressed);
    /* Standalone renderer fixtures have document bytes but no cross-reference index. */
    return profile && depth < 16 &&
           cupidimage_pdf_resolve_value(profile->document, profile->document_size, view, pos, value,
                                        depth);
}

/* Resolve stream controls with the same recursion and work budget as their container.
 * Only decoder-defined dictionary entries affect decoding; extension keys are ignored. */
static int object_parameter_value(const pdf_xref *xref, const pdf_object_view *view, size_t *pos,
                                  object_parameters *out, unsigned depth, size_t *work,
                                  const pdf_image_stream_profile *profile, const char *filter) {
    static const char *keys[] = {
        "Predictor",        "Colors",     "Columns", "BitsPerComponent",       "EarlyChange",
        "ColorTransform",   "K",          "Rows",    "DamagedRowsBeforeError", "EndOfLine",
        "EncodedByteAlign", "EndOfBlock", "BlackIs1"};
    if (depth >= PDF_OBJECT_DEPTH)
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    if (*pos >= view->len)
        return 0;
    pdf_object_id reference;
    if (cupidimage_pdf_page_reference(view, *pos, &reference)) {
        pdf_object_view value = {0};
        size_t at = 0;
        int ok = object_parameter_resolve(xref, profile, view, *pos, &value, depth, work, 0) &&
                 object_parameter_value(xref, &value, &at, out, depth + 1, work, profile, filter);
        cupidimage_pdf_object_view_release(&value);
        if (!ok || !cupidimage_pdf_parse_reference(view, pos, &reference))
            return 0;
        return 1;
    }
    if (view->data[*pos] == '[') {
        (*pos)++;
        if (!object_parameter_append(out, "[ ", 2))
            return 0;
        for (unsigned count = 0;; count++) {
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
            if (*pos < view->len && view->data[*pos] == ']') {
                (*pos)++;
                return object_parameter_append(out, "] ", 2);
            }
            if (count == 8 ||
                !object_parameter_value(xref, view, pos, out, depth + 1, work, profile, filter))
                return 0;
        }
    }
    if (view->len - *pos >= 2 && !memcmp(view->data + *pos, "<<", 2)) {
        pdf_object_view dictionary = {0};
        size_t end;
        if (!cupidimage_pdf_dictionary_view(view, *pos, &dictionary, &end) ||
            !object_parameter_append(out, "<< ", 3))
            return 0;
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
            size_t at;
            if ((filter && !cupidimage_pdf_filter_parameter_applies(filter, keys[i])) ||
                !cupidimage_pdf_page_value(&dictionary, keys[i], &at))
                continue;
            if (!object_parameter_append(out, "/", 1) ||
                !object_parameter_append(out, keys[i], strlen(keys[i])) ||
                !object_parameter_append(out, " ", 1) ||
                !object_parameter_value(xref, &dictionary, &at, out, depth + 1, work, profile,
                                        filter))
                return 0;
        }
        *pos = end;
        return object_parameter_append(out, ">> ", 3);
    }
    if (view->data[*pos] == '/') {
        char name[128];
        static const char hex[] = "0123456789ABCDEF";
        if (!cupidimage_pdf_read_name(view, pos, name) || !object_parameter_append(out, "/", 1))
            return 0;
        for (size_t i = 0; name[i]; i++) {
            unsigned char c = (unsigned char)name[i];
            char escaped[] = {'#', hex[c >> 4], hex[c & 15]};
            if (!object_parameter_append(out, escaped, 3))
                return 0;
        }
        return object_parameter_append(out, " ", 1);
    }
    size_t start = *pos;
    if (object_word(view, start, "null"))
        *pos += 4;
    else if (object_word(view, start, "true"))
        *pos += 4;
    else if (object_word(view, start, "false"))
        *pos += 5;
    else {
        double number;
        if (!cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &number) ||
            (*pos < view->len && !cupidimage_pdf_is_ws(view->data[*pos]) &&
             !cupidimage_pdf_is_delim(view->data[*pos])))
            return 0;
    }
    return object_parameter_append(out, view->data + start, *pos - start) &&
           object_parameter_append(out, " ", 1);
}

static int object_decode_parameters(const pdf_xref *xref, const pdf_object_view *view,
                                    object_parameters *out, unsigned depth, size_t *work,
                                    const pdf_image_stream_profile *profile, char filters[8][32],
                                    int count) {
    size_t pos = 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    if (pos == view->len)
        return 0;
    if (view->data[pos] != '[') {
        if (count != 1 && !object_word(view, pos, "null"))
            return 0;
        return object_parameter_value(xref, view, &pos, out, depth, work, profile,
                                      count ? filters[0] : NULL);
    }
    pos++;
    if (!object_parameter_append(out, "[ ", 2))
        return 0;
    for (int i = 0; i < count; i++)
        if (!object_parameter_value(xref, view, &pos, out, depth + 1, work, profile, filters[i]))
            return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
    return pos < view->len && view->data[pos] == ']' && object_parameter_append(out, "] ", 2);
}

static int object_decode_stream(const pdf_object_view *view, const unsigned char *data,
                                size_t length, unsigned char **decoded, size_t *decoded_length,
                                unsigned depth, size_t *work, int uncompressed,
                                const pdf_image_stream_profile *profile) {
    object_parameters out = {malloc(PDF_STREAM_PARAMETERS_LIMIT), 0};
    *decoded = NULL;
    *decoded_length = 0;
    if (!out.data)
        return 0;
    int ok = object_parameter_append(&out, "<< ", 3);
    size_t pos;
    if (ok && cupidimage_pdf_page_value(view, "Filter", &pos)) {
        pdf_object_view value = {0};
        size_t at = 0;
        ok = object_parameter_resolve(view->xref, profile, view, pos, &value, depth, work,
                                      uncompressed) &&
             object_parameter_append(&out, "/Filter ", 8) &&
             object_parameter_value(view->xref, &value, &at, &out, depth + 1, work, profile, NULL);
        cupidimage_pdf_object_view_release(&value);
    }
    char filters[8][32];
    int count = 0;
    ok = ok && object_parameter_append(&out, ">>", 2);
    pdf_object_view filter_view = {out.data, out.length, NULL, NULL};
    ok = ok && cupidimage_pdf_stream_filter_names(&filter_view, filters, &count);
    if (ok)
        out.length -= 2;
    if (ok && cupidimage_pdf_page_value(view, "DecodeParms", &pos)) {
        pdf_object_view value = {0};
        ok = object_parameter_resolve(view->xref, profile, view, pos, &value, depth, work,
                                      uncompressed) &&
             object_parameter_append(&out, "/DecodeParms ", 13) &&
             object_decode_parameters(view->xref, &value, &out, depth + 1, work, profile, filters,
                                      count);
        cupidimage_pdf_object_view_release(&value);
    }
    ok = ok && object_parameter_append(&out, ">>", 2);
    pdf_object_view direct = {out.data, out.length, NULL, NULL};
    if (ok)
        ok = profile ? cupidimage_pdf_decode_direct_image_stream(&direct, data, length, decoded,
                                                                 decoded_length, profile)
                     : cupidimage_pdf_decode_direct_stream(&direct, data, length, decoded,
                                                           decoded_length);
    free(out.data);
    return ok;
}

int cupidimage_pdf_decode_stream(const pdf_object_view *view, const unsigned char *data,
                                 size_t length, unsigned char **decoded, size_t *decoded_length) {
    if (!view->xref)
        return cupidimage_pdf_decode_direct_stream(view, data, length, decoded, decoded_length);
    size_t work = PDF_OBJECT_WORK;
    return object_decode_stream(view, data, length, decoded, decoded_length, 0, &work, 0, NULL);
}

int cupidimage_pdf_decode_image_stream(const pdf_object_view *view, const unsigned char *data,
                                       size_t length, unsigned char **decoded,
                                       size_t *decoded_length,
                                       const pdf_image_stream_profile *profile) {
    size_t work = PDF_OBJECT_WORK;
    return profile &&
           object_decode_stream(view, data, length, decoded, decoded_length, 0, &work, 0, profile);
}

static int object_number_property(const pdf_xref *xref, const pdf_object_view *dictionary,
                                  const char *key, size_t limit, size_t *number, unsigned depth,
                                  size_t *work, int uncompressed) {
    pdf_object_view value = {0};
    size_t pos = 0;
    int ok = object_property(xref, dictionary, key, &value, depth, work, uncompressed) &&
             object_uint(&value, &pos, limit, number);
    cupidimage_pdf_object_view_release(&value);
    return ok;
}

static int object_at(const pdf_xref *xref, pdf_object_id object, size_t offset,
                     pdf_object_view *view, unsigned depth, size_t *work) {
    pdf_object_id found;
    size_t start, end;
    if (offset >= xref->size ||
        !cupidimage_pdf_object_header(xref->data, xref->size, offset, &found, &start) ||
        found != object)
        return -1;
    pdf_object_view tail = {xref->data + start, xref->size - start, NULL, xref}, dictionary = {0};
    size_t pos = 0;
    cupidimage_pdf_skip_ws_and_comments(tail.data, tail.len, &pos);
    if (tail.len - pos >= 2 && !memcmp(tail.data + pos, "<<", 2)) {
        if (!cupidimage_pdf_dictionary_view(&tail, pos, &dictionary, &end) || end > *work)
            return -1;
        *work -= end;
        pos = end;
        cupidimage_pdf_skip_ws_and_comments(tail.data, tail.len, &pos);
        if (object_word(&tail, pos, "stream")) {
            size_t length;
            if (!object_number_property(xref, &dictionary, "Length", INT_MAX, &length, depth, work,
                                        0))
                return -1;
            pos += 6;
            if (pos < tail.len && tail.data[pos] == '\r') {
                pos++;
                if (pos < tail.len && tail.data[pos] == '\n')
                    pos++;
            } else if (pos < tail.len && tail.data[pos] == '\n')
                pos++;
            else
                return -1;
            if (length > tail.len - pos)
                return -1;
            pos += length;
            cupidimage_pdf_skip_ws_and_comments(tail.data, tail.len, &pos);
            if (!object_word(&tail, pos, "endstream"))
                return -1;
            pos += 9;
            cupidimage_pdf_skip_ws_and_comments(tail.data, tail.len, &pos);
            if (!object_word(&tail, pos, "endobj"))
                return -1;
            *view = tail;
            view->len = pos;
            return 1;
        }
    }
    if (!cupidimage_pdf_object_end(xref->data, xref->size, start, &end) || end - start > *work)
        return -1;
    *work -= end - start;
    *view = tail;
    view->len = end - start;
    return 1;
}

static int object_compressed(const pdf_xref *xref, pdf_object_id object,
                             const pdf_xref_entry *entry, pdf_object_view *view, unsigned depth,
                             size_t *work) {
    pdf_object_id container_id = PDF_OBJECT_ID(entry->offset, 0);
    const pdf_xref_entry *container_entry = cupidimage_pdf_xref_find(xref, container_id);
    if (!container_entry || container_entry->kind != 1)
        return -1;
    pdf_object_view container = {0}, type = {0};
    unsigned char *decoded = NULL;
    size_t count, first, length = 0, pos = 0;
    int ok = object_lookup(xref, container_id, &container, depth + 1, work) == 1;
    char name[128];
    if (ok)
        ok = object_property(xref, &container, "Type", &type, depth, work, 1) &&
             cupidimage_pdf_read_name(&type, &pos, name) && !strcmp(name, "ObjStm") &&
             object_number_property(xref, &container, "N", 200000, &count, depth, work, 1) &&
             count && entry->index < count &&
             object_number_property(xref, &container, "First", INT_MAX, &first, depth, work, 1);
    const unsigned char *raw = NULL;
    size_t raw_length, checked_length;
    if (ok)
        ok = object_number_property(xref, &container, "Length", INT_MAX, &checked_length, depth,
                                    work, 1) &&
             cupidimage_pdf_object_stream(xref->data, xref->size, &container, &raw, &raw_length) &&
             raw_length == checked_length &&
             object_decode_stream(&container, raw, raw_length, &decoded, &length, depth + 1, work,
                                  1, NULL) &&
             first < length && length <= *work;
    if (ok) {
        *work -= length;
        pdf_object_view header = {decoded, first, NULL, xref};
        size_t previous = 0, begin = 0, end = length - first;
        pos = 0;
        for (size_t i = 0; ok && i < count; i++) {
            size_t number, offset;
            ok = object_uint(&header, &pos, INT_MAX, &number) && number &&
                 object_uint(&header, &pos, INT_MAX, &offset) && offset < length - first &&
                 (!i || offset > previous);
            if (!ok)
                break;
            previous = offset;
            if (i == entry->index) {
                if (number != PDF_OBJECT_NUMBER(object))
                    ok = 0;
                begin = offset;
            } else if (i == (size_t)entry->index + 1)
                end = offset;
        }
        cupidimage_pdf_skip_ws_and_comments(header.data, header.len, &pos);
        ok = ok && pos == header.len && end > begin;
        if (ok) {
            unsigned char *copy = malloc(end - begin);
            if (!copy)
                ok = 0;
            else {
                memcpy(copy, decoded + first + begin, end - begin);
                view->data = view->owned = copy;
                view->len = end - begin;
                view->xref = xref;
                pdf_object_view dictionary = {0};
                size_t after;
                if (cupidimage_pdf_dictionary_view(view, 0, &dictionary, &after)) {
                    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &after);
                    if (object_word(view, after, "stream")) {
                        cupidimage_pdf_object_view_release(view);
                        ok = 0;
                    }
                }
            }
        }
    }
    free(decoded);
    cupidimage_pdf_object_view_release(&type);
    cupidimage_pdf_object_view_release(&container);
    return ok ? 1 : -1;
}

static int object_lookup(const pdf_xref *xref, pdf_object_id object, pdf_object_view *view,
                         unsigned depth, size_t *work) {
    if (depth >= PDF_OBJECT_DEPTH)
        return -1;
    const pdf_xref_entry *entry = cupidimage_pdf_xref_find(xref, object);
    if (!entry)
        return 0;
    if (entry->kind == 1)
        return object_at(xref, object, entry->offset, view, depth, work);
    return object_compressed(xref, object, entry, view, depth, work);
}

int cupidimage_pdf_lookup_object(const pdf_xref *xref, const unsigned char *data, size_t size,
                                 pdf_object_id object, pdf_object_view *view) {
    memset(view, 0, sizeof(*view));
    if (xref) {
        if (xref->data != data || xref->size != size)
            return -1;
        size_t work = PDF_OBJECT_WORK;
        return object_lookup(xref, object, view, 0, &work);
    }
    if (size < 5 || memcmp(data, "%PDF-", 5))
        return cupidimage_pdf_scan_object(data, size, object, view) ? 1 : -1;
    size_t eof, offset;
    pdf_xref index = {0};
    if (!cupidimage_pdf_find_eof_marker(data, size, &eof) ||
        !cupidimage_pdf_find_startxref(data, eof, &offset) ||
        !cupidimage_pdf_parse_xref_table(data, size, offset, &index, NULL, 0))
        return -1;
    size_t work = PDF_OBJECT_WORK;
    int result = object_lookup(&index, object, view, 0, &work);
    view->xref = NULL;
    free(index.entries);
    return result;
}

int cupidimage_pdf_get_object_content(const unsigned char *data, size_t size, pdf_object_id object,
                                      pdf_object_view *view) {
    return cupidimage_pdf_lookup_object(NULL, data, size, object, view) == 1;
}

int cupidimage_pdf_view_object(const unsigned char *data, size_t size,
                               const pdf_object_view *parent, pdf_object_id object,
                               pdf_object_view *view) {
    return cupidimage_pdf_lookup_object(parent ? parent->xref : NULL, data, size, object, view) ==
           1;
}
