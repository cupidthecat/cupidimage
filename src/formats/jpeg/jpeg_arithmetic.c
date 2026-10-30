#include "jpeg_internal.h"
#include <limits.h>
#include <string.h>

typedef struct {
    uint16_t qe;
    uint8_t lps, mps, toggle;
} jpeg_probability;

/* ISO/IEC 10918-1, Table D.3. */
static const jpeg_probability jpeg_probabilities[113] = {
    {0x5A1D, 1, 1, 1},     {0x2586, 14, 2, 0},    {0x1114, 16, 3, 0},    {0x080B, 18, 4, 0},
    {0x03D8, 20, 5, 0},    {0x01DA, 23, 6, 0},    {0x00E5, 25, 7, 0},    {0x006F, 28, 8, 0},
    {0x0036, 30, 9, 0},    {0x001A, 33, 10, 0},   {0x000D, 35, 11, 0},   {0x0006, 9, 12, 0},
    {0x0003, 10, 13, 0},   {0x0001, 12, 13, 0},   {0x5A7F, 15, 15, 1},   {0x3F25, 36, 16, 0},
    {0x2CF2, 38, 17, 0},   {0x207C, 39, 18, 0},   {0x17B9, 40, 19, 0},   {0x1182, 42, 20, 0},
    {0x0CEF, 43, 21, 0},   {0x09A1, 45, 22, 0},   {0x072F, 46, 23, 0},   {0x055C, 48, 24, 0},
    {0x0406, 49, 25, 0},   {0x0303, 51, 26, 0},   {0x0240, 52, 27, 0},   {0x01B1, 54, 28, 0},
    {0x0144, 56, 29, 0},   {0x00F5, 57, 30, 0},   {0x00B7, 59, 31, 0},   {0x008A, 60, 32, 0},
    {0x0068, 62, 33, 0},   {0x004E, 63, 34, 0},   {0x003B, 32, 35, 0},   {0x002C, 33, 9, 0},
    {0x5AE1, 37, 37, 1},   {0x484C, 64, 38, 0},   {0x3A0D, 65, 39, 0},   {0x2EF1, 67, 40, 0},
    {0x261F, 68, 41, 0},   {0x1F33, 69, 42, 0},   {0x19A8, 70, 43, 0},   {0x1518, 72, 44, 0},
    {0x1177, 73, 45, 0},   {0x0E74, 74, 46, 0},   {0x0BFB, 75, 47, 0},   {0x09F8, 77, 48, 0},
    {0x0861, 78, 49, 0},   {0x0706, 79, 50, 0},   {0x05CD, 48, 51, 0},   {0x04DE, 50, 52, 0},
    {0x040F, 50, 53, 0},   {0x0363, 51, 54, 0},   {0x02D4, 52, 55, 0},   {0x025C, 53, 56, 0},
    {0x01F8, 54, 57, 0},   {0x01A4, 55, 58, 0},   {0x0160, 56, 59, 0},   {0x0125, 57, 60, 0},
    {0x00F6, 58, 61, 0},   {0x00CB, 59, 62, 0},   {0x00AB, 61, 63, 0},   {0x008F, 61, 32, 0},
    {0x5B12, 65, 65, 1},   {0x4D04, 80, 66, 0},   {0x412C, 81, 67, 0},   {0x37D8, 82, 68, 0},
    {0x2FE8, 83, 69, 0},   {0x293C, 84, 70, 0},   {0x2379, 86, 71, 0},   {0x1EDF, 87, 72, 0},
    {0x1AA9, 87, 73, 0},   {0x174E, 72, 74, 0},   {0x1424, 72, 75, 0},   {0x119C, 74, 76, 0},
    {0x0F6B, 74, 77, 0},   {0x0D51, 75, 78, 0},   {0x0BB6, 77, 79, 0},   {0x0A40, 77, 48, 0},
    {0x5832, 80, 81, 1},   {0x4D1C, 88, 82, 0},   {0x438E, 89, 83, 0},   {0x3BDD, 90, 84, 0},
    {0x34EE, 91, 85, 0},   {0x2EAE, 92, 86, 0},   {0x299A, 93, 87, 0},   {0x2516, 86, 71, 0},
    {0x5570, 88, 89, 1},   {0x4CA9, 95, 90, 0},   {0x44D9, 96, 91, 0},   {0x3E22, 97, 92, 0},
    {0x3824, 99, 93, 0},   {0x32B4, 99, 94, 0},   {0x2E17, 93, 86, 0},   {0x56A8, 95, 96, 1},
    {0x4F46, 101, 97, 0},  {0x47E5, 102, 98, 0},  {0x41CF, 103, 99, 0},  {0x3C3D, 104, 100, 0},
    {0x375E, 99, 93, 0},   {0x5231, 105, 102, 0}, {0x4C0F, 106, 103, 0}, {0x4639, 107, 104, 0},
    {0x415E, 103, 99, 0},  {0x5627, 105, 106, 1}, {0x50E7, 108, 107, 0}, {0x4B85, 109, 103, 0},
    {0x5597, 110, 109, 0}, {0x504F, 111, 107, 0}, {0x5A10, 110, 111, 1}, {0x5522, 112, 109, 0},
    {0x59EB, 112, 111, 1},
};

typedef struct {
    const uint8_t *data;
    size_t size, position;
    uint32_t interval, code;
    int bits, ended, failed;
    uint8_t dc[4][49], ac[4][245];
    unsigned context[4];
} jpeg_arithmetic;

/* A marker terminates the arithmetic segment with implicit zero bytes. */
static unsigned arithmetic_byte(jpeg_arithmetic *reader) {
    if (reader->ended)
        return 0;
    if (reader->position >= reader->size) {
        reader->failed = 1;
        return 0;
    }
    unsigned value = reader->data[reader->position];
    if (value == 255) {
        size_t next = reader->position + 1;
        while (next < reader->size && reader->data[next] == 255)
            next++;
        if (next == reader->size) {
            reader->failed = 1;
            return 0;
        }
        if (reader->data[next]) {
            reader->ended = 1;
            return 0;
        }
        reader->position = next;
    }
    reader->position++;
    return value;
}

static void arithmetic_start(jpeg_arithmetic *reader) {
    reader->interval = 0x10000;
    reader->code = arithmetic_byte(reader) << 24;
    reader->code |= arithmetic_byte(reader) << 16;
    reader->bits = 0;
    memset(reader->dc, 0, sizeof(reader->dc));
    memset(reader->ac, 0, sizeof(reader->ac));
    memset(reader->context, 0, sizeof(reader->context));
}

/* Annex D keeps the interval in 17 bits and the code in two 16-bit halves. */
static int arithmetic_decision(jpeg_arithmetic *reader, uint8_t *state) {
    unsigned index = state ? *state & 127u : 0;
    unsigned mps = state ? *state >> 7 : 0;
    const jpeg_probability *probability = jpeg_probabilities + index;
    unsigned qe = probability->qe;
    unsigned result = mps, lps = 0;
    reader->interval -= qe;
    if ((reader->code >> 16) >= reader->interval) {
        reader->code -= reader->interval << 16;
        lps = reader->interval >= qe;
        reader->interval = qe;
    } else if (reader->interval >= 0x8000) {
        return (int)result;
    } else {
        lps = reader->interval < qe;
    }
    if (lps)
        result ^= 1;
    if (state) {
        if (lps && probability->toggle)
            mps ^= 1;
        *state = (uint8_t)((mps << 7) | (lps ? probability->lps : probability->mps));
    }
    while (reader->interval < 0x8000) {
        if (!reader->bits) {
            reader->code |= arithmetic_byte(reader) << 8;
            reader->bits = 8;
        }
        reader->code <<= 1;
        reader->interval <<= 1;
        reader->bits--;
    }
    return (int)result;
}

static int arithmetic_magnitude(jpeg_arithmetic *reader, uint8_t *bins, unsigned first, unsigned x1,
                                unsigned x2, unsigned *magnitude) {
    unsigned bound = 0, context = first;
    if (arithmetic_decision(reader, bins + context)) {
        bound = 1;
        context = x1;
        if (arithmetic_decision(reader, bins + context)) {
            bound = 2;
            context = x2;
            while (arithmetic_decision(reader, bins + context)) {
                if (bound >= 0x4000)
                    return 0;
                bound <<= 1;
                context++;
            }
        }
    }
    unsigned value = bound;
    context += 14;
    while ((bound >>= 1) != 0)
        if (arithmetic_decision(reader, bins + context))
            value |= bound;
    *magnitude = value + 1;
    return !reader->failed;
}

static int arithmetic_dc(jpeg_arithmetic *reader, jpeg_comp *component, unsigned ci, int al,
                         const jpeg_arithmetic_conditioning *conditioning, int32_t *block) {
    unsigned table = (unsigned)component->dc_table;
    uint8_t *bins = reader->dc[table];
    unsigned context = reader->context[ci];
    int difference = 0;
    reader->context[ci] = 0;
    if (arithmetic_decision(reader, bins + context)) {
        int sign = arithmetic_decision(reader, bins + context + 1);
        unsigned magnitude;
        if (!arithmetic_magnitude(reader, bins, context + 2 + sign, 20, 21, &magnitude))
            return 0;
        unsigned lower = (1u << conditioning->lower[table]) >> 1;
        unsigned upper = 1u << conditioning->upper[table];
        if (magnitude > lower)
            reader->context[ci] = (magnitude > upper ? 12u : 4u) + 4u * sign;
        difference = sign ? -(int)magnitude : (int)magnitude;
    }
    int64_t prediction = (int64_t)component->dc_pred + difference;
    int64_t coefficient = prediction * (1 << al);
    if (prediction < INT_MIN || prediction > INT_MAX || coefficient < INT32_MIN ||
        coefficient > INT32_MAX)
        return 0;
    component->dc_pred = (int)prediction;
    block[0] = (int32_t)coefficient;
    return !reader->failed;
}

static int arithmetic_ac(jpeg_arithmetic *reader, jpeg_comp *component, int ss, int se, int al,
                         const jpeg_arithmetic_conditioning *conditioning, int32_t *block) {
    unsigned table = (unsigned)component->ac_table;
    uint8_t *bins = reader->ac[table];
    for (int k = ss; k <= se; k++) {
        unsigned context = 3u * (k - 1);
        if (arithmetic_decision(reader, bins + context))
            break;
        while (!arithmetic_decision(reader, bins + context + 1)) {
            if (++k > se)
                return 0;
            context += 3;
        }
        int sign = arithmetic_decision(reader, NULL);
        unsigned magnitude;
        unsigned x2 = k <= conditioning->ac[table] ? 189u : 217u;
        if (!arithmetic_magnitude(reader, bins, context + 2, context + 2, x2, &magnitude))
            return 0;
        int64_t value = (int64_t)magnitude * (1 << al);
        if (value > INT32_MAX)
            return 0;
        block[cupidimage_jpeg_zigzag[k]] = sign ? -(int32_t)value : (int32_t)value;
    }
    return !reader->failed;
}

static int arithmetic_refine_ac(jpeg_arithmetic *reader, jpeg_comp *component, int ss, int se,
                                int al, int32_t *block) {
    uint8_t *bins = reader->ac[component->ac_table];
    int last = se;
    while (last >= ss && !block[cupidimage_jpeg_zigzag[last]])
        last--;
    for (int k = ss; k <= se; k++) {
        unsigned context = 3u * (k - 1);
        if (k > last && arithmetic_decision(reader, bins + context))
            break;
        for (;;) {
            int32_t *value = block + cupidimage_jpeg_zigzag[k];
            if (*value) {
                if (arithmetic_decision(reader, bins + context + 2)) {
                    int64_t refined = (int64_t)*value + (*value < 0 ? -(1 << al) : (1 << al));
                    if (refined < INT32_MIN || refined > INT32_MAX)
                        return 0;
                    *value = (int32_t)refined;
                }
                break;
            }
            if (arithmetic_decision(reader, bins + context + 1)) {
                *value = arithmetic_decision(reader, NULL) ? -(1 << al) : (1 << al);
                break;
            }
            if (++k > se)
                return 0;
            context += 3;
        }
    }
    return !reader->failed;
}

static int arithmetic_block(jpeg_arithmetic *reader, jpeg_comp *component, unsigned ci, int ss,
                            int se, int ah, int al, int sequential,
                            const jpeg_arithmetic_conditioning *conditioning, int32_t *block) {
    if (!ss) {
        if (ah) {
            if (arithmetic_decision(reader, NULL))
                block[0] |= 1 << al;
        } else if (!arithmetic_dc(reader, component, ci, al, conditioning, block)) {
            return 0;
        }
        if (!sequential)
            return !reader->failed;
        ss = 1;
    }
    return ah ? arithmetic_refine_ac(reader, component, ss, se, al, block)
              : arithmetic_ac(reader, component, ss, se, al, conditioning, block);
}

static int arithmetic_marker(jpeg_arithmetic *reader, size_t *marker) {
    size_t position = reader->position;
    while (position < reader->size) {
        if (reader->data[position++] != 255)
            continue;
        size_t start = position - 1;
        while (position < reader->size && reader->data[position] == 255)
            position++;
        if (position == reader->size)
            return 0;
        if (reader->data[position]) {
            *marker = start;
            reader->position = position + 1;
            return reader->data[position];
        }
        position++;
    }
    return 0;
}

int cupidimage_jpeg_decode_arithmetic_scan(const uint8_t *data, size_t size, jpeg_comp *components,
                                           int count, const int *scan, int scan_count, int ss,
                                           int se, int ah, int al, int width, int height,
                                           int columns, int rows, int hmax, int vmax,
                                           const jpeg_arithmetic_conditioning *conditioning,
                                           int restart_interval, size_t *consumed, char *error,
                                           size_t capacity, int sequential) {
    jpeg_arithmetic reader = {0};
    reader.data = data;
    reader.size = size;
    arithmetic_start(&reader);
    for (int i = 0; i < count; i++)
        components[i].dc_pred = 0;
    int scan_columns = columns, scan_rows = rows;
    if (scan_count == 1) {
        const jpeg_comp *component = components + scan[0];
        scan_columns = (width * component->h + 8 * hmax - 1) / (8 * hmax);
        scan_rows = (height * component->v + 8 * vmax - 1) / (8 * vmax);
    }
    unsigned mcus = 0, restart = 0;
    for (int y = 0; y < scan_rows; y++) {
        for (int x = 0; x < scan_columns; x++) {
            for (int i = 0; i < scan_count; i++) {
                unsigned ci = (unsigned)scan[i];
                jpeg_comp *component = components + ci;
                int h = scan_count == 1 ? 1 : component->h;
                int v = scan_count == 1 ? 1 : component->v;
                for (int by = 0; by < v; by++) {
                    for (int bx = 0; bx < h; bx++) {
                        size_t index =
                            ((size_t)(y * v + by) * component->blocks_w + x * h + bx) * 64;
                        if (!arithmetic_block(&reader, component, ci, ss, se, ah, al, sequential,
                                              conditioning, component->coeffs + index))
                            goto fail;
                    }
                }
            }
            mcus++;
            if (restart_interval && mcus % (unsigned)restart_interval == 0 &&
                (x + 1 < scan_columns || y + 1 < scan_rows)) {
                size_t marker;
                if (arithmetic_marker(&reader, &marker) != (int)(0xd0 + (restart++ & 7)))
                    goto fail;
                reader.ended = 0;
                arithmetic_start(&reader);
                for (int i = 0; i < count; i++)
                    components[i].dc_pred = 0;
            }
        }
    }
    if (reader.failed || !arithmetic_marker(&reader, consumed))
        goto fail;
    return 1;
fail:
    cupidimage_jpeg_set_error(error, capacity, "invalid arithmetic JPEG scan");
    return 0;
}
