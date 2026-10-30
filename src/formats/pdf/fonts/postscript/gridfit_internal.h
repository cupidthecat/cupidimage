/****************************************************************************
 *
 * psglue.h
 *
 *   Adobe's code for shared stuff (specification only).
 *
 * Copyright 2007-2013 Adobe Systems Incorporated.
 *
 * This software, and all works of authorship, whether in source or
 * object code form as indicated by the copyright notice(s) included
 * herein (collectively, the "Work") is made available, and may only be
 * used, modified, and distributed under the FreeType Project License,
 * LICENSE.TXT.  Additionally, subject to the terms and conditions of the
 * FreeType Project License, each contributor to the Work hereby grants
 * to any individual or legal entity exercising permissions granted by
 * the FreeType Project License and this section (hereafter, "You" or
 * "Your") a perpetual, worldwide, non-exclusive, no-charge,
 * royalty-free, irrevocable (except as stated in this section) patent
 * license to make, have made, use, offer to sell, sell, import, and
 * otherwise transfer the Work, where such license applies only to those
 * patent claims licensable by such contributor that are necessarily
 * infringed by their contribution(s) alone or by combination of their
 * contribution(s) with the Work to which such contribution(s) was
 * submitted.  If You institute patent litigation against any entity
 * (including a cross-claim or counterclaim in a lawsuit) alleging that
 * the Work or a contribution incorporated within the Work constitutes
 * direct or contributory patent infringement, then any patent licenses
 * granted to You under this License for that Work shall terminate as of
 * the date such litigation is filed.
 *
 * By using, modifying, or distributing the Work you indicate that you
 * have read and understood the terms and conditions of the
 * FreeType Project License as well as those provided in this section,
 * and you accept them fully.
 *
 */

/* Private C99 bridge. Types are adapted from the licensed component interfaces. */
#include "formats/pdf/fonts/type2.h"
#include "gridfit.h"
#include <math.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int32_t PSGF_FT_Int32, PSGF_FT_Int, PSGF_FT_Fixed, CF2_F16Dot16, CF2_Int;
typedef uint32_t PSGF_FT_UInt32, PSGF_FT_UInt, CF2_UInt;
typedef int16_t PSGF_FT_Short;
typedef unsigned char PSGF_FT_Byte;
typedef int PSGF_FT_Bool, PSGF_FT_Error;
typedef void *PSGF_FT_Memory;

typedef struct {
    int32_t x, y;
} PSGF_FT_Vector;

#define PSGF_FT_UNUSED(x) (void)(x)
#define PSGF_FT_Err_Ok 0
#define PSGF_FT_BEGIN_HEADER
#define PSGF_FT_END_HEADER
#define PSGF_FT_LOCAL(x) static x
#define PSGF_FT_LOCAL_DEF(x) static x
#define PSGF_TRUE 1
#define PSGF_FALSE 0
#define PSGF_FT_BOOL(x) (!!(x))
#define PSGF_FT_ASSERT(x) ((void)0)
#define PSGF_FT_ZERO(x) memset(x, 0, sizeof(*(x)))
#define PSGF_FT_MEM_ZERO(x, n) memset(x, 0, n)
#define PSGF_FT_TRACE4(x) ((void)0)
#define PSGF_FT_TRACE5(x) ((void)0)
#define PSGF_FT_TRACE6(x) ((void)0)
#define PSGF_FT_TRACE7(x) ((void)0)
#define PSGF_FT_ERROR(x) ((void)0)
#define PSGF_FT_ABS(x) ((x) < 0 ? -(x) : (x))
#define PSGF_FT_MIN(x, y) ((x) < (y) ? (x) : (y))
#define PSGF_FT_MAX(x, y) ((x) > (y) ? (x) : (y))
#define PSGF_ADD_INT32(x, y) ((int32_t)((uint32_t)(x) + (uint32_t)(y)))
#define PSGF_SUB_INT32(x, y) ((int32_t)((uint32_t)(x) - (uint32_t)(y)))
#define PSGF_MUL_INT32(x, y) ((int32_t)((uint32_t)(x) * (uint32_t)(y)))
#define PSGF_NEG_INT32(x) ((int32_t)(0u - (uint32_t)(x)))
#define PSGF_FT_THROW(x) 1
#define CF2_SET_ERROR(p, e)                                                                        \
    do {                                                                                           \
        if (!*(p))                                                                                 \
            *(p) = 1;                                                                              \
    } while (0)

static int32_t PSGF_FT_MulFix(int32_t a, int32_t b) {
    int64_t v = (int64_t)a * b;
    return (int32_t)(v < 0 ? -((-v + 32768) / 65536) : (v + 32768) / 65536);
}

static int32_t PSGF_FT_DivFix(int32_t a, int32_t b) {
    if (!b)
        return a < 0 ? -INT32_MAX : INT32_MAX;
    int64_t x = a, y = b;
    int sign = (x < 0) ^ (y < 0);
    if (x < 0)
        x = -x;
    if (y < 0)
        y = -y;
    int64_t v = (x * 65536 + y / 2) / y;
    return (int32_t)(sign ? -v : v);
}

static int32_t PSGF_FT_MulDiv(int32_t a, int32_t b, int32_t c) {
    if (!c)
        return (a < 0) ^ (b < 0) ? -INT32_MAX : INT32_MAX;
    int64_t x = (int64_t)a * b, y = c;
    int sign = (x < 0) ^ (y < 0);
    if (x < 0)
        x = -x;
    if (y < 0)
        y = -y;
    int64_t v = (x + y / 2) / y;
    return (int32_t)(sign ? -v : v);
}

#include "../../../../../third_party/freetype-pshinter/psfixed.h"

typedef struct {
    CF2_Fixed a, b, c, d, tx, ty;
} CF2_Matrix;
typedef struct CF2_FontRec_ *CF2_Font;
typedef struct CF2_HintRec_ CF2_HintRec, *CF2_Hint;

enum { CF2_PathOpMoveTo = 1, CF2_PathOpLineTo = 2, CF2_PathOpQuadTo = 3, CF2_PathOpCubeTo = 4 };

typedef struct {
    PSGF_FT_Vector pt0, pt1, pt2, pt3;
    CF2_Int op;
} CF2_CallbackParamsRec, *CF2_CallbackParams;
typedef struct CF2_OutlineCallbacksRec_ *CF2_OutlineCallbacks;
typedef void (*CF2_Callback_Type)(CF2_OutlineCallbacks, const CF2_CallbackParams);

typedef struct CF2_OutlineCallbacksRec_ {
    CF2_Callback_Type moveTo, lineTo, quadTo, cubeTo;
    CF2_Int windingMomentum;
    PSGF_FT_Memory memory;
    PSGF_FT_Error *error;
} CF2_OutlineCallbacksRec;

static void ps_gridfit_fail(PSGF_FT_Memory memory);

typedef struct {
    void *ptr;
    size_t count, capacity, sizeItem;
    PSGF_FT_Error *error;
    PSGF_FT_Memory memory;
} CF2_ArrStackRec, *CF2_ArrStack;

static void cf2_arrstack_init(CF2_ArrStack a, PSGF_FT_Memory memory, PSGF_FT_Error *error,
                              size_t size) {
    memset(a, 0, sizeof(*a));
    a->memory = memory;
    a->error = error;
    a->sizeItem = size;
}

static void cf2_arrstack_finalize(CF2_ArrStack a) {
    free(a->ptr);
    a->ptr = NULL;
}

static void cf2_arrstack_clear(CF2_ArrStack a) {
    a->count = 0;
}

static size_t cf2_arrstack_size(CF2_ArrStack a) {
    return a->count;
}

static void *cf2_arrstack_getPointer(CF2_ArrStack a, size_t i) {
    if (i >= a->count)
        ps_gridfit_fail(a->memory);
    return (unsigned char *)a->ptr + i * a->sizeItem;
}

static void cf2_arrstack_push(CF2_ArrStack a, const void *p) {
    if (a->count == a->capacity) {
        size_t n = a->capacity ? a->capacity * 2 : 16;
        if (n > 256 || a->sizeItem > SIZE_MAX / n)
            ps_gridfit_fail(a->memory);
        void *next = realloc(a->ptr, n * a->sizeItem);
        if (!next)
            ps_gridfit_fail(a->memory);
        a->ptr = next;
        a->capacity = n;
    }
    memcpy((unsigned char *)a->ptr + a->count++ * a->sizeItem, p, a->sizeItem);
}

#include "../../../../../third_party/freetype-pshinter/psblues.h"
#include "../../../../../third_party/freetype-pshinter/pshints.h"

typedef struct {
    PSGF_FT_Fixed arrays[4][14];
    size_t counts[4];
    PSGF_FT_Fixed scale, shift, fuzz;
    unsigned language;
} PS_Decoder;

struct CF2_FontRec_ {
    PSGF_FT_Memory memory;
    PSGF_FT_Error error;
    PSGF_FT_Bool isT1, hinted, darkened, stemDarkened, reverseWinding;
    CF2_Matrix innerTransform, outerTransform;
    CF2_Fixed darkenX, darkenY;
    CF2_BluesRec blues;
    PS_Decoder *decoder;
};

static void cf2_getBlueMetrics(PS_Decoder *d, CF2_Fixed *scale, CF2_Fixed *shift, CF2_Fixed *fuzz) {
    *scale = d->scale;
    *shift = d->shift;
    *fuzz = d->fuzz;
}

#define BLUE_GETTER(name, idx)                                                                     \
    static void name(PS_Decoder *d, size_t *n, PSGF_FT_Fixed **p) {                                \
        *n = d->counts[idx];                                                                       \
        *p = d->arrays[idx];                                                                       \
    }
BLUE_GETTER(cf2_getBlueValues, 0)
BLUE_GETTER(cf2_getOtherBlues, 1)
BLUE_GETTER(cf2_getFamilyBlues, 2)
BLUE_GETTER(cf2_getFamilyOtherBlues, 3)

static unsigned cf2_getLanguageGroup(PS_Decoder *d) {
    return d->language;
}

static void cf2_hintmask_init(CF2_HintMask m, PSGF_FT_Error *e) {
    memset(m, 0, sizeof(*m));
    m->error = e;
}

static PSGF_FT_Bool cf2_hintmask_isValid(CF2_HintMask m) {
    return m->isValid;
}

static PSGF_FT_Bool cf2_hintmask_isNew(CF2_HintMask m) {
    return m->isNew;
}

static void cf2_hintmask_setNew(CF2_HintMask m, PSGF_FT_Bool v) {
    m->isNew = v;
}

static PSGF_FT_Byte *cf2_hintmask_getMaskPtr(CF2_HintMask m) {
    return m->mask;
}

static void cf2_hintmask_setAll(CF2_HintMask m, size_t n) {
    m->bitCount = n;
    m->byteCount = (n + 7) / 8;
    m->isValid = m->isNew = 1;
    memset(m->mask, 255, sizeof(m->mask));
    if (n % 8)
        m->mask[m->byteCount - 1] &= (unsigned char)(255u << (8 - n % 8));
}

#include "../../../../../third_party/freetype-pshinter/psblues.inc"
#include "../../../../../third_party/freetype-pshinter/pshints.inc"
