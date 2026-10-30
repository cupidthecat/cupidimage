#include "pdf_icc_lut.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

static unsigned lut_u16(const unsigned char *p) {
    return (unsigned)p[0] * 256 + p[1];
}

static uint32_t lut_u32(const unsigned char *p) {
    return (uint32_t)p[0] * 16777216u + (uint32_t)p[1] * 65536u + (unsigned)p[2] * 256u + p[3];
}

int cupidimage_pdf_icc_lut_parse(pdf_icc_lut *lut, const unsigned char *data, size_t length,
                                 unsigned inputs, int lab) {
    memset(lut, 0, sizeof(*lut));
    if (length < 48 || (inputs != 1 && inputs != 3 && inputs != 4) || lut_u32(data + 4) ||
        data[8] != inputs || data[9] != 3 || data[10] < 2 || data[11])
        return 0;
    unsigned bytes, in, out;
    size_t header;
    if (!memcmp(data, "mft1", 4)) {
        if (!lab)
            return 0; /* ICC.1 does not define an 8-bit PCSXYZ encoding. */
        bytes = 1;
        in = out = 256;
        header = 48;
    } else if (!memcmp(data, "mft2", 4) && length >= 52) {
        bytes = 2;
        in = lut_u16(data + 48);
        out = lut_u16(data + 50);
        header = 52;
        if (in < 2 || in > 4096 || out < 2 || out > 4096)
            return 0;
    } else
        return 0;
    /* These forward transforms have device inputs, never PCSXYZ inputs. */
    for (unsigned i = 0; i < 9; i++)
        if (lut_u32(data + 12 + i * 4) != (i % 4 == 0 ? 65536u : 0u))
            return 0;
    size_t input_bytes = (size_t)in * inputs * bytes;
    size_t output_bytes = (size_t)out * 3 * bytes;
    if (input_bytes > length - header || output_bytes > length - header - input_bytes)
        return 0;
    size_t available = (length - header - input_bytes - output_bytes) / (3 * bytes);
    size_t count = 1;
    for (unsigned i = inputs; i > 0; i--) {
        lut->strides[i - 1] = count;
        if (count > available / data[10])
            return 0;
        count *= data[10];
    }
    size_t expected = header + input_bytes + count * 3 * bytes + output_bytes;
    if (expected > length)
        return 0;
    /* Some producers include trailing padding in the tag extent. The declared
     * tables end here; trailing bytes are neither samples nor extra stages. */
    lut->inputs = inputs;
    lut->bytes = bytes;
    for (unsigned i = 0; i < inputs; i++)
        lut->grids[i] = data[10];
    lut->input_entries = in;
    lut->output_entries = out;
    lut->input = data + header;
    lut->clut = lut->input + input_bytes;
    lut->output = lut->clut + count * 3 * bytes;
    return 1;
}

static double lut_sample(const unsigned char *table, size_t index, unsigned bytes) {
    return bytes == 1 ? table[index] / 255.0 : lut_u16(table + index * 2) / 65535.0;
}

static double lut_table(const unsigned char *table, unsigned count, unsigned bytes, double value) {
    double at = fmax(0, fmin(1, value)) * (count - 1);
    unsigned low = (unsigned)floor(at), high = low + (low + 1 < count);
    double a = lut_sample(table, low, bytes), b = lut_sample(table, high, bytes);
    return a + (b - a) * (at - low);
}

static void lut_three(const pdf_icc_lut *lut, size_t base, const size_t steps[3],
                      const double fraction[3], double out[3]) {
    unsigned order[3] = {0, 1, 2};
    for (unsigned i = 1; i < 3; i++)
        for (unsigned j = i; j > 0 && fraction[order[j]] > fraction[order[j - 1]]; j--) {
            unsigned saved = order[j];
            order[j] = order[j - 1];
            order[j - 1] = saved;
        }
    for (unsigned component = 0; component < 3; component++) {
        size_t index = base;
        double previous = lut_sample(lut->clut, index * 3 + component, lut->bytes);
        double result = previous;
        for (unsigned i = 0; i < 3; i++) {
            unsigned axis = order[i];
            index += steps[axis];
            double next = lut_sample(lut->clut, index * 3 + component, lut->bytes);
            result += fraction[axis] * (next - previous);
            previous = next;
        }
        out[component] = result;
    }
}

int cupidimage_pdf_icc_lut_value(const pdf_icc_lut *lut, const double *values, double out[3]) {
    if (!lut->inputs)
        return 0;
    double fraction[4] = {0};
    size_t base = 0, steps[4] = {0};
    for (unsigned i = 0; i < lut->inputs; i++) {
        if (!isfinite(values[i]))
            return 0;
        double value = fmax(0, fmin(1, values[i]));
        if (lut->input) {
            const unsigned char *table = lut->input + (size_t)i * lut->input_entries * lut->bytes;
            value = lut_table(table, lut->input_entries, lut->bytes, value);
        }
        double at = value * (lut->grids[i] - 1);
        unsigned low = (unsigned)floor(at);
        base += low * lut->strides[i];
        steps[i] = low + 1 < lut->grids[i] ? lut->strides[i] : 0;
        fraction[i] = at - low;
    }
    if (lut->inputs == 1) {
        for (unsigned i = 0; i < 3; i++) {
            double a = lut_sample(lut->clut, base * 3 + i, lut->bytes);
            double b = lut_sample(lut->clut, (base + steps[0]) * 3 + i, lut->bytes);
            out[i] = a + (b - a) * fraction[0];
        }
    } else if (lut->inputs == 3)
        lut_three(lut, base, steps, fraction, out);
    else {
        double other[3];
        lut_three(lut, base, steps + 1, fraction + 1, out);
        lut_three(lut, base + steps[0], steps + 1, fraction + 1, other);
        for (unsigned i = 0; i < 3; i++)
            out[i] += (other[i] - out[i]) * fraction[0];
    }
    for (unsigned i = 0; lut->output && i < 3; i++)
        out[i] = lut_table(lut->output + (size_t)i * lut->output_entries * lut->bytes,
                           lut->output_entries, lut->bytes, out[i]);
    return 1;
}

int cupidimage_pdf_icc_clut_parse(pdf_icc_lut *lut, const unsigned char *data, size_t length,
                                  unsigned inputs) {
    memset(lut, 0, sizeof(*lut));
    if (length < 20 || (inputs != 1 && inputs != 3 && inputs != 4) ||
        (data[16] != 1 && data[16] != 2) || data[17] || data[18] || data[19])
        return 0;
    for (unsigned i = inputs; i < 16; i++)
        if (data[i])
            return 0;
    size_t available = (length - 20) / (3 * data[16]), count = 1;
    for (unsigned i = inputs; i > 0; i--) {
        unsigned grid = data[i - 1];
        if (grid < 2 || count > available / grid)
            return 0;
        lut->strides[i - 1] = count;
        lut->grids[i - 1] = grid;
        count *= grid;
    }
    lut->inputs = inputs;
    lut->bytes = data[16];
    lut->clut = data + 20;
    return 1;
}
