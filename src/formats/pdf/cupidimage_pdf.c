#include "core/internal.h"
#include "cupidimage.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pdf_internal.h"

static int pdf_rotate_page(cupidimage_image *img, int rotation) {
    if (!rotation)
        return 1;
    uint32_t width = img->width, height = img->height;
    uint32_t new_width = rotation == 180 ? width : height;
    uint32_t new_height = rotation == 180 ? height : width;
    uint8_t *pixels = (uint8_t *)malloc((size_t)width * height * 4);
    if (!pixels)
        return 0;
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t dx, dy;
            if (rotation == 90) {
                dx = height - 1 - y;
                dy = x;
            } else if (rotation == 180) {
                dx = width - 1 - x;
                dy = height - 1 - y;
            } else {
                dx = y;
                dy = width - 1 - x;
            }
            memcpy(pixels + ((size_t)dy * new_width + dx) * 4,
                   img->rgba + ((size_t)y * width + x) * 4, 4);
        }
    }
    free(img->rgba);
    img->rgba = pixels;
    img->width = new_width;
    img->height = new_height;
    return 1;
}

static int cupidimage_load_pdf_internal(const unsigned char *data, size_t size,
                                        cupidimage_image *img, int page_index, char *err,
                                        size_t errcap, const cupidimage_pdf_options *opts) {
    if (!data || !img) {
        cupidimage_set_err(err, errcap, "pdf: null pointer");
        return 0;
    }
    memset(img, 0, sizeof(*img));
    if (opts && ((opts->text_antialias != 0 && opts->text_antialias != 1) ||
                 (opts->truetype_hinting != 0 && opts->truetype_hinting != 1) ||
                 (opts->cff_hinting != 0 && opts->cff_hinting != 1) ||
                 (opts->type1_hinting != 0 && opts->type1_hinting != 1))) {
        cupidimage_set_err(err, errcap, "pdf: invalid text rendering option");
        return 0;
    }
    if (!cupidimage_pdf_check_header(data, size)) {
        cupidimage_set_err(err, errcap, "pdf: invalid header");
        return 0;
    }

    size_t eof_pos = 0;
    if (!cupidimage_pdf_find_eof_marker(data, size, &eof_pos)) {
        cupidimage_set_err(err, errcap, "pdf: EOF marker not found");
        return 0;
    }

    size_t xref_offset = 0;
    if (!cupidimage_pdf_find_startxref(data, eof_pos, &xref_offset)) {
        cupidimage_set_err(err, errcap, "pdf: startxref not found");
        return 0;
    }

    pdf_xref xref = {0};
    if (!cupidimage_pdf_parse_xref_table(data, size, xref_offset, &xref, err, errcap)) {
        return 0;
    }

    if (!cupidimage_pdf_parse_trailer(data, size, xref_offset, &xref, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    pdf_object_id pages_obj = 0;
    if (!cupidimage_pdf_get_pages_obj(data, size, &xref, &pages_obj, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    pdf_object_id page_obj = 0;
    if (!cupidimage_pdf_get_page_obj_by_index(data, size, &xref, pages_obj, page_index, &page_obj,
                                              err, errcap)) {
        free(xref.entries);
        return 0;
    }

    pdf_font_table fonts;
    cupidimage_pdf_font_table_init(&fonts);
    if (!cupidimage_pdf_collect_page_fonts_indexed(data, size, page_obj, &fonts, err, errcap,
                                                   &xref)) {
        cupidimage_pdf_font_table_free(&fonts);
        free(xref.entries);
        cupidimage_set_err(err, errcap, "pdf: invalid or exhausted font resources");
        return 0;
    }

    pdf_page_geometry geometry;
    if (!cupidimage_pdf_get_page_geometry_indexed(data, size, page_obj, &geometry, err, errcap,
                                                  &xref)) {
        cupidimage_pdf_font_table_free(&fonts);
        free(xref.entries);
        return 0;
    }

    int width = geometry.width;
    int height = geometry.height;
    if (width <= 0 || height <= 0) {
        cupidimage_pdf_font_table_free(&fonts);
        free(xref.entries);
        cupidimage_set_err(err, errcap, "pdf: invalid page dimensions");
        return 0;
    }

    size_t pixel_count = (size_t)width * (size_t)height;
    if (height > 0 && pixel_count / (size_t)height != (size_t)width) {
        cupidimage_pdf_font_table_free(&fonts);
        free(xref.entries);
        cupidimage_set_err(err, errcap, "pdf: image too large");
        return 0;
    }
    if (pixel_count > SIZE_MAX / 4u) {
        cupidimage_pdf_font_table_free(&fonts);
        free(xref.entries);
        cupidimage_set_err(err, errcap, "pdf: image too large");
        return 0;
    }

    img->width = (uint32_t)width;
    img->height = (uint32_t)height;
    img->rgba = (unsigned char *)calloc(pixel_count * 4u, 1);
    if (!img->rgba) {
        cupidimage_pdf_font_table_free(&fonts);
        free(xref.entries);
        cupidimage_set_err(err, errcap, "pdf: out of memory");
        return 0;
    }

    memset(img->rgba, 255, pixel_count * 4u);

    unsigned char *stream_data = NULL;
    size_t stream_len = 0;
    int is_flate = 0;
    if (!cupidimage_pdf_get_page_content_stream(data, size, &xref, page_obj, &stream_data,
                                                &stream_len, &is_flate, err, errcap)) {
        cupidimage_pdf_font_table_free(&fonts);
        free(xref.entries);
        free(img->rgba);
        img->rgba = NULL;
        return 0;
    }

    {
        const unsigned char *render_data = stream_data;
        size_t render_len = stream_len;
        unsigned char *inflated = NULL;

        if (is_flate) {
            size_t inflated_len = 0;
            if (!cupidimage_pdf_inflate_auto(stream_data, stream_len, &inflated, &inflated_len)) {
                free(stream_data);
                cupidimage_pdf_font_table_free(&fonts);
                free(xref.entries);
                free(img->rgba);
                img->rgba = NULL;
                cupidimage_set_err(err, errcap, "pdf: failed to decode Flate stream");
                return 0;
            }
            render_data = inflated;
            render_len = inflated_len;
        }

        if (!cupidimage_pdf_render_content_stream_options(render_data, render_len, data, size,
                                                          page_obj, &fonts, img, &geometry, err,
                                                          errcap, &xref, opts)) {
            free(inflated);
            free(stream_data);
            cupidimage_pdf_font_table_free(&fonts);
            free(xref.entries);
            free(img->rgba);
            img->rgba = NULL;
            return 0;
        }

        free(inflated);
    }
    free(stream_data);

    if (!pdf_rotate_page(img, geometry.rotation)) {
        cupidimage_pdf_font_table_free(&fonts);
        free(xref.entries);
        cupidimage_free(img);
        cupidimage_set_err(err, errcap, "pdf: out of memory rotating page");
        return 0;
    }

    img->hotspot_x = 0;
    img->hotspot_y = 0;

    cupidimage_pdf_font_table_free(&fonts);
    free(xref.entries);
    return 1;
}

int cupidimage_load_pdf(const unsigned char *data, size_t size, cupidimage_image *img, char *err,
                        size_t errcap) {
    return cupidimage_load_pdf_internal(data, size, img, 0, err, errcap, NULL);
}

int cupidimage_load_pdf_page(const unsigned char *data, size_t size, cupidimage_image *img,
                             int page_index, char *err, size_t errcap) {
    return cupidimage_load_pdf_internal(data, size, img, page_index, err, errcap, NULL);
}

int cupidimage_load_pdf_page_with_options(const unsigned char *data, size_t size,
                                          cupidimage_image *img, int page_index,
                                          const cupidimage_pdf_options *opts, char *err,
                                          size_t errcap) {
    return cupidimage_load_pdf_internal(data, size, img, page_index, err, errcap, opts);
}

int cupidimage_get_pdf_page_count(const unsigned char *data, size_t size, int *count, char *err,
                                  size_t errcap) {
    if (!data || !count) {
        cupidimage_set_err(err, errcap, "pdf: null pointer");
        return 0;
    }
    if (!cupidimage_pdf_check_header(data, size)) {
        cupidimage_set_err(err, errcap, "pdf: invalid header");
        return 0;
    }

    size_t eof_pos = 0;
    if (!cupidimage_pdf_find_eof_marker(data, size, &eof_pos)) {
        cupidimage_set_err(err, errcap, "pdf: EOF marker not found");
        return 0;
    }

    size_t xref_offset = 0;
    if (!cupidimage_pdf_find_startxref(data, eof_pos, &xref_offset)) {
        cupidimage_set_err(err, errcap, "pdf: startxref not found");
        return 0;
    }

    pdf_xref xref = {0};
    if (!cupidimage_pdf_parse_xref_table(data, size, xref_offset, &xref, err, errcap)) {
        return 0;
    }

    if (!cupidimage_pdf_parse_trailer(data, size, xref_offset, &xref, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    pdf_object_id pages_obj = 0;
    if (!cupidimage_pdf_get_pages_obj(data, size, &xref, &pages_obj, err, errcap)) {
        free(xref.entries);
        return 0;
    }

    int page_count = 0;
    if (!cupidimage_pdf_get_pages_count_indexed(data, size, pages_obj, &page_count, err, errcap,
                                                &xref)) {
        free(xref.entries);
        return 0;
    }

    *count = page_count;
    free(xref.entries);
    return 1;
}

int cupidimage_load_pdf_page_file(const char *path, cupidimage_image *img, int page_index,
                                  char *err, size_t errcap) {
    unsigned char *buf = NULL;
    size_t size = 0;
    if (!cupidimage_read_file_bytes(path, &buf, &size, err, errcap)) {
        return 0;
    }
    int ok = cupidimage_load_pdf_page(buf, size, img, page_index, err, errcap);
    free(buf);
    return ok;
}

int cupidimage_load_pdf_page_file_with_options(const char *path, cupidimage_image *img,
                                               int page_index, const cupidimage_pdf_options *opts,
                                               char *err, size_t errcap) {
    unsigned char *buf = NULL;
    size_t size = 0;
    if (!cupidimage_read_file_bytes(path, &buf, &size, err, errcap))
        return 0;
    int ok = cupidimage_load_pdf_page_with_options(buf, size, img, page_index, opts, err, errcap);
    free(buf);
    return ok;
}

int cupidimage_get_pdf_page_count_file(const char *path, int *count, char *err, size_t errcap) {
    unsigned char *buf = NULL;
    size_t size = 0;
    if (!cupidimage_read_file_bytes(path, &buf, &size, err, errcap)) {
        return 0;
    }
    int ok = cupidimage_get_pdf_page_count(buf, size, count, err, errcap);
    free(buf);
    return ok;
}

int cupidimage_load_pdf_file(const char *path, cupidimage_image *img, char *err, size_t errcap) {
    return cupidimage_load_pdf_page_file(path, img, 0, err, errcap);
}
