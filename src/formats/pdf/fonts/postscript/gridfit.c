#include "gridfit_internal.h"
#include "type1_gridfit.h"

typedef struct {
    jmp_buf failure;
    struct CF2_FontRec_ font;
    PS_Decoder decoder;
    CF2_ArrStackRec horizontal, vertical;
    CF2_HintMaskRec mask;
    CF2_GlyphPathRec path;
    CF2_OutlineCallbacksRec callbacks;
    pdf_cff_outline *output;
    size_t *work;
    double start_x, start_y;
    int open;
} pdf_ps_gridfit_context;

static void ps_gridfit_fail(PSGF_FT_Memory memory) {
    pdf_ps_gridfit_context *context = memory;
    longjmp(context->failure, 1);
}

static int ps_gridfit_charge(size_t *work, size_t amount) {
    if (*work < amount) {
        *work = 0;
        return 0;
    }
    *work -= amount;
    return 1;
}

static int ps_gridfit_fixed(double value, int32_t *result) {
    if (!isfinite(value) || fabs(value) > 4096)
        return 0;
    *result = (int32_t)round(value * 65536);
    return 1;
}

static double ps_gridfit_pixel(int32_t value) {
    int64_t wide = value;
    int64_t f26 = wide >= 0 ? wide / 1024 : -((-wide + 1023) / 1024);
    return (double)f26 / 64;
}

static void ps_gridfit_emit(pdf_ps_gridfit_context *context, unsigned kind,
                            const double points[6]) {
    pdf_cff_outline *output = context->output;
    if (output->count == 65536 || !ps_gridfit_charge(context->work, 1))
        ps_gridfit_fail(context);
    if (output->count == output->capacity) {
        unsigned capacity = output->capacity ? output->capacity * 2 : 64;
        pdf_cff_segment *next = realloc(output->segments, (size_t)capacity * sizeof(*next));
        if (!next)
            ps_gridfit_fail(context);
        output->segments = next;
        output->capacity = capacity;
    }
    pdf_cff_segment *segment = output->segments + output->count++;
    segment->kind = kind;
    memcpy(segment->points, points, sizeof(segment->points));
}

static void ps_gridfit_close(pdf_ps_gridfit_context *context) {
    if (context->open) {
        double points[6] = {context->start_x, context->start_y};
        ps_gridfit_emit(context, PDF_CFF_CLOSE, points);
        context->open = 0;
    }
}

static void ps_gridfit_callback(CF2_OutlineCallbacks callbacks, const CF2_CallbackParams params) {
    pdf_ps_gridfit_context *context = callbacks->memory;
    if (params->op == CF2_PathOpMoveTo) {
        ps_gridfit_close(context);
        return;
    }
    if (!context->open) {
        double points[6] = {ps_gridfit_pixel(params->pt0.x), ps_gridfit_pixel(params->pt0.y)};
        ps_gridfit_emit(context, PDF_CFF_MOVE, points);
        context->start_x = points[0];
        context->start_y = points[1];
        context->open = 1;
    }
    double points[6] = {ps_gridfit_pixel(params->pt1.x), ps_gridfit_pixel(params->pt1.y)};
    if (params->op == CF2_PathOpLineTo)
        ps_gridfit_emit(context, PDF_CFF_LINE, points);
    else if (params->op == CF2_PathOpCubeTo) {
        points[2] = ps_gridfit_pixel(params->pt2.x);
        points[3] = ps_gridfit_pixel(params->pt2.y);
        points[4] = ps_gridfit_pixel(params->pt3.x);
        points[5] = ps_gridfit_pixel(params->pt3.y);
        ps_gridfit_emit(context, PDF_CFF_CURVE, points);
    } else
        ps_gridfit_fail(context);
}

static void ps_gridfit_begin(pdf_ps_gridfit_context *context,
                             const pdf_ps_private_hints *private_hints,
                             const pdf_ps_hint_program *hints, const pdf_ps_gridfit_size *size,
                             int type1) {
    struct CF2_FontRec_ *font = &context->font;
    font->memory = context;
    font->decoder = &context->decoder;
    font->hinted = 1;
    font->isT1 = type1;
    font->innerTransform.a = size->x_scale;
    font->innerTransform.d = size->y_scale;
    font->outerTransform.a = font->outerTransform.d = 65536;
    PS_Decoder *decoder = &context->decoder;
    for (unsigned i = 0; i < 4; i++) {
        decoder->counts[i] = private_hints->counts[i];
        for (unsigned j = 0; j < private_hints->counts[i]; j++)
            ps_gridfit_fixed(private_hints->arrays[i][j], &decoder->arrays[i][j]);
    }
    ps_gridfit_fixed(private_hints->scalars[0], &decoder->scale);
    ps_gridfit_fixed(floor(private_hints->scalars[1]), &decoder->shift);
    ps_gridfit_fixed(floor(private_hints->scalars[2]), &decoder->fuzz);
    decoder->language = (unsigned)private_hints->scalars[6];
    cf2_blues_init(&font->blues, font);
    cf2_arrstack_init(&context->horizontal, context, &font->error, sizeof(CF2_StemHintRec));
    cf2_arrstack_init(&context->vertical, context, &font->error, sizeof(CF2_StemHintRec));
    for (unsigned i = 0; hints && i < hints->stem_count; i++) {
        const pdf_ps_stem *stem = hints->stems + i;
        CF2_StemHintRec item = {0};
        ps_gridfit_fixed(stem->start, &item.min);
        ps_gridfit_fixed(stem->start + stem->width, &item.max);
        cf2_arrstack_push(stem->vertical ? &context->vertical : &context->horizontal, &item);
    }
    cf2_hintmask_init(&context->mask, &font->error);
    context->callbacks.moveTo = context->callbacks.lineTo = context->callbacks.cubeTo =
        ps_gridfit_callback;
    context->callbacks.error = &font->error;
    context->callbacks.memory = context;
    PSGF_FT_Vector translation = {0};
    cf2_glyphpath_init(&context->path, font, &context->callbacks, size->y_scale,
                       &context->horizontal, &context->vertical, &context->mask, size->origin_y,
                       &font->blues, &translation);
}

static int ps_gridfit_validate(const pdf_ps_private_hints *private_hints,
                               const pdf_ps_hint_program *hints, const pdf_cff_outline *source,
                               const pdf_ps_gridfit_size *size) {
    if (!private_hints || !hints || !source || !size || private_hints->invalid ||
        (hints->diagnostics & ~(PDF_PS_HINT_EMPTY_MASK | PDF_PS_HINT_MASK_PADDING)) ||
        hints->stem_count > 96 || hints->event_count > 65536 ||
        (hints->event_count && !hints->events) || source->composite || source->count > 65536 ||
        (source->count && !source->segments) || size->x_scale <= 0 || size->y_scale <= 0 ||
        size->x_scale > 2 * 65536 || size->y_scale > 2 * 65536 ||
        (int64_t)size->origin_x < -4096 * 65536 || (int64_t)size->origin_x > 4096 * 65536 ||
        (int64_t)size->origin_y < -4096 * 65536 || (int64_t)size->origin_y > 4096 * 65536)
        return 0;
    double origin[2] = {size->origin_x / 65536.0, size->origin_y / 65536.0};
    static const unsigned limits[6] = {14, 10, 14, 10, 12, 12};
    int32_t value;
    for (unsigned i = 0; i < 6; i++) {
        if (private_hints->counts[i] > limits[i] || (i < 4 && private_hints->counts[i] % 2))
            return 0;
        for (unsigned j = 0; j < private_hints->counts[i]; j++)
            if (!ps_gridfit_fixed(private_hints->arrays[i][j], &value))
                return 0;
    }
    for (unsigned i = 0; i < 8; i++)
        if (!ps_gridfit_fixed(private_hints->scalars[i], &value) || private_hints->scalars[i] < 0)
            return 0;
    if (private_hints->scalars[5] > 1 || private_hints->scalars[6] > 1 ||
        floor(private_hints->scalars[5]) != private_hints->scalars[5] ||
        floor(private_hints->scalars[6]) != private_hints->scalars[6])
        return 0;
    unsigned vertical = 0;
    for (unsigned i = 0; i < hints->stem_count; i++) {
        const pdf_ps_stem *stem = hints->stems + i;
        if (stem->vertical > 1 || (vertical && !stem->vertical) ||
            !ps_gridfit_fixed(stem->start, &value) ||
            !ps_gridfit_fixed(stem->start + stem->width, &value) ||
            !ps_gridfit_fixed(stem->start + origin[stem->vertical ? 0 : 1], &value) ||
            !ps_gridfit_fixed(stem->start + stem->width + origin[stem->vertical ? 0 : 1], &value) ||
            !isfinite(stem->width) || (stem->width < 0 && stem->width != -20 && stem->width != -21))
            return 0;
        vertical |= stem->vertical;
    }
    for (unsigned i = 0; i < hints->event_count; i++) {
        const pdf_ps_hint_event *event = hints->events + i;
        /* Empty masks and unused tail bits are retained as diagnosed recovery.
         * The map consumes only the declared stem bits. */
        if (event->stem_count != hints->stem_count || event->counter > 1 ||
            event->segment > source->count || (i && event->segment < hints->events[i - 1].segment))
            return 0;
    }
    int open = 0;
    for (unsigned i = 0; i < source->count; i++) {
        const pdf_cff_segment *segment = source->segments + i;
        if (segment->kind > PDF_CFF_CLOSE || (segment->kind != PDF_CFF_MOVE && !open))
            return 0;
        unsigned count = segment->kind == PDF_CFF_CURVE ? 6 : 2;
        for (unsigned j = 0; j < count; j++)
            if (!ps_gridfit_fixed(segment->points[j] + origin[j % 2], &value))
                return 0;
        open = segment->kind != PDF_CFF_CLOSE;
    }
    return 1;
}

int cupidimage_pdf_ps_gridfit(const pdf_ps_private_hints *private_hints,
                              const pdf_ps_hint_program *hints, const pdf_cff_outline *source,
                              const pdf_ps_gridfit_size *size, pdf_cff_outline *output,
                              size_t *work) {
    if (!output || output == source)
        return 0;
    memset(output, 0, sizeof(*output));
    if (!work || !private_hints || !hints || !source || !size || source->count > 65536 ||
        hints->stem_count > 96 || hints->event_count > 65536)
        return 0;
    size_t admission = 5120 + source->count + hints->event_count + hints->stem_count +
                       (size_t)hints->stem_count * hints->stem_count * 16;
    if (!ps_gridfit_charge(work, admission) ||
        !ps_gridfit_validate(private_hints, hints, source, size))
        return 0;
    pdf_ps_gridfit_context *context = calloc(1, sizeof(*context));
    if (!context)
        return 0;
    context->work = work;
    context->output = output;
    volatile int ok = 0;
    if (setjmp(context->failure))
        goto done;
    ps_gridfit_begin(context, private_hints, hints, size, 0);
    struct CF2_FontRec_ *font = &context->font;
    unsigned event_index = 0;
    for (unsigned i = 0; i <= source->count; i++) {
        while (event_index < hints->event_count && hints->events[event_index].segment == i) {
            if (!ps_gridfit_charge(work, 4096 + (size_t)hints->stem_count * hints->stem_count * 16))
                ps_gridfit_fail(context);
            const pdf_ps_hint_event *event = hints->events + event_index++;
            CF2_HintMaskRec mask;
            cf2_hintmask_init(&mask, &font->error);
            mask.isValid = mask.isNew = 1;
            mask.bitCount = event->stem_count;
            mask.byteCount = (event->stem_count + 7) / 8;
            memcpy(mask.mask, event->mask, sizeof(mask.mask));
            if (event->counter) {
                CF2_HintMapRec map;
                cf2_hintmap_init(&map, font, &context->path.initialHintMap,
                                 &context->path.hintMoves, size->y_scale);
                cf2_hintmap_build(&map, &context->horizontal, &context->vertical, &mask, 0, 0);
            } else
                context->mask = mask;
        }
        if (i == source->count)
            break;
        if (!ps_gridfit_charge(work, 256 + (size_t)hints->stem_count * 32))
            ps_gridfit_fail(context);
        const pdf_cff_segment *segment = source->segments + i;
        int32_t p[6] = {0};
        for (unsigned j = 0; j < (segment->kind == PDF_CFF_CURVE ? 6u : 2u); j++)
            ps_gridfit_fixed(
                segment->points[j] + (j % 2 ? size->origin_y : size->origin_x) / 65536.0, p + j);
        switch (segment->kind) {
            case PDF_CFF_MOVE:
                cf2_glyphpath_moveTo(&context->path, p[0], p[1]);
                break;
            case PDF_CFF_LINE:
                cf2_glyphpath_lineTo(&context->path, p[0], p[1]);
                break;
            case PDF_CFF_CURVE:
                cf2_glyphpath_curveTo(&context->path, p[0], p[1], p[2], p[3], p[4], p[5]);
                break;
            case PDF_CFF_CLOSE:
                cf2_glyphpath_closeOpenPath(&context->path);
                break;
        }
        if (font->error)
            ps_gridfit_fail(context);
    }
    if (!ps_gridfit_charge(work, 256 + (size_t)hints->stem_count * 32))
        ps_gridfit_fail(context);
    cf2_glyphpath_closeOpenPath(&context->path);
    ps_gridfit_close(context);
    output->width = source->width;
    output->width_y = source->width_y;
    ok = !font->error;
done:
    cf2_glyphpath_finalize(&context->path);
    cf2_arrstack_finalize(&context->horizontal);
    cf2_arrstack_finalize(&context->vertical);
    free(context);
    if (!ok)
        cupidimage_pdf_type2_free(output);
    return ok;
}

static void ps_gridfit_type1_stems(pdf_ps_gridfit_context *context,
                                   const pdf_type1_hint_event *event) {
    unsigned count = event->kind == PDF_T1_HINT_STEM3 ? 6 : 2;
    for (unsigned i = 0; i < count; i += 2) {
        CF2_StemHintRec item = {0};
        ps_gridfit_fixed(event->values[i], &item.min);
        ps_gridfit_fixed(event->values[i] + event->values[i + 1], &item.max);
        cf2_arrstack_push(event->vertical ? &context->vertical : &context->horizontal, &item);
    }
}

static void ps_gridfit_type1_end(pdf_ps_gridfit_context *context) {
    CF2_GlyphPath path = &context->path;
    /* Flush the pending element without synthesizing a closing stroke. */
    if (path->elemIsQueued) {
        PSGF_FT_Vector end =
            path->prevElemOp == CF2_PathOpLineTo ? path->prevElemP1 : path->prevElemP3;
        cf2_glyphpath_pushPrevElem(path, &path->hintMap, &end, end, PSGF_FALSE);
    }
    path->moveIsPending = PSGF_TRUE;
    path->pathIsOpen = path->pathIsClosing = path->elemIsQueued = PSGF_FALSE;
    if (context->open) {
        double point[6] = {context->start_x, context->start_y};
        ps_gridfit_emit(context, PDF_CFF_END, point);
        context->open = 0;
    }
}

int cupidimage_pdf_ps_gridfit_type1(const pdf_ps_private_hints *private_hints,
                                    const pdf_type1_hint_trace *trace,
                                    const pdf_cff_outline *source, const pdf_ps_gridfit_size *size,
                                    pdf_cff_outline *output, size_t *work) {
    if (!output || output == source)
        return 0;
    memset(output, 0, sizeof(*output));
    if (!work || !private_hints || !trace || !source || !size || source->count > 65536 ||
        trace->count > 65536 || !ps_gridfit_charge(work, 5120 + source->count + trace->count))
        return 0;
    pdf_ps_private_hints selected;
    pdf_ps_hint_program empty_hints = {0};
    pdf_cff_outline empty_outline = {0};
    unsigned stems = 0;
    if (!cupidimage_pdf_ps_type1_inputs(private_hints, trace, source, size, &selected, &stems) ||
        !ps_gridfit_validate(&selected, &empty_hints, &empty_outline, size) ||
        !ps_gridfit_charge(work, stems + (size_t)stems * stems * 16))
        return 0;
    pdf_ps_gridfit_context *context = calloc(1, sizeof(*context));
    if (!context)
        return 0;
    context->work = work;
    context->output = output;
    volatile int ok = 0;
    if (setjmp(context->failure))
        goto done;
    ps_gridfit_begin(context, &selected, NULL, size, 1);
    /* The first map includes all declarations, across replacement contexts. */
    for (unsigned i = 0; i < trace->count; i++)
        if (trace->events[i].kind <= PDF_T1_HINT_STEM3)
            ps_gridfit_type1_stems(context, trace->events + i);
    cf2_glyphpath_moveTo(&context->path, 0, 0);
    cf2_arrstack_clear(&context->horizontal);
    cf2_arrstack_clear(&context->vertical);
    cf2_hintmask_init(&context->mask, &context->font.error);
    context->mask.isNew = 1;
    unsigned event_index = 0;
    for (unsigned i = 0; i <= source->count; i++) {
        while (event_index < trace->count && trace->events[event_index].segment == i) {
            const pdf_type1_hint_event *event = trace->events + event_index++;
            size_t cost =
                event->kind == PDF_T1_HINT_REPLACE ? 4096 + (size_t)stems * stems * 16 : 64;
            if (!ps_gridfit_charge(work, cost))
                ps_gridfit_fail(context);
            if (event->kind <= PDF_T1_HINT_STEM3)
                ps_gridfit_type1_stems(context, event);
            else if (event->kind == PDF_T1_HINT_REPLACE) {
                cf2_arrstack_clear(&context->horizontal);
                cf2_arrstack_clear(&context->vertical);
                cf2_hintmask_init(&context->mask, &context->font.error);
                context->mask.isNew = 1;
            }
        }
        if (i == source->count)
            break;
        if (!ps_gridfit_charge(work, 256 + (size_t)stems * 32))
            ps_gridfit_fail(context);
        const pdf_cff_segment *segment = source->segments + i;
        int32_t p[6] = {0};
        for (unsigned j = 0; j < (segment->kind == PDF_CFF_CURVE ? 6u : 2u); j++)
            ps_gridfit_fixed(
                segment->points[j] + (j % 2 ? size->origin_y : size->origin_x) / 65536.0, p + j);
        if (segment->kind == PDF_CFF_MOVE)
            cf2_glyphpath_moveTo(&context->path, p[0], p[1]);
        else if (segment->kind == PDF_CFF_LINE)
            cf2_glyphpath_lineTo(&context->path, p[0], p[1]);
        else if (segment->kind == PDF_CFF_CURVE)
            cf2_glyphpath_curveTo(&context->path, p[0], p[1], p[2], p[3], p[4], p[5]);
        else if (segment->kind == PDF_CFF_END)
            ps_gridfit_type1_end(context);
        else if (segment->kind == PDF_CFF_CLOSE) {
            cf2_glyphpath_closeOpenPath(&context->path);
            ps_gridfit_close(context);
        }
        /* SETPOINT changes already appear in subsequent absolute endpoints. */
        if (context->font.error)
            ps_gridfit_fail(context);
    }
    output->width = source->width;
    output->width_y = source->width_y;
    ok = !context->font.error;
done:
    cf2_glyphpath_finalize(&context->path);
    cf2_arrstack_finalize(&context->horizontal);
    cf2_arrstack_finalize(&context->vertical);
    free(context);
    if (!ok)
        cupidimage_pdf_type2_free(output);
    return ok;
}
