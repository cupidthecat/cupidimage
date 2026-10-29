#include "fixtures/pdf-cff.inc"
#include "fixtures/pdf-truetype.inc"
#include "fixtures/pdf-type1.inc"
#include "formats/pdf/fonts/program.h"
#include "formats/pdf/fonts/standard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void release_outline(pdf_tt_outline *tt, pdf_cff_outline *ps, int borrowed) {
    if (!borrowed) {
        cupidimage_pdf_tt_outline_free(tt);
        cupidimage_pdf_type2_free(ps);
    }
}

static void same_outline(const pdf_tt_outline *a, const pdf_tt_outline *b, const pdf_cff_outline *c,
                         const pdf_cff_outline *d) {
    assert(a->count == b->count && c->count == d->count);
    assert(a->advance == b->advance);
    assert(c->width == d->width && c->width_y == d->width_y);
    for (unsigned i = 0; i < a->count; i++) {
        assert(a->points[i].x == b->points[i].x && a->points[i].y == b->points[i].y);
        assert(a->points[i].on == b->points[i].on && a->points[i].end == b->points[i].end);
    }
    for (unsigned i = 0; i < c->count; i++) {
        assert(c->segments[i].kind == d->segments[i].kind);
        for (unsigned j = 0; j < 6; j++)
            assert(c->segments[i].points[j] == d->segments[i].points[j]);
    }
}

static unsigned check_program(struct pdf_font_program *program, unsigned glyph) {
    pdf_tt_outline uncached = {0}, cached = {0}, again = {0};
    pdf_cff_outline ps_uncached = {0}, ps_cached = {0}, ps_again = {0};
    int borrowed = 0;
    size_t empty = 0, budget = 1024u * 1024u;
    assert(cupidimage_pdf_font_outline(program, glyph, &uncached, &ps_uncached, &empty, &borrowed));
    assert(!borrowed && (uncached.count || ps_uncached.count));
    assert(cupidimage_pdf_font_outline(program, glyph, &cached, &ps_cached, &budget, &borrowed));
    assert(borrowed && budget < 1024u * 1024u);
    same_outline(&uncached, &cached, &ps_uncached, &ps_cached);
    size_t remaining = budget;
    for (unsigned i = 0; i < 32; i++) {
        assert(cupidimage_pdf_font_outline(program, glyph, &again, &ps_again, &budget, &borrowed));
        assert(borrowed && budget == remaining);
        assert(again.points == cached.points && ps_again.segments == ps_cached.segments);
        same_outline(&uncached, &again, &ps_uncached, &ps_again);
    }
    release_outline(&uncached, &ps_uncached, 0);
    cupidimage_pdf_font_outline_cache_clear(program);
    assert(program->outline_cache_next == 0);
    for (unsigned i = 0; i < PDF_FONT_OUTLINE_CACHE_SLOTS; i++)
        assert(!program->outline_cache[i].valid);
    return 35;
}

int main(void) {
    struct pdf_font_program tt = {0}, cff = {0}, type1 = {0};
    assert(cupidimage_pdf_tt_init(&tt.font, test_truetype_font, sizeof(test_truetype_font)));
    cff.is_cff = 1;
    assert(cupidimage_pdf_cff_init(&cff.cff, test_cff_font, sizeof(test_cff_font)));
    type1.is_type1 = 1;
    assert(cupidimage_pdf_type1_init(&type1.type1, test_type1_font, sizeof(test_type1_font),
                                     test_type1_font_clear, test_type1_font_encrypted));
    unsigned checks = check_program(&tt, 1) + check_program(&cff, 2) +
                      check_program(&type1, cupidimage_pdf_type1_name(&type1.type1, "A"));
    cupidimage_pdf_cff_free(&cff.cff);
    cupidimage_pdf_type1_free(&type1.type1);

    struct pdf_font_program second = {0};
    assert(cupidimage_pdf_tt_init(&second.font, test_truetype_font, sizeof(test_truetype_font)));
    pdf_tt_outline first_outline = {0}, second_outline = {0};
    pdf_cff_outline first_ps = {0}, second_ps = {0};
    size_t budget = 1024u * 1024u;
    int borrowed;
    assert(cupidimage_pdf_font_outline(&tt, 1, &first_outline, &first_ps, &budget, &borrowed));
    assert(
        cupidimage_pdf_font_outline(&second, 1, &second_outline, &second_ps, &budget, &borrowed));
    assert(first_outline.points != second_outline.points);
    cupidimage_pdf_font_outline_cache_clear(&tt);
    assert(second_outline.points[0].x == 0);
    cupidimage_pdf_font_outline_cache_clear(&second);
    checks++;

    struct pdf_font_program inherited = {0};
    assert(cupidimage_pdf_tt_init(&inherited.font, test_truetype_font, sizeof(test_truetype_font)));
    unsigned char metrics[32] = {0};
    for (unsigned i = 0; i < 8; i++) {
        metrics[i * 4] = 3;
        metrics[i * 4 + 1] = 232;
    }
    metrics[4] = 2;
    metrics[5] = 0;
    metrics[16] = 2;
    metrics[17] = 188;
    inherited.font.hmetrics = 8;
    inherited.font.hmtx = (pdf_tt_table){metrics, sizeof(metrics)};
    unsigned char glyph_data[512];
    assert(inherited.font.glyf.length <= sizeof(glyph_data));
    memcpy(glyph_data, inherited.font.glyf.data, inherited.font.glyf.length);
    inherited.font.glyf.data = glyph_data;
    const unsigned char *location = inherited.font.loca.data + 4 * 2;
    size_t start = ((unsigned)location[0] * 256 + location[1]) * 2;
    glyph_data[start + 10] |= 2;
    checks += check_program(&inherited, 4);
    pdf_tt_outline inherited_outline = {0};
    pdf_cff_outline unused = {0};
    budget = 1024u * 1024u;
    assert(cupidimage_pdf_font_outline(&inherited, 4, &inherited_outline, &unused, &budget,
                                       &borrowed));
    assert(borrowed && inherited_outline.advance == 512);
    assert(cupidimage_pdf_tt_advance(&inherited.font, 4) == 700);
    cupidimage_pdf_font_outline_cache_clear(&inherited);
    checks++;

    struct pdf_font_program standard = {0};
    const pdf_standard_outline *font = cupidimage_pdf_standard_outline("Helvetica");
    standard.is_cff = 1;
    assert(font && cupidimage_pdf_cff_init(&standard.cff, font->data, font->length));
    unsigned first = 0;
    for (unsigned i = 0; i <= PDF_FONT_OUTLINE_CACHE_SLOTS; i++) {
        char name[2] = {(char)('A' + i), 0};
        unsigned glyph = cupidimage_pdf_cff_name(&standard.cff, name);
        if (!i)
            first = glyph;
        pdf_tt_outline outline = {0};
        pdf_cff_outline ps = {0};
        assert(cupidimage_pdf_font_outline(&standard, glyph, &outline, &ps, &budget, &borrowed));
        assert(borrowed && ps.count);
        checks++;
    }
    for (unsigned i = 0; i < PDF_FONT_OUTLINE_CACHE_SLOTS; i++)
        assert(!standard.outline_cache[i].valid || standard.outline_cache[i].glyph != first);
    size_t before = budget;
    pdf_tt_outline outline = {0};
    pdf_cff_outline ps = {0};
    assert(cupidimage_pdf_font_outline(&standard, first, &outline, &ps, &budget, &borrowed));
    assert(borrowed && budget < before);
    size_t exhausted = 0;
    unsigned uncached_glyph = cupidimage_pdf_cff_name(&standard.cff, "Z");
    pdf_tt_outline uncached_tt = {0};
    pdf_cff_outline uncached_ps = {0};
    assert(cupidimage_pdf_font_outline(&standard, uncached_glyph, &uncached_tt, &uncached_ps,
                                       &exhausted, &borrowed));
    assert(!borrowed && !exhausted && uncached_ps.count);
    release_outline(&uncached_tt, &uncached_ps, borrowed);
    pdf_tt_outline retained_tt = {0};
    pdf_cff_outline retained_ps = {0};
    assert(cupidimage_pdf_font_outline(&standard, first, &retained_tt, &retained_ps, &exhausted,
                                       &borrowed));
    assert(borrowed && !exhausted && retained_ps.segments == ps.segments);
    checks += 2;
    cupidimage_pdf_font_outline_cache_clear(&standard);
    cupidimage_pdf_cff_free(&standard.cff);
    checks++;
    printf("PDF font outline cache: %u checks passed\n", checks);
    return 0;
}
