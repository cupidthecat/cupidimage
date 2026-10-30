#include "coverage.h"
#include <limits.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#define STANDALONE_
#define FT_INT64
#define FT_Int64 int64_t
#define FT_UInt64 uint64_t
#define FT_UInt unsigned
#include "../../../../third_party/freetype-raster/ftgrays.inc"

typedef struct {
    gray_TWorker worker;
    jmp_buf abort;
    unsigned char *pixels;
    unsigned width;
} coverage_context;

static void coverage_spans(int y, int count, const FT_Span *spans, void *user) {
    coverage_context *ctx = user;
    for (int i = 0; i < count; i++)
        memset(ctx->pixels + (size_t)y * ctx->width + spans[i].x, spans[i].coverage, spans[i].len);
}

static int coverage_run(coverage_context *ctx) {
    if (setjmp(ctx->abort))
        return 0;
    return gray_convert_glyph(&ctx->worker) == 0;
}

int cupidimage_pdf_coverage(const pdf_coverage_point *points, unsigned count, unsigned width,
                            unsigned height, int even_odd, size_t *work_left,
                            unsigned char **output) {
    if (!output)
        return 0;
    *output = NULL;
    if (!work_left || (count && !points) || count > 65535 || width > 32767 || height > 32767 ||
        (even_odd != 0 && even_odd != 1))
        return 0;
    size_t pixels = (size_t)width * height;
    if (pixels > 16 * 1024 * 1024 || pixels > *work_left || count > *work_left - pixels)
        return 0;
    *work_left -= pixels + count;
    unsigned contours = 0;
    for (unsigned i = 0; i < count; i++) {
        if (points[i].x < -2097152 || points[i].x > 2097152 || points[i].y < -2097152 ||
            points[i].y > 2097152 || points[i].tag > 2 || points[i].end > 1)
            return 0;
        contours += points[i].end;
    }
    if (count && !points[count - 1].end)
        return 0;
    if (!pixels)
        return 1;
    coverage_context *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return 0;
    int ok = 0;
    ctx->pixels = calloc(pixels, 1);
    if (!ctx->pixels)
        goto done;
    if (!count) {
        ok = 1;
        goto done;
    }
    gray_PWorker w = &ctx->worker;
    w->outline.points = malloc(((size_t)count + 1) * sizeof(FT_Vector));
    w->outline.tags = malloc((size_t)count + 1);
    w->outline.contours = malloc((size_t)contours * sizeof(unsigned short));
    size_t cells = ((size_t)width + height) * 10 + 1024;
    w->buffer = calloc(cells, sizeof(TCell));
    if (!w->outline.points || !w->outline.tags || !w->outline.contours || !w->buffer)
        goto done;
    /* The outline walker temporarily forms a pointer before the first point. */
    w->outline.points++;
    w->outline.tags++;
    for (unsigned i = 0, c = 0; i < count; i++) {
        w->outline.points[i] = (FT_Vector){points[i].x, points[i].y};
        w->outline.tags[i] = points[i].tag;
        if (points[i].end)
            w->outline.contours[c++] = (unsigned short)i;
    }
    w->outline.n_points = (unsigned short)count;
    w->outline.n_contours = (unsigned short)contours;
    w->outline.flags = even_odd ? FT_OUTLINE_EVEN_ODD_FILL : 0;
    w->cell_null = w->buffer + cells - 1;
    w->max_ex = (int)width;
    w->max_ey = (int)height;
    w->render_span = coverage_spans;
    w->render_span_data = ctx;
    w->work_left = work_left;
    w->abort = &ctx->abort;
    ctx->width = width;
    ok = coverage_run(ctx);
    w->outline.points--;
    w->outline.tags--;
done:
    if (ok) {
        *output = ctx->pixels;
        ctx->pixels = NULL;
    }
    free(ctx->worker.outline.points);
    free(ctx->worker.outline.tags);
    free(ctx->worker.outline.contours);
    free(ctx->worker.buffer);
    free(ctx->pixels);
    free(ctx);
    return ok;
}
