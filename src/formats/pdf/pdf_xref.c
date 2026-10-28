#include "pdf_internal.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define PDF_XREF_ROWS (1024u * 1024u)
#define PDF_XREF_SECTIONS 128u
#define PDF_XREF_BYTES (64u * 1024u * 1024u)

typedef struct {
    pdf_object_view dictionary;
    const unsigned char *raw;
    unsigned char *decoded;
    size_t length, first_entry, object_limit;
} pdf_xref_pending;

typedef struct {
    const unsigned char *data;
    size_t size, capacity, decoded;
    pdf_xref *result;
    size_t visited[PDF_XREF_SECTIONS];
    unsigned sections, visited_count, pending_count;
    pdf_xref_pending pending[PDF_XREF_SECTIONS];
} pdf_xref_builder;

typedef struct {
    size_t size, previous, hybrid;
    int has_previous, has_hybrid;
    pdf_object_id root, info;
} pdf_xref_trailer;

static int xref_word(const unsigned char *data, size_t size, size_t pos, const char *word) {
    size_t length = strlen(word);
    return pos <= size && length <= size - pos && !memcmp(data + pos, word, length) &&
           (length == size - pos || cupidimage_pdf_is_ws(data[pos + length]) ||
            cupidimage_pdf_is_delim(data[pos + length]));
}

static int xref_uint(const pdf_object_view *view, size_t *pos, size_t limit, size_t *number) {
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    int negative = 0;
    if (*pos < view->len && (view->data[*pos] == '+' || view->data[*pos] == '-'))
        negative = view->data[(*pos)++] == '-';
    if (!cupidimage_pdf_parse_decimal(view->data, view->len, pos, limit, number) ||
        (negative && *number) ||
        (*pos < view->len && !cupidimage_pdf_is_ws(view->data[*pos]) &&
         !cupidimage_pdf_is_delim(view->data[*pos])))
        return 0;
    return 1;
}

static int xref_key_uint(const pdf_object_view *view, const char *key, size_t limit, size_t *number,
                         int required, int *present) {
    size_t pos;
    *present = 0;
    if (!cupidimage_pdf_page_value(view, key, &pos) ||
        xref_word(view->data, view->len, pos, "null"))
        return !required;
    pdf_object_id reference;
    if (cupidimage_pdf_page_reference(view, pos, &reference) ||
        !xref_uint(view, &pos, limit, number))
        return 0;
    *present = 1;
    return 1;
}

static int xref_name(const pdf_object_view *view, size_t *pos) {
    size_t start = *pos;
    char name[128];
    return cupidimage_pdf_read_name(view, pos, name) ||
           (start < view->len && view->data[start] == '/' && *pos == start + 1);
}

/* Bootstrap stream dictionaries cannot depend on the index they describe. */
static int xref_direct_value(const pdf_object_view *view, size_t *pos, unsigned depth) {
    if (depth == 64)
        return 0;
    cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
    if (*pos == view->len)
        return 0;
    unsigned char c = view->data[*pos];
    if (c == '/')
        return xref_name(view, pos);
    if (c == '(') {
        (*pos)++;
        unsigned nesting = 1;
        while (*pos < view->len && nesting) {
            c = view->data[(*pos)++];
            if (c == '\\' && *pos < view->len)
                (*pos)++;
            else if (c == '(') {
                if (++nesting > 64)
                    return 0;
            } else if (c == ')')
                nesting--;
        }
        return nesting == 0;
    }
    if (c == '[' || (c == '<' && view->len - *pos >= 2 && view->data[*pos + 1] == '<')) {
        int dictionary = c == '<';
        *pos += dictionary ? 2 : 1;
        for (;;) {
            cupidimage_pdf_skip_ws_and_comments(view->data, view->len, pos);
            if (*pos == view->len)
                return 0;
            if (!dictionary && view->data[*pos] == ']') {
                (*pos)++;
                return 1;
            }
            if (dictionary && view->len - *pos >= 2 && !memcmp(view->data + *pos, ">>", 2)) {
                *pos += 2;
                return 1;
            }
            if (dictionary && (view->data[*pos] != '/' || !xref_name(view, pos)))
                return 0;
            if (!xref_direct_value(view, pos, depth + 1))
                return 0;
        }
    }
    if (c == '<') {
        (*pos)++;
        while (*pos < view->len) {
            c = view->data[(*pos)++];
            if (c == '>')
                return 1;
            if (!cupidimage_pdf_is_ws(c) && !(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f') &&
                !(c >= 'A' && c <= 'F'))
                return 0;
        }
        return 0;
    }
    const char *words[] = {"null", "true", "false"};
    for (unsigned i = 0; i < 3; i++)
        if (xref_word(view->data, view->len, *pos, words[i])) {
            *pos += strlen(words[i]);
            return 1;
        }
    pdf_object_id reference;
    double number;
    if (cupidimage_pdf_page_reference(view, *pos, &reference) ||
        !cupidimage_pdf_parse_pdf_number(view->data, view->len, pos, &number))
        return 0;
    return *pos == view->len || cupidimage_pdf_is_ws(view->data[*pos]) ||
           cupidimage_pdf_is_delim(view->data[*pos]);
}

static int xref_metadata(const pdf_object_view *view, pdf_xref_trailer *trailer) {
    int present;
    memset(trailer, 0, sizeof(*trailer));
    if (!xref_key_uint(view, "Size", (size_t)INT_MAX + 1, &trailer->size, 1, &present) ||
        !trailer->size ||
        !xref_key_uint(view, "Prev", SIZE_MAX, &trailer->previous, 0, &trailer->has_previous) ||
        !xref_key_uint(view, "XRefStm", SIZE_MAX, &trailer->hybrid, 0, &trailer->has_hybrid))
        return 0;
    size_t pos;
    if (cupidimage_pdf_page_value(view, "Root", &pos) &&
        !xref_word(view->data, view->len, pos, "null") &&
        !cupidimage_pdf_page_reference(view, pos, &trailer->root))
        return 0;
    if (cupidimage_pdf_page_value(view, "Info", &pos))
        cupidimage_pdf_page_reference(view, pos, &trailer->info);
    return 1;
}

static int xref_compare(const void *left, const void *right) {
    const pdf_xref_entry *a = left, *b = right;
    if (a->obj_num != b->obj_num)
        return a->obj_num < b->obj_num ? -1 : 1;
    return a->order < b->order ? -1 : a->order > b->order;
}

static int xref_add(pdf_xref_builder *builder, size_t number, unsigned kind, uint64_t field,
                    uint64_t last) {
    pdf_xref *xref = builder->result;
    if (xref->count == PDF_XREF_ROWS || number > INT_MAX || field > SIZE_MAX ||
        (kind <= 1 && last > 65535) || (kind == 2 && (field > INT_MAX || last > UINT_MAX)))
        return 0;
    if ((size_t)xref->count == builder->capacity) {
        size_t capacity = builder->capacity ? builder->capacity * 2 : 256;
        pdf_xref_entry *entries = realloc(xref->entries, capacity * sizeof(*entries));
        if (!entries)
            return 0;
        xref->entries = entries;
        builder->capacity = capacity;
    }
    pdf_xref_entry *entry = xref->entries + xref->count;
    memset(entry, 0, sizeof(*entry));
    entry->obj_num = (int)number;
    entry->kind = kind;
    entry->offset = (size_t)field;
    entry->gen_num = kind == 2 ? 0 : (int)last;
    entry->index = kind == 2 ? (unsigned)last : 0;
    entry->order = (unsigned)xref->count++;
    return 1;
}

static int xref_table(pdf_xref_builder *builder, size_t offset, pdf_xref_trailer *trailer) {
    pdf_object_view view = {builder->data, builder->size, NULL, NULL};
    size_t pos = offset + 4, first_entry = (size_t)builder->result->count;
    for (;;) {
        cupidimage_pdf_skip_ws_and_comments(view.data, view.len, &pos);
        if (xref_word(view.data, view.len, pos, "trailer")) {
            pos += 7;
            break;
        }
        size_t first, count;
        if (!xref_uint(&view, &pos, INT_MAX, &first) ||
            !xref_uint(&view, &pos, PDF_XREF_ROWS, &count) || count > (size_t)INT_MAX + 1 - first)
            return 0;
        for (size_t i = 0; i < count; i++) {
            size_t field, generation;
            if (!xref_uint(&view, &pos, SIZE_MAX, &field) ||
                !xref_uint(&view, &pos, 65536, &generation))
                return 0;
            cupidimage_pdf_skip_ws_and_comments(view.data, view.len, &pos);
            if (pos == view.len || (view.data[pos] != 'n' && view.data[pos] != 'f'))
                return 0;
            unsigned kind = view.data[pos++] == 'n';
            if (pos < view.len && !cupidimage_pdf_is_ws(view.data[pos]))
                return 0;
            if (generation == 65536) {
                if (first + i || kind || field)
                    return 0;
                generation = 65535;
            }
            if (!xref_add(builder, first + i, kind, field, generation))
                return 0;
        }
    }
    pdf_object_view dictionary = {0};
    size_t end;
    if (!cupidimage_pdf_dictionary_view(&view, pos, &dictionary, &end) ||
        !xref_metadata(&dictionary, trailer))
        return 0;
    size_t count = (size_t)builder->result->count - first_entry;
    pdf_xref_entry *entries = count ? builder->result->entries + first_entry : NULL;
    if (count > 1)
        qsort(entries, count, sizeof(*entries), xref_compare);
    for (size_t i = 0; i < count; i++) {
        if (i && entries[i].obj_num == entries[i - 1].obj_num)
            return 0;
        if ((size_t)entries[i].obj_num >= trailer->size)
            entries[i].kind = 3;
    }
    return 1;
}

static int xref_field(const unsigned char *data, size_t width, uint64_t *value) {
    *value = 0;
    int overflow = 0;
    for (size_t i = 0; i < width; i++) {
        if (*value > (UINT64_MAX >> 8))
            overflow = 1;
        *value = (*value << 8) | data[i];
    }
    return !overflow;
}

static int xref_ranges(const pdf_object_view *view, size_t start, size_t size, size_t *rows) {
    size_t pos = start, previous = 0;
    *rows = 0;
    if (pos == SIZE_MAX) {
        *rows = size;
        return size <= PDF_XREF_ROWS;
    }
    if (pos == view->len || view->data[pos++] != '[')
        return 0;
    for (;;) {
        cupidimage_pdf_skip_ws_and_comments(view->data, view->len, &pos);
        if (pos < view->len && view->data[pos] == ']')
            return 1;
        size_t first, count;
        if (!xref_uint(view, &pos, INT_MAX, &first) ||
            !xref_uint(view, &pos, PDF_XREF_ROWS, &count) || first < previous ||
            count > (size_t)INT_MAX + 1 - first || count > PDF_XREF_ROWS - *rows)
            return 0;
        previous = first + count;
        *rows += count;
    }
}

static int xref_stream_end(const unsigned char *data, size_t size, size_t pos) {
    cupidimage_pdf_skip_ws_and_comments(data, size, &pos);
    if (!xref_word(data, size, pos, "endstream"))
        return 0;
    pos += 9;
    cupidimage_pdf_skip_ws_and_comments(data, size, &pos);
    return xref_word(data, size, pos, "endobj");
}

/* The decoded row count bootstraps legacy indirect Length values. The resulting
 * bytes remain provisional until Length is resolved against this revision's index. */
static int xref_bootstrap_stream(const pdf_object_view *dictionary, const unsigned char *raw,
                                 size_t available, size_t expected, unsigned char **decoded,
                                 size_t *length) {
    size_t pos;
    if (!cupidimage_pdf_page_value(dictionary, "Filter", &pos) ||
        xref_word(dictionary->data, dictionary->len, pos, "null")) {
        return expected <= available && xref_stream_end(raw, available, expected) &&
               cupidimage_pdf_decode_direct_stream(dictionary, raw, expected, decoded, length);
    }
    size_t limit = available < PDF_XREF_BYTES ? available : PDF_XREF_BYTES;
    unsigned attempts = 0;
    for (size_t end = 0; end < limit; end++) {
        if (!xref_word(raw, available, end, "endstream") || !xref_stream_end(raw, available, end))
            continue;
        if (++attempts > 128)
            return 0;
        for (unsigned eol = 0; eol <= 2 && eol <= end; eol++) {
            if (eol && raw[end - eol] != '\r' && raw[end - eol] != '\n')
                break;
            if (cupidimage_pdf_decode_direct_stream(dictionary, raw, end - eol, decoded, length)) {
                if (*length == expected)
                    return 1;
                free(*decoded);
                *decoded = NULL;
            }
        }
    }
    return 0;
}

static int xref_stream(pdf_xref_builder *builder, size_t offset, pdf_xref_trailer *trailer,
                       size_t table_start, size_t table_end) {
    size_t first_entry = table_end > table_start ? table_start : (size_t)builder->result->count;
    pdf_object_id object;
    size_t body, end, pos, widths[3], index = SIZE_MAX, rows, raw_length;
    if (!cupidimage_pdf_object_header(builder->data, builder->size, offset, &object, &body))
        return 0;
    pdf_object_view tail = {builder->data + body, builder->size - body, NULL, NULL}, view = {0};
    if (!cupidimage_pdf_dictionary_view(&tail, 0, &view, &end) || !xref_metadata(&view, trailer))
        return 0;
    char name[128];
    if (!cupidimage_pdf_page_value(&view, "Type", &pos) ||
        !cupidimage_pdf_read_name(&view, &pos, name) || strcmp(name, "XRef"))
        return 0;
    const char *direct[] = {"Filter", "DecodeParms"};
    for (unsigned i = 0; i < sizeof(direct) / sizeof(*direct); i++)
        if (cupidimage_pdf_page_value(&view, direct[i], &pos) && !xref_direct_value(&view, &pos, 0))
            return 0;
    const char *forbidden[] = {"F"};
    for (unsigned i = 0; i < sizeof(forbidden) / sizeof(*forbidden); i++)
        if (cupidimage_pdf_page_value(&view, forbidden[i], &pos) &&
            !xref_word(view.data, view.len, pos, "null"))
            return 0;
    if (!cupidimage_pdf_page_value(&view, "W", &pos) || pos == view.len || view.data[pos++] != '[')
        return 0;
    size_t row_size = 0;
    for (unsigned i = 0; i < 3; i++) {
        if (!xref_uint(&view, &pos, PDF_XREF_BYTES, widths + i) || widths[i] > SIZE_MAX - row_size)
            return 0;
        row_size += widths[i];
    }
    cupidimage_pdf_skip_ws_and_comments(view.data, view.len, &pos);
    if (pos == view.len || view.data[pos] != ']')
        return 0;
    if (cupidimage_pdf_page_value(&view, "Index", &pos) &&
        !xref_word(view.data, view.len, pos, "null"))
        index = pos;
    if (!xref_ranges(&view, index, trailer->size, &rows) || (rows && !row_size) ||
        (row_size && rows > PDF_XREF_BYTES / row_size))
        return 0;
    int present;
    pdf_object_id length_reference;
    if (!cupidimage_pdf_page_value(&view, "Length", &pos))
        return 0;
    int indirect_length = cupidimage_pdf_page_reference(&view, pos, &length_reference);
    if (!indirect_length && !xref_key_uint(&view, "Length", SIZE_MAX, &raw_length, 1, &present))
        return 0;
    pos = end;
    cupidimage_pdf_skip_ws_and_comments(tail.data, tail.len, &pos);
    if (!xref_word(tail.data, tail.len, pos, "stream"))
        return 0;
    pos += 6;
    if (pos < tail.len && tail.data[pos] == '\r') {
        pos++;
        if (pos < tail.len && tail.data[pos] == '\n')
            pos++;
    } else if (pos < tail.len && tail.data[pos] == '\n')
        pos++;
    else
        return 0;
    const unsigned char *raw = tail.data + pos;
    unsigned char *decoded = NULL;
    size_t length = 0;
    if (indirect_length) {
        if (!xref_bootstrap_stream(&view, raw, tail.len - pos, rows * row_size, &decoded, &length))
            return 0;
    } else if (raw_length > tail.len - pos ||
               !xref_stream_end(tail.data, tail.len, pos + raw_length) ||
               !cupidimage_pdf_decode_direct_stream(&view, raw, raw_length, &decoded, &length))
        return 0;
    int ok = length == rows * row_size && length <= PDF_XREF_BYTES - builder->decoded;
    if (ok)
        builder->decoded += length;
    size_t at = 0, first = 0, count = rows, parsed = 0;
    pos = index == SIZE_MAX ? 0 : index + 1;
    int self = 0;
    while (ok && parsed < rows) {
        if (index != SIZE_MAX &&
            (!xref_uint(&view, &pos, INT_MAX, &first) || !xref_uint(&view, &pos, rows, &count))) {
            ok = 0;
            break;
        }
        for (size_t i = 0; ok && i < count; i++) {
            if (first + i >= trailer->size) {
                at += row_size;
                ok = xref_add(builder, first + i, 3, 0, 0);
                continue;
            }
            uint64_t kind = 1, field = 0, last = 0;
            int known = !widths[0] || xref_field(decoded + at, widths[0], &kind);
            at += widths[0];
            if (!known || kind > 2) {
                kind = 3;
                at += widths[1] + widths[2];
            } else {
                ok = xref_field(decoded + at, widths[1], &field);
                at += widths[1];
                ok = ok && xref_field(decoded + at, widths[2], &last);
                at += widths[2];
            }
            if (ok && first + i == PDF_OBJECT_NUMBER(object) && kind == 1 && field == offset &&
                last == PDF_OBJECT_GENERATION(object))
                self = 1;
            if (ok)
                ok = xref_add(builder, first + i, (unsigned)kind, field, last);
        }
        parsed += count;
        if (index == SIZE_MAX)
            break;
    }
    if (!self)
        for (size_t i = table_start; i < table_end; i++) {
            const pdf_xref_entry *entry = builder->result->entries + i;
            if ((unsigned)entry->obj_num == PDF_OBJECT_NUMBER(object) && entry->kind == 1 &&
                entry->offset == offset &&
                (unsigned)entry->gen_num == PDF_OBJECT_GENERATION(object))
                self = 1;
        }
    if (ok && self && indirect_length) {
        pdf_xref_pending *pending = builder->pending + builder->pending_count++;
        *pending = (pdf_xref_pending){view, raw, decoded, length, first_entry, trailer->size};
    } else
        free(decoded);
    return ok && self;
}

static int xref_section(pdf_xref_builder *builder, size_t offset, int hybrid, size_t table_start,
                        size_t table_end, size_t maximum_size) {
    if (offset >= builder->size || builder->sections == PDF_XREF_SECTIONS)
        return 0;
    if (!hybrid) {
        for (unsigned i = 0; i < builder->visited_count; i++)
            if (builder->visited[i] == offset)
                return 0;
        builder->visited[builder->visited_count++] = offset;
    }
    int latest = builder->sections++ == 0;
    size_t first = (size_t)builder->result->count;
    int classic = xref_word(builder->data, builder->size, offset, "xref");
    pdf_xref_trailer trailer;
    if (hybrid && classic)
        return 0;
    if (!(classic ? xref_table(builder, offset, &trailer)
                  : xref_stream(builder, offset, &trailer, table_start, table_end)))
        return 0;
    if (latest) {
        builder->result->root_obj = trailer.root;
        builder->result->info_obj = trailer.info;
        builder->result->object_limit = trailer.size;
    } else if (trailer.size > maximum_size)
        return 0;
    if (hybrid)
        return 1;
    size_t last = (size_t)builder->result->count;
    if (classic && trailer.has_hybrid &&
        !xref_section(builder, trailer.hybrid, 1, first, last, trailer.size))
        return 0;
    return !trailer.has_previous || xref_section(builder, trailer.previous, 0, 0, 0, trailer.size);
}

static int xref_validate_lengths(pdf_xref_builder *builder) {
    pdf_xref *full = builder->result;
    for (unsigned i = 0; i < builder->pending_count; i++) {
        pdf_xref_pending *pending = builder->pending + i;
        pdf_xref revision = {0};
        revision.data = builder->data;
        revision.size = builder->size;
        revision.object_limit = pending->object_limit;
        revision.entries = malloc((size_t)full->count * sizeof(*revision.entries));
        if (!revision.entries)
            return 0;
        for (int j = 0; j < full->count; j++) {
            const pdf_xref_entry *entry = full->entries + j;
            if (entry->order >= pending->first_entry &&
                (!revision.count || revision.entries[revision.count - 1].obj_num != entry->obj_num))
                revision.entries[revision.count++] = *entry;
        }
        pdf_object_view dictionary = pending->dictionary, value = {0};
        dictionary.xref = &revision;
        size_t pos = 0, length = 0;
        int ok = cupidimage_pdf_dictionary_value(builder->data, builder->size, &dictionary,
                                                 "Length", &value) &&
                 xref_uint(&value, &pos, PDF_XREF_BYTES, &length);
        size_t available = builder->size - (size_t)(pending->raw - builder->data);
        unsigned char *decoded = NULL;
        size_t decoded_length = 0;
        if (ok)
            ok = length <= available && xref_stream_end(pending->raw, available, length) &&
                 cupidimage_pdf_decode_direct_stream(&dictionary, pending->raw, length, &decoded,
                                                     &decoded_length) &&
                 decoded_length == pending->length &&
                 !memcmp(decoded, pending->decoded, decoded_length);
        free(decoded);
        cupidimage_pdf_object_view_release(&value);
        free(revision.entries);
        if (!ok)
            return 0;
    }
    return 1;
}

int cupidimage_pdf_parse_xref_table(const unsigned char *data, size_t size, size_t offset,
                                    pdf_xref *xref, char *error, size_t capacity) {
    memset(xref, 0, sizeof(*xref));
    xref->data = data;
    xref->size = size;
    pdf_xref_builder builder = {0};
    builder.data = data;
    builder.size = size;
    builder.result = xref;
    int ok = xref_section(&builder, offset, 0, 0, 0, 0);
    if (ok && xref->count > 1)
        qsort(xref->entries, (size_t)xref->count, sizeof(*xref->entries), xref_compare);
    if (ok)
        ok = xref_validate_lengths(&builder);
    for (unsigned i = 0; i < builder.pending_count; i++)
        free(builder.pending[i].decoded);
    if (!ok) {
        free(xref->entries);
        memset(xref, 0, sizeof(*xref));
        cupidimage_set_err(error, capacity, "pdf: invalid or exhausted cross-reference index");
        return 0;
    }
    int used = 0;
    for (int i = 0; i < xref->count; i++)
        if (!used || xref->entries[i].obj_num != xref->entries[used - 1].obj_num)
            xref->entries[used++] = xref->entries[i];
    xref->count = used;
    return 1;
}

int cupidimage_pdf_parse_trailer(const unsigned char *data, size_t size, size_t offset,
                                 pdf_xref *xref, char *error, size_t capacity) {
    (void)offset;
    if (xref->data != data || xref->size != size || !PDF_OBJECT_NUMBER(xref->root_obj) ||
        PDF_OBJECT_NUMBER(xref->root_obj) >= xref->object_limit) {
        cupidimage_set_err(error, capacity, "pdf: missing or invalid catalog reference");
        return 0;
    }
    return 1;
}

const pdf_xref_entry *cupidimage_pdf_xref_find(const pdf_xref *xref, pdf_object_id object) {
    uint32_t number = PDF_OBJECT_NUMBER(object);
    if (!number || number >= xref->object_limit)
        return NULL;
    int low = 0, high = xref->count;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if ((unsigned)xref->entries[middle].obj_num < number)
            low = middle + 1;
        else
            high = middle;
    }
    if (low == xref->count || (unsigned)xref->entries[low].obj_num != number)
        return NULL;
    const pdf_xref_entry *entry = xref->entries + low;
    return entry->kind == 1 || entry->kind == 2
               ? ((unsigned)entry->gen_num == PDF_OBJECT_GENERATION(object) ? entry : NULL)
               : NULL;
}
