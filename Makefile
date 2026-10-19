CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -Wall -Wextra -std=c99
LDLIBS ?= -lm
INCLUDES = -Isrc -I.

OBJDIR = obj
BINDIR = bin
SRCDIR = src

LIBOBJ = $(OBJDIR)/cupidimage.o $(OBJDIR)/cupidimage_internal.o $(OBJDIR)/cupidimage_png.o $(OBJDIR)/cupidimage_jpeg.o $(OBJDIR)/cupidimage_webp.o $(OBJDIR)/cupidimage_webp_tables.o $(OBJDIR)/cupidimage_webp_lossless.o $(OBJDIR)/cupidimage_gif.o $(OBJDIR)/cupidimage_bmp.o $(OBJDIR)/cupidimage_ico.o $(OBJDIR)/cupidimage_tiff.o $(OBJDIR)/cupidimage_tga.o $(OBJDIR)/cupidimage_pdf.o $(OBJDIR)/cupidimage_svg.o $(OBJDIR)/cupidimage_svg_base.o $(OBJDIR)/cupidimage_kitty.o $(OBJDIR)/cupidimage_sixel.o
LIB = $(BINDIR)/libcupidimage.a
CLI = $(BINDIR)/cupidimage
TEST_PNG = $(BINDIR)/test_png

.PHONY: all lib cli test-png test-svg test-svg-regressions test-pdf clean test-cli test-decoder-safety test-deflate-containers

all: lib cli

lib: $(LIB)

cli: $(CLI)

test-png: $(TEST_PNG)
	$(TEST_PNG)

$(TEST_PNG): tests/test_png.c $(LIB) $(SRCDIR)/cupidimage.h
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

test-cli: cli
	python3 tests/test_cli.py $(CLI)


test-decoder-safety: $(BINDIR)/test_decoder_safety
	$(BINDIR)/test_decoder_safety

$(BINDIR)/test_decoder_safety: tests/test_decoder_safety.c $(LIB) $(SRCDIR)/cupidimage.h

test-deflate-containers: $(BINDIR)/test_deflate_containers
	$(BINDIR)/test_deflate_containers

$(BINDIR)/test_deflate_containers: tests/test_deflate_containers.c tests/pdf_fixture.h $(LIB) $(SRCDIR)/cupidimage.h
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

test-svg: cli
	./scripts/test_svg_assets.sh

test-svg-regressions: cli lib
	./scripts/test_svg_regressions.sh

test-pdf: cli
	./scripts/test_pdf_assets.sh

$(LIB): $(LIBOBJ)
	@mkdir -p $(BINDIR)
	$(AR) rcs $@ $(LIBOBJ)

$(CLI): $(SRCDIR)/cupidimage.c $(SRCDIR)/cupidimage_internal.c $(SRCDIR)/cupidimage_png.c $(SRCDIR)/cupidimage_jpeg.c $(SRCDIR)/cupidimage_webp.c $(SRCDIR)/cupidimage_webp_tables.c $(SRCDIR)/cupidimage_webp_lossless.c $(SRCDIR)/cupidimage_gif.c $(SRCDIR)/cupidimage_bmp.c $(SRCDIR)/cupidimage_ico.c $(SRCDIR)/cupidimage_tiff.c $(SRCDIR)/cupidimage_tga.c $(SRCDIR)/cupidimage_pdf.c $(SRCDIR)/cupidimage_svg.c $(SRCDIR)/cupidimage_svg_base.c $(SRCDIR)/cupidimage_kitty.c $(SRCDIR)/cupidimage_sixel.c $(SRCDIR)/cupidimage_cli.c | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $^ -o $@ $(LDLIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(BINDIR):
	@mkdir -p $(BINDIR)

clean:
	rm -f $(OBJDIR)/*.o $(LIB) $(CLI) $(TEST_PNG)

	rm -f $(OBJDIR)/*.o $(LIB) $(CLI) $(BINDIR)/test_decoder_safety

	rm -f $(OBJDIR)/*.o $(LIB) $(CLI) $(BINDIR)/test_deflate_containers
