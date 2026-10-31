#include "monochrome_engine.h"
#include <setjmp.h>
#include <string.h>

#define STANDALONE_
#undef FT_STATIC_RASTER
#define CUPIDIMAGE_MONO_TICK(worker)                                                               \
    (*(worker)->work_left ? ((*(worker)->work_left)--, (void)0) : longjmp(*(worker)->abort, 1))
#include "third_party/freetype-monochrome/ftraster.inc"

int cupidimage_pdf_monochrome_engine(const FT_Outline *outline, unsigned char *bitmap,
                                     unsigned width, unsigned height, long *pool, size_t pool_words,
                                     size_t *work) {
    black_TWorker storage = {0};
    black_PWorker worker = &storage;
    jmp_buf abort;
    worker->outline = *outline;
    worker->bTop = (Int)height - 1;
    worker->bRight = (Int)width - 1;
    worker->bPitch = (Int)((width + 7) / 8);
    worker->bOrigin = bitmap + (height - 1) * (size_t)worker->bPitch;
    worker->buff = pool;
    worker->sizeBuff = pool + pool_words;
    worker->work_left = work;
    worker->abort = &abort;
    if (setjmp(abort))
        return 0;
    return Render_Glyph(worker) == 0;
}
