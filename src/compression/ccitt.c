#include "ccitt.h"
#include "ccitt_tables.h"
#include <stdlib.h>
#include <string.h>

#define FAX_LIMIT (64u * 1024u * 1024u)

typedef struct {
    const unsigned char *data;
    size_t length, bit, work;
} fax_bits;

static int fax_bit(fax_bits *bits, int *value) {
    if (bits->bit / 8 >= bits->length || !bits->work)
        return 0;
    bits->work--;
    *value = (bits->data[bits->bit / 8] >> (7 - bits->bit % 8)) & 1;
    bits->bit++;
    return 1;
}

static int fax_eol(fax_bits *bits) {
    size_t save = bits->bit;
    int bit, zeros = 0;
    while (zeros < 4096 && fax_bit(bits, &bit)) {
        if (bit) {
            if (zeros >= 11)
                return 1;
            break;
        }
        zeros++;
    }
    bits->bit = save;
    return 0;
}

static int fax_end(fax_bits *bits, int k) {
    size_t save = bits->bit;
    int count = k < 0 ? 2 : 6;
    for (int i = 0; i < count; i++) {
        if (!fax_eol(bits)) {
            bits->bit = save;
            return 0;
        }
        if (k > 0) {
            int tag;
            if (!fax_bit(bits, &tag) || !tag) {
                bits->bit = save;
                return 0;
            }
        }
    }
    return 1;
}

static int fax_run(fax_bits *bits, int black, uint32_t remaining, uint32_t *result) {
    const ccitt_code *term = black ? ccitt_black_term : ccitt_white_term;
    const ccitt_code *makeup = black ? ccitt_black_makeup : ccitt_white_makeup;
    size_t makeup_count = black ? sizeof(ccitt_black_makeup) / sizeof(*makeup)
                                : sizeof(ccitt_white_makeup) / sizeof(*makeup);
    uint32_t total = 0;
    for (;;) {
        unsigned code = 0;
        int found = 0, terminal = 0;
        uint32_t run = 0;
        for (unsigned length = 1; length <= 13 && !found; length++) {
            int bit;
            if (!fax_bit(bits, &bit))
                return 0;
            code = (code << 1) | (unsigned)bit;
            for (size_t i = 0; i < 64; i++)
                if (term[i].length == length && term[i].code == code) {
                    run = term[i].run;
                    found = terminal = 1;
                    break;
                }
            for (size_t i = 0; i < makeup_count && !found; i++)
                if (makeup[i].length == length && makeup[i].code == code) {
                    run = makeup[i].run;
                    found = 1;
                }
        }
        if (!found || run > remaining - total)
            return 0;
        total += run;
        if (terminal) {
            *result = total;
            return 1;
        }
    }
}

static void fax_paint(unsigned char *row, uint32_t start, uint32_t end, int black) {
    if (!black)
        return;
    for (uint32_t x = start; x < end; x++)
        row[x / 8] |= (unsigned char)(128u >> (x % 8));
}

static int fax_1d(fax_bits *bits, unsigned char *row, uint32_t width) {
    uint32_t x = 0, steps = 0;
    int black = 0;
    while (x < width) {
        uint32_t run;
        if (++steps > 2u * width + 64u || !fax_run(bits, black, width - x, &run))
            return 0;
        fax_paint(row, x, x + run, black);
        x += run;
        black = !black;
    }
    return 1;
}

/* Reference transitions alternate white-to-black and black-to-white; sentinels
   at Columns supply both imaginary transitions beyond the reference line. */
static size_t fax_transitions(const unsigned char *row, uint32_t width, uint32_t *edges) {
    size_t count = 0;
    int black = 0;
    for (uint32_t x = 0; x < width; x++) {
        int next = (row[x / 8] >> (7 - x % 8)) & 1;
        if (next != black) {
            edges[count++] = x;
            black = next;
        }
    }
    edges[count++] = width;
    edges[count++] = width;
    return count;
}

static int fax_2d(fax_bits *bits, unsigned char *row, uint32_t width, const uint32_t *edges,
                  size_t edge_count) {
    uint32_t x = 0, steps = 0;
    int black = 0, initial = 1;
    while (x < width) {
        if (++steps > 2u * width + 64u)
            return 0;
        unsigned code = 0;
        int mode = -1, delta = 0;
        for (unsigned n = 1; n <= 7; n++) {
            int bit;
            if (!fax_bit(bits, &bit))
                return 0;
            code = (code << 1) | (unsigned)bit;
            if (n == 1 && code == 1) {
                mode = 0;
                break;
            }
            if (n == 3 && code == 1) {
                mode = 1;
                break;
            }
            if (n == 4 && code == 1) {
                mode = 2;
                break;
            }
            if ((n == 3 || n == 6 || n == 7) && (code == 2 || code == 3)) {
                mode = 0;
                delta = (n == 3 ? 1 : n == 6 ? 2 : 3) * (code == 3 ? 1 : -1);
                break;
            }
        }
        if (mode < 0)
            return 0; /* Uncompressed extension is not accepted. */
        if (mode == 1) {
            uint32_t first, second;
            if (!fax_run(bits, black, width - x, &first) ||
                !fax_run(bits, !black, width - x - first, &second))
                return 0;
            fax_paint(row, x, x + first, black);
            x += first;
            fax_paint(row, x, x + second, !black);
            x += second;
        } else {
            size_t low = 0, high = edge_count;
            while (low < high) {
                size_t mid = low + (high - low) / 2;
                if (edges[mid] < x || (!initial && edges[mid] == x))
                    low = mid + 1;
                else
                    high = mid;
            }
            if ((low & 1u) != (unsigned)black)
                low++;
            uint32_t b1 = low < edge_count ? edges[low] : width;
            uint32_t b2 = low + 1 < edge_count ? edges[low + 1] : width;
            int64_t next = mode == 2 ? (int64_t)b2 : (int64_t)b1 + delta;
            if (next < (int64_t)x || next > width)
                return 0;
            fax_paint(row, x, (uint32_t)next, black);
            x = (uint32_t)next;
            if (mode == 0)
                black = !black;
        }
        initial = 0;
    }
    return 1;
}

static int fax_empty_tail(const fax_bits *bits) {
    size_t remaining = bits->length * 8 - bits->bit;
    if (remaining > 7)
        return 0;
    for (size_t b = bits->bit; b < bits->length * 8; b++)
        if ((bits->data[b / 8] >> (7 - b % 8)) & 1)
            return 0;
    return 1;
}

/* Find the next row boundary without consuming it. Re-reading a malformed
   row still spends the shared work budget, so recovery cannot loop forever. */
static int fax_resync(fax_bits *bits, size_t start) {
    bits->bit = start;
    size_t zero_start = start;
    unsigned zeros = 0;
    int bit;
    while (fax_bit(bits, &bit)) {
        if (!bit) {
            if (!zeros)
                zero_start = bits->bit - 1;
            if (zeros < 11)
                zeros++;
        } else {
            if (zeros >= 11) {
                bits->bit = zero_start;
                return 1;
            }
            zeros = 0;
        }
    }
    return 0;
}

int cupidimage_ccitt_decode(const unsigned char *data, size_t length,
                            const cupidimage_ccitt_options *o, unsigned char **output,
                            size_t *output_length) {
    if (!output || !output_length)
        return 0;
    *output = NULL;
    *output_length = 0;
    if (!o || (!data && length))
        return 0;
    if (length > FAX_LIMIT || o->columns < 1 || o->columns > 1048576 || o->rows < 0 ||
        o->damaged_rows < 0)
        return 0;

    size_t row_bytes = ((size_t)o->columns + 7) / 8, used = 0, capacity = 0;
    unsigned char *decoded = NULL, *row = calloc(1, row_bytes), *previous = calloc(1, row_bytes);
    uint32_t *edges = malloc(((size_t)o->columns + 2) * sizeof(*edges));
    if (!row || !previous || !edges)
        goto fail;
    fax_bits bits = {data, length, 0, 128u * 1024u * 1024u};
    unsigned rows = 0, damaged = 0;
    int previous_damaged = 0;
    for (;;) {
        if (!o->end_of_block && o->rows && rows == (unsigned)o->rows)
            break;
        if (o->byte_align)
            bits.bit = (bits.bit + 7) & ~(size_t)7;
        if (bits.bit > length * 8)
            goto fail;
        if (fax_end(&bits, o->k))
            break;
        if (!o->end_of_block && fax_empty_tail(&bits))
            break;
        if (used > FAX_LIMIT - row_bytes)
            goto fail;
        int eol = fax_eol(&bits), one_dimensional = o->k == 0;
        int valid = !o->end_of_line || eol;
        if (valid && o->k > 0)
            valid = fax_bit(&bits, &one_dimensional);
        size_t payload_start = bits.bit;
        memset(row, 0, row_bytes);
        if (valid) {
            if (one_dimensional)
                valid = fax_1d(&bits, row, (uint32_t)o->columns);
            else
                valid = fax_2d(&bits, row, (uint32_t)o->columns, edges,
                               fax_transitions(previous, (uint32_t)o->columns, edges));
        }
        if (!valid) {
            if (!o->end_of_line || o->k < 0 || damaged >= (unsigned)o->damaged_rows ||
                !fax_resync(&bits, payload_start))
                goto fail;
            damaged++;
            if (previous_damaged)
                memset(row, 0, row_bytes);
            else
                memcpy(row, previous, row_bytes);
        }
        previous_damaged = !valid;
        if (used + row_bytes > capacity) {
            size_t next = capacity ? capacity * 2 : row_bytes;
            if (next < used + row_bytes)
                next = used + row_bytes;
            if (next > FAX_LIMIT)
                next = FAX_LIMIT;
            unsigned char *grown = realloc(decoded, next);
            if (!grown)
                goto fail;
            decoded = grown;
            capacity = next;
        }
        memcpy(previous, row, row_bytes);
        for (size_t i = 0; i < row_bytes; i++)
            decoded[used + i] = o->black_is_one ? row[i] : (unsigned char)~row[i];
        used += row_bytes;
        rows++;
    }
    free(row);
    free(previous);
    free(edges);
    if (!decoded) {
        decoded = malloc(1);
        if (!decoded)
            return 0;
    }
    *output = decoded;
    *output_length = used;
    return 1;
fail:
    free(decoded);
    free(row);
    free(previous);
    free(edges);
    return 0;
}
