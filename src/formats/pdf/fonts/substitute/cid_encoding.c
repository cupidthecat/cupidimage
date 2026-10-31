#include "cid_encoding.h"
#include "../../pdf_internal.h"
#include "../program.h"
#include "cid_data.h"
#include "cp932_data.inc"
#include "cp936_data.inc"
#include "cp949_data.inc"
#include "cp950_data.inc"
#include <string.h>

unsigned cupidimage_pdf_cid_substitute_encoding(const char *name, const char *ordering) {
    static const struct {
        const char *name, *ordering;
        unsigned encoding;
    } legacy[] = {{"90ms-RKSJ", "Japan1", PDF_CID_SUBSTITUTE_CP932},
                  {"GB-EUC", "GB1", PDF_CID_SUBSTITUTE_CP936},
                  {"GBK-EUC", "GB1", PDF_CID_SUBSTITUTE_CP936},
                  {"GBKp-EUC", "GB1", PDF_CID_SUBSTITUTE_CP936},
                  {"ETen-B5", "CNS1", PDF_CID_SUBSTITUTE_CP950},
                  {"ETenms-B5", "CNS1", PDF_CID_SUBSTITUTE_CP950},
                  {"KSCms-UHC", "Korea1", PDF_CID_SUBSTITUTE_CP949},
                  {"KSCms-UHC-HW", "Korea1", PDF_CID_SUBSTITUTE_CP949}};

    for (unsigned i = 0; i < sizeof(legacy) / sizeof(*legacy); i++) {
        size_t length = strlen(legacy[i].name);
        if ((!ordering || !strcmp(ordering, legacy[i].ordering)) &&
            !strncmp(name, legacy[i].name, length) &&
            (!strcmp(name + length, "-H") || !strcmp(name + length, "-V")))
            return legacy[i].encoding;
    }

    static const struct {
        const char *prefix, *ordering;
    } collections[] = {
        {"UniJIS", "Japan1"}, {"UniGB", "GB1"}, {"UniCNS", "CNS1"}, {"UniKS", "Korea1"}};

    for (unsigned i = 0; i < sizeof(collections) / sizeof(*collections); i++) {
        size_t prefix = strlen(collections[i].prefix);
        if ((ordering && strcmp(ordering, collections[i].ordering)) ||
            strncmp(name, collections[i].prefix, prefix))
            continue;
        const char *suffix = name + prefix;
        if (!strcmp(suffix, "-UCS2-H") || !strcmp(suffix, "-UCS2-V"))
            return PDF_CID_SUBSTITUTE_UCS2;
        if (!strcmp(suffix, "-UTF16-H") || !strcmp(suffix, "-UTF16-V"))
            return PDF_CID_SUBSTITUTE_UTF16;
    }
    return PDF_CID_SUBSTITUTE_NONE;
}

static uint32_t cid_encoding_u32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

int cupidimage_pdf_cid_substitute_cmap(pdf_font_program *program) {
    const unsigned char *data = program->bytes;
    size_t length = program->length;
    if (length < 12 || memcmp(data, "OTTO", 4))
        return 0;
    unsigned count = (unsigned)data[4] * 256 + data[5];
    if (count > (length - 12) / 16)
        return 0;
    for (unsigned i = 0; i < count; i++) {
        const unsigned char *entry = data + 12 + i * 16;
        if (memcmp(entry, "cmap", 4))
            continue;
        uint32_t offset = cid_encoding_u32(entry + 8), bytes = cid_encoding_u32(entry + 12);
        if (offset > length || bytes > length - offset)
            return 0;
        program->font.cmap = (pdf_tt_table){data + offset, bytes};
        program->font.glyphs = program->cff.chars.count;
        return 1;
    }
    return 0;
}

static unsigned cid_vertical_glyph(const pdf_cid_font_source *source, unsigned glyph) {
    size_t lo = 0, hi = source->vertical_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (source->vertical[mid][0] == glyph)
            return source->vertical[mid][1];
        if (source->vertical[mid][0] < glyph)
            lo = mid + 1;
        else
            hi = mid;
    }
    return glyph;
}

unsigned cupidimage_pdf_cid_substitute_glyph(const pdf_font_program *program, unsigned encoding,
                                             uint32_t code, size_t bytes, int vertical) {
    uint32_t scalar = code;
    const uint16_t (*mapping)[2] = NULL;
    size_t count = 0;
    switch (encoding) {
        case PDF_CID_SUBSTITUTE_CP932:
            mapping = cid_cp932;
            count = sizeof(cid_cp932) / sizeof(*cid_cp932);
            break;
        case PDF_CID_SUBSTITUTE_CP936:
            mapping = cid_cp936;
            count = sizeof(cid_cp936) / sizeof(*cid_cp936);
            break;
        case PDF_CID_SUBSTITUTE_CP949:
            mapping = cid_cp949;
            count = sizeof(cid_cp949) / sizeof(*cid_cp949);
            break;
        case PDF_CID_SUBSTITUTE_CP950:
            mapping = cid_cp950;
            count = sizeof(cid_cp950) / sizeof(*cid_cp950);
            break;
    }
    if (mapping) {
        if ((bytes != 1 && bytes != 2) || (bytes == 1 && code > 255) ||
            (bytes == 2 && code < 256) || code > 65535)
            return 0;
        size_t lo = 0, hi = count;
        scalar = 0xffffffffu;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (mapping[mid][0] == code) {
                scalar = mapping[mid][1];
                break;
            }
            if (mapping[mid][0] < code)
                lo = mid + 1;
            else
                hi = mid;
        }
    } else if (encoding == PDF_CID_SUBSTITUTE_UTF16 && bytes == 4) {
        uint32_t high = code >> 16, low = code & 65535;
        if (high < 0xd800 || high > 0xdbff || low < 0xdc00 || low > 0xdfff)
            return 0;
        scalar = 0x10000 + ((high - 0xd800) << 10) + low - 0xdc00;
    } else if ((encoding != PDF_CID_SUBSTITUTE_UTF16 && encoding != PDF_CID_SUBSTITUTE_UCS2) ||
               bytes != 2 || code > 65535)
        return 0;
    if (scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff))
        return 0;
    unsigned glyph = cupidimage_pdf_tt_map(&program->font, scalar, 3, 10);
    if (!glyph)
        glyph = cupidimage_pdf_tt_map(&program->font, scalar, 3, 1);
    if (!glyph)
        glyph = cupidimage_pdf_tt_map(&program->font, scalar, 0, -1);
    return vertical && program->cid_substitute ? cid_vertical_glyph(program->cid_substitute, glyph)
                                               : glyph;
}
