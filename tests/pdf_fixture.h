#ifndef PDF_FIXTURE_H
#define PDF_FIXTURE_H

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned char data[16384];
    size_t size;
} test_pdf_buffer;

static void test_pdf_append(test_pdf_buffer *pdf, const void *data, size_t size) {
    assert(size <= sizeof(pdf->data) - pdf->size);
    if (size)
        memcpy(pdf->data + pdf->size, data, size);
    pdf->size += size;
}

static void test_pdf_format(test_pdf_buffer *pdf, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int size =
        vsnprintf((char *)pdf->data + pdf->size, sizeof(pdf->data) - pdf->size, format, args);
    va_end(args);
    assert(size >= 0 && (size_t)size < sizeof(pdf->data) - pdf->size);
    pdf->size += (size_t)size;
}

static test_pdf_buffer test_make_pdf(const void *content, size_t size, int flate) {
    test_pdf_buffer pdf = {{0}, 0};
    size_t offsets[5] = {0};
    test_pdf_format(&pdf, "%%PDF-1.4\n");
    offsets[1] = pdf.size;
    test_pdf_format(&pdf, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    offsets[2] = pdf.size;
    test_pdf_format(&pdf, "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
    offsets[3] = pdf.size;
    test_pdf_format(&pdf, "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 72 72] "
                          "/Resources << >> /Contents 4 0 R >>\nendobj\n");
    offsets[4] = pdf.size;
    test_pdf_format(&pdf, "4 0 obj\n<< /Length %zu %s>>\nstream\n", size,
                    flate ? "/Filter /FlateDecode " : "");
    test_pdf_append(&pdf, content, size);
    test_pdf_format(&pdf, "\nendstream\nendobj\n");
    size_t xref = pdf.size;
    test_pdf_format(&pdf, "xref\n0 5\n0000000000 65535 f \n");
    for (unsigned i = 1; i < 5; i++)
        test_pdf_format(&pdf, "%010zu 00000 n \n", offsets[i]);
    test_pdf_format(&pdf, "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n", xref);
    return pdf;
}

#endif
