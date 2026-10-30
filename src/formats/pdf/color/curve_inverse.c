#include "curve_inverse.h"
#include <math.h>
#include <string.h>

static double inverse_sample(const pdf_icc_curve *curve, unsigned index) {
    const unsigned char *p = curve->samples + (size_t)index * 2;
    return ((unsigned)p[0] * 256 + p[1]) / 65535.0;
}

static double inverse_clip(double x) {
    return fmax(0, fmin(1, x));
}

static int inverse_piece(pdf_icc_inverse_curve *inverse, double first, double last, int power,
                         double gamma, double a, double b, double offset) {
    if (first > last || inverse->piece_count == 2)
        return 0;
    pdf_icc_inverse_piece p = {first, last, 0, 0, gamma, a, b, offset, power};
    double lo = a * first + b, hi = a * last + b;
    if (power) {
        if (lo < 0 || hi < 0)
            return 0;
        lo = pow(lo, gamma);
        hi = pow(hi, gamma);
    }
    lo += offset;
    hi += offset;
    if (!isfinite(lo) || !isfinite(hi))
        return 0;
    p.low = inverse_clip(lo);
    p.high = inverse_clip(hi);
    inverse->pieces[inverse->piece_count++] = p;
    return 1;
}

int cupidimage_pdf_icc_inverse_prepare(const pdf_icc_curve *curve, pdf_icc_inverse_curve *inverse,
                                       size_t *work) {
    if (!inverse)
        return 0;
    pdf_icc_curve input = {0};
    if (curve)
        input = *curve;
    memset(inverse, 0, sizeof(*inverse));
    if (!curve || !work || input.count > 65536 ||
        (input.parametric != 0 && input.parametric != 1) ||
        (!input.parametric && input.count > 1 && !input.samples))
        return 0;
    size_t cost = 16 + (input.parametric ? 0 : input.count);
    if (*work < cost) {
        *work = 0;
        return 0;
    }
    *work -= cost;
    inverse->curve = input;
    if (!input.parametric && input.count > 1) {
        double first = inverse_sample(&input, 0), last = inverse_sample(&input, input.count - 1);
        if (first == last)
            return 0;
        inverse->direction = first < last ? 1 : -1;
        double previous = first;
        for (unsigned i = 1; i < input.count; i++) {
            double value = inverse_sample(&input, i);
            if ((value - previous) * inverse->direction < 0)
                return 0;
            previous = value;
        }
        inverse->low = first;
        inverse->high = last;
    } else {
        const double *p = input.parameters;
        if (!input.parametric) {
            double gamma = input.count ? p[0] : 1;
            if (!isfinite(gamma) || gamma <= 0 || !inverse_piece(inverse, 0, 1, 1, gamma, 1, 0, 0))
                return 0;
        } else {
            static const unsigned counts[5] = {1, 3, 4, 5, 7};
            if (input.function > 4)
                return 0;
            for (unsigned i = 0; i < counts[input.function]; i++)
                if (!isfinite(p[i]))
                    return 0;
            if (p[0] <= 0 || (input.function && p[1] <= 0))
                return 0;
            if (!input.function) {
                if (!inverse_piece(inverse, 0, 1, 1, p[0], 1, 0, 0))
                    return 0;
            } else {
                double split = input.function <= 2 ? -p[2] / p[1] : p[4];
                double lower_slope = input.function <= 2 ? 0 : p[3];
                double lower_offset = input.function == 2 ? p[3] : input.function == 4 ? p[6] : 0;
                double upper_offset = input.function == 2 ? p[3] : input.function == 4 ? p[5] : 0;
                if (!isfinite(split))
                    return 0;
                if (split > 0 && !inverse_piece(inverse, 0, split > 1 ? 1 : nextafter(split, 0), 0,
                                                1, lower_slope, 0, lower_offset))
                    return 0;
                if (split <= 1 &&
                    !inverse_piece(inverse, fmax(0, split), 1, 1, p[0], p[1], p[2], upper_offset))
                    return 0;
            }
        }
        if (!inverse->piece_count)
            return 0;
        inverse->low = inverse->pieces[0].low;
        inverse->high = inverse->pieces[inverse->piece_count - 1].high;
        if (inverse->low == inverse->high)
            return 0;
        inverse->direction = inverse->low < inverse->high ? 1 : -1;
        double previous = inverse->low;
        for (unsigned i = 0; i < inverse->piece_count; i++) {
            const pdf_icc_inverse_piece *piece = inverse->pieces + i;
            if ((piece->low - previous) * inverse->direction < 0 ||
                (piece->high - piece->low) * inverse->direction < 0)
                return 0;
            previous = piece->high;
        }
    }
    inverse->valid = 1;
    return 1;
}

static int inverse_piece_range(const pdf_icc_inverse_piece *p, double y, double *first,
                               double *last) {
    if (y < fmin(p->low, p->high) || y > fmax(p->low, p->high))
        return 0;
    *first = p->first;
    *last = p->last;
    if (p->low == p->high)
        return 1;
    double root = y - p->offset;
    if (p->power) {
        if (root < 0)
            return 0;
        root = pow(root, 1 / p->gamma);
    }
    root = (root - p->b) / p->a;
    if (!isfinite(root))
        return -1;
    root = fmax(p->first, fmin(p->last, root));
    double check = p->a * root + p->b;
    if (p->power)
        check = pow(check, p->gamma);
    check = inverse_clip(check + p->offset);
    if (!isfinite(check) || fabs(check - y) > 1e-9)
        return -1;
    if (y == 0) {
        if (p->high > p->low)
            *last = root;
        else
            *first = root;
    } else if (y == 1) {
        if (p->high > p->low)
            *first = root;
        else
            *last = root;
    } else
        *first = *last = root;
    return 1;
}

int cupidimage_pdf_icc_inverse_value(const pdf_icc_inverse_curve *inverse, double input,
                                     double *output) {
    if (!inverse || !inverse->valid || !output || !isfinite(input))
        return 0;
    double y =
        fmax(fmin(inverse->low, inverse->high), fmin(fmax(inverse->low, inverse->high), input));
    const pdf_icc_curve *curve = &inverse->curve;
    if (!curve->parametric && curve->count > 1) {
        unsigned low = 0, high = curve->count;
        double target = y * inverse->direction;
        while (low < high) {
            unsigned middle = low + (high - low) / 2;
            if (inverse_sample(curve, middle) * inverse->direction < target)
                low = middle + 1;
            else
                high = middle;
        }
        unsigned first = low;
        double value = inverse_sample(curve, first);
        if (value == y) {
            high = curve->count;
            while (low < high) {
                unsigned middle = low + (high - low) / 2;
                if (inverse_sample(curve, middle) * inverse->direction <= target)
                    low = middle + 1;
                else
                    high = middle;
            }
            unsigned last = low - 1;
            *output = (double)(last == curve->count - 1 ? first : last) / (curve->count - 1);
        } else {
            if (!first)
                return 0;
            double previous = inverse_sample(curve, first - 1);
            *output = (first - 1 + (y - previous) / (value - previous)) / (curve->count - 1);
        }
        return 1;
    }
    for (unsigned pass = 0; pass < 2; pass++) {
        double first = INFINITY, last = -INFINITY, nearest = 0, distance = INFINITY;
        for (unsigned i = 0; i < inverse->piece_count; i++) {
            const pdf_icc_inverse_piece *p = inverse->pieces + i;
            double a, b;
            int found = inverse_piece_range(p, y, &a, &b);
            if (found < 0)
                return 0;
            if (found) {
                first = fmin(first, a);
                last = fmax(last, b);
            }
            if (fabs(y - p->low) < distance) {
                nearest = p->low;
                distance = fabs(y - p->low);
            }
            if (fabs(y - p->high) < distance) {
                nearest = p->high;
                distance = fabs(y - p->high);
            }
        }
        if (isfinite(first)) {
            *output = last == 1 ? first : last;
            return 1;
        }
        y = nearest;
    }
    return 0;
}
