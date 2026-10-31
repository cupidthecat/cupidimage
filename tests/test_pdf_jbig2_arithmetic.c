/* The independently implemented MQ encoder is used only by this test executable. */
#define OPJ_SKIP_POISON
#include "formats/pdf/images/jbig2/internal.h"
#include "third_party/openjpeg/prefix.h"
/* Prefixes must be defined before the independent encoder declarations. */
#include "third_party/openjpeg/mqc.inc"

typedef struct {
    unsigned context, value;
} decision;

static unsigned checks;

static int encode(const decision *events, unsigned count, unsigned contexts, unsigned initial,
                  unsigned char **output, size_t *length, unsigned char *final_states) {
    *output = NULL;
    *length = 0;
    if (!contexts || contexts > 131072 || count > 1048576 || initial >= 94)
        return 0;
    unsigned char *buffer = calloc((size_t)count * 4 + 65, 1);
    const opj_mqc_state_t **states = calloc(contexts, sizeof(*states));
    if (!buffer || !states) {
        free(buffer);
        free(states);
        return 0;
    }
    opj_mqc_t coder = {0};
    opj_mqc_resetstates(&coder);
    opj_mqc_setstate(&coder, 0, initial & 1, initial >> 1);
    for (unsigned i = 0; i < contexts; i++)
        states[i] = coder.ctxs[0];
    opj_mqc_init_enc(&coder, buffer + 1);
    for (unsigned i = 0; i < count; i++) {
        if (events[i].context >= contexts || events[i].value > 1) {
            free(buffer);
            free(states);
            return 0;
        }
        coder.curctx = states + events[i].context;
        opj_mqc_encode_macro((&coder), coder.curctx, coder.a, coder.c, coder.ct, events[i].value);
    }
    opj_mqc_flush(&coder);
    unsigned bytes = opj_mqc_numbytes(&coder);
    assert(bytes <= count * 4 + 60);
    buffer[1 + bytes++] = 255;
    buffer[1 + bytes++] = 172;
    memmove(buffer, buffer + 1, bytes);
    if (final_states)
        for (unsigned i = 0; i < contexts; i++)
            final_states[i] = (unsigned char)(states[i] - mqc_states);
    free(states);
    *output = buffer;
    *length = bytes;
    return 1;
}

static unsigned random_value(unsigned *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void bit_tests(void) {
    unsigned random = 3200088;
    for (unsigned trial = 0; trial < 294; trial++) {
        unsigned contexts = trial < 94 ? 1 : (trial % 5 == 0 ? 512 : 1 + trial % 17);
        unsigned initial = trial < 94 ? trial : 0;
        decision events[512];
        for (unsigned i = 0; i < 512; i++) {
            events[i].context = random_value(&random) % contexts;
            events[i].value = (random_value(&random) >> (trial % 24)) & 1;
        }
        unsigned char *encoded, expected_states[512], states[512];
        size_t length;
        assert(encode(events, 512, contexts, initial, &encoded, &length, expected_states));
        memset(states, (int)initial, sizeof(states));
        size_t work = 100000;
        pdf_jb2_context context;
        assert(cupidimage_pdf_jb2_context_init(&context, &work, 10000));
        pdf_jb2_arithmetic decoder;
        assert(cupidimage_pdf_jb2_arithmetic_init(&decoder, &context, encoded, length));
        for (unsigned i = 0; i < 512; i++) {
            unsigned value;
            assert(cupidimage_pdf_jb2_arithmetic_bit(&decoder, states + events[i].context, &value));
            assert(value == events[i].value);
            checks++;
        }
        assert(!memcmp(states, expected_states, contexts));
        free(encoded);
        cupidimage_pdf_jb2_clear(&context);
    }
}

static void integer_event(decision *events, unsigned *count, unsigned *previous, unsigned bit) {
    events[(*count)++] = (decision){*previous, bit};
    *previous = (*previous << 1) | bit;
    if (*previous >= 512)
        *previous = (*previous & 511) | 256;
}

static void integer_tests(void) {
    const int64_t values[] = {
        0,  1,   -1,  3,    -3,  4,    -4,   19,    -19,  20,    -20,          83,           -83,
        84, -84, 339, -339, 340, -340, 4435, -4435, 4436, -4436, 4294971731LL, -4294971731LL};
    const unsigned widths[] = {2, 4, 6, 8, 12, 32}, offsets[] = {0, 4, 20, 84, 340, 4436};
    decision events[8192];
    unsigned count = 0;
    for (unsigned round = 0; round < 4; round++)
        for (unsigned i = 0; i <= sizeof(values) / sizeof(*values); i++) {
            int oob = i == sizeof(values) / sizeof(*values);
            int64_t value = oob ? 0 : values[i];
            unsigned sign = value < 0 || oob, level = 0, previous = 1;
            uint64_t magnitude = value < 0 ? (uint64_t)-value : (uint64_t)value;
            while (level < 5 && magnitude >= offsets[level + 1])
                level++;
            integer_event(events, &count, &previous, sign);
            for (unsigned j = 0; j < level; j++)
                integer_event(events, &count, &previous, 1);
            if (level < 5)
                integer_event(events, &count, &previous, 0);
            magnitude -= offsets[level];
            for (unsigned bit = widths[level]; bit; bit--)
                integer_event(events, &count, &previous, (unsigned)(magnitude >> (bit - 1)) & 1);
        }
    unsigned char *encoded, states[512] = {0};
    size_t length;
    assert(count <= 8192 && encode(events, count, 512, 0, &encoded, &length, NULL));
    size_t work = 1000000;
    pdf_jb2_context context;
    pdf_jb2_arithmetic decoder;
    assert(cupidimage_pdf_jb2_context_init(&context, &work, 10000));
    assert(cupidimage_pdf_jb2_arithmetic_init(&decoder, &context, encoded, length));
    for (unsigned round = 0; round < 4; round++)
        for (unsigned i = 0; i <= sizeof(values) / sizeof(*values); i++) {
            int64_t value;
            int oob;
            assert(cupidimage_pdf_jb2_arithmetic_integer(&decoder, states, &value, &oob));
            assert(oob == (i == sizeof(values) / sizeof(*values)));
            if (!oob)
                assert(value == values[i]);
            checks++;
        }
    free(encoded);
    cupidimage_pdf_jb2_clear(&context);
}

static void identifier_tests(void) {
    unsigned random = 14492;
    for (unsigned bits = 0; bits <= 16; bits++) {
        decision events[1024];
        unsigned count = 0, values[64];
        unsigned contexts = 1u << bits;
        for (unsigned i = 0; i < 64; i++) {
            values[i] = random_value(&random) & (contexts - 1);
            unsigned previous = 1;
            for (unsigned bit = bits; bit; bit--) {
                unsigned value = (values[i] >> (bit - 1)) & 1;
                events[count++] = (decision){previous, value};
                previous = (previous << 1) | value;
            }
        }
        unsigned char *encoded, *states = calloc(contexts, 1);
        size_t length;
        assert(states && encode(events, count, contexts, 0, &encoded, &length, NULL));
        size_t work = 100000;
        pdf_jb2_context context;
        pdf_jb2_arithmetic decoder;
        assert(cupidimage_pdf_jb2_context_init(&context, &work, 10000));
        assert(cupidimage_pdf_jb2_arithmetic_init(&decoder, &context, encoded, length));
        for (unsigned i = 0; i < 64; i++) {
            uint32_t value;
            assert(cupidimage_pdf_jb2_arithmetic_id(&decoder, states, contexts, bits, &value));
            assert(value == values[i]);
            checks++;
        }
        free(states);
        free(encoded);
        cupidimage_pdf_jb2_clear(&context);
    }
}

int main(int argc, char **argv) {
    if (argc == 2 && (!strcmp(argv[1], "--encode") || !strcmp(argv[1], "--batch"))) {
        int batch = !strcmp(argv[1], "--batch");
        do {
            unsigned contexts, count;
            int fields = scanf("%u %u", &contexts, &count);
            if (batch && fields == EOF)
                return 0;
            if (fields != 2 || count > 1048576)
                return 2;
            decision *events = calloc(count ? count : 1, sizeof(*events));
            if (!events)
                return 2;
            for (unsigned i = 0; i < count; i++)
                if (scanf("%u %u", &events[i].context, &events[i].value) != 2) {
                    free(events);
                    return 2;
                }
            unsigned char *data;
            size_t length;
            int ok = encode(events, count, contexts, 0, &data, &length, NULL);
            free(events);
            if (!ok)
                return 3;
            if (batch) {
                unsigned char header[4] = {(unsigned char)(length >> 24),
                                           (unsigned char)(length >> 16),
                                           (unsigned char)(length >> 8), (unsigned char)length};
                ok = fwrite(header, 1, 4, stdout) == 4;
            }
            ok = ok && fwrite(data, 1, length, stdout) == length;
            free(data);
            if (!ok)
                return 3;
        } while (batch);
        return 0;
    }
    bit_tests();
    integer_tests();
    identifier_tests();
    printf("JBIG2 arithmetic: %u independent encoder/state/integer/identifier checks\n", checks);
    return 0;
}
