CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -Wall -Wextra -std=c99
LDLIBS ?= -lm
INCLUDES = -Isrc -I.

OBJDIR = obj
BINDIR = bin
SRCDIR = src
LIBSRC = $(filter-out $(SRCDIR)/cupidimage_cli.c,$(wildcard $(SRCDIR)/*.c))
LIBOBJ = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(LIBSRC))
CLI_OBJ = $(OBJDIR)/cupidimage_cli.o
LIB = $(BINDIR)/libcupidimage.a
CLI = $(BINDIR)/cupidimage
TEST_TARGETS = test-png test-cli test-decoder-safety test-deflate-containers \
 test-pdf-graphics test-jpeg test-pdf-pages test-pdf-images test-sixel \
 test-gif-regressions test-terminal-regressions test-svg-regressions test-svg-safety test-webp test-pdf-safety test-pdf-forms \
 test-tiff-bounds test-svg-css-oom test-pdf-clipping test-pdf-clip-oom test-svg-numeric test-pdf-colors
TEST_BINS = test_png test_decoder_safety test_deflate_containers test_pdf_graphics \
 test_jpeg_decoder test_pdf_pages test_sixel test_svg_safety test_webp_lossless test_tiff_bounds test_svg_css_oom test_pdf_clip_oom test_svg_numeric

.PHONY: all lib cli test $(TEST_TARGETS) test-svg test-pdf clean
all: lib cli
lib: $(LIB)
cli: $(CLI)
test: $(TEST_TARGETS)

test-png: $(BINDIR)/test_png
	$<
test-cli: $(CLI)
	python3 tests/test_cli.py $<
test-decoder-safety: $(BINDIR)/test_decoder_safety
	$<
test-deflate-containers: $(BINDIR)/test_deflate_containers
	$<
test-pdf-graphics: $(BINDIR)/test_pdf_graphics
	$<
test-svg-safety: $(BINDIR)/test_svg_safety
	$<
test-svg-numeric: $(BINDIR)/test_svg_numeric
	$<
test-webp: $(BINDIR)/test_webp_lossless
	$<

test-tiff-bounds: $(BINDIR)/test_tiff_bounds
	$<

test-svg-css-oom: $(BINDIR)/test_svg_css_oom
	$<

test-pdf-clip-oom: $(BINDIR)/test_pdf_clip_oom
	$<
test-jpeg: $(BINDIR)/test_jpeg_decoder
	python3 scripts/test_jpeg_decoder.py $<
test-pdf-pages: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_pages.py $<
test-pdf-images: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_images.py $<
test-pdf-safety: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_safety.py $<
test-pdf-forms: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_forms.py $<

test-pdf-clipping: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_clipping.py $<

test-pdf-colors: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_colors.py $<
test-sixel: $(BINDIR)/test_sixel
	python3 scripts/test_sixel.py $<
test-gif-regressions:
	CC="$(CC)" CFLAGS="$(CFLAGS)" bash scripts/test_gif_regressions.sh
test-terminal-regressions:
	CC="$(CC)" CFLAGS="$(CFLAGS)" bash scripts/test_terminal_regressions.sh
test-svg-regressions: $(CLI) $(LIB)
	CC="$(CC)" CFLAGS="$(CFLAGS)" BIN="$(abspath $(CLI))" LIB="$(abspath $(LIB))" bash scripts/test_svg_regressions.sh

# Interactive asset viewers are separate from the unattended regression suite.
test-svg: $(CLI)
	bash scripts/test_svg_assets.sh
test-pdf: $(CLI)
	bash scripts/test_pdf_assets.sh

$(BINDIR)/test_%: tests/test_%.c $(LIB) $(SRCDIR)/cupidimage.h | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)
$(BINDIR)/test_deflate_containers $(BINDIR)/test_pdf_graphics: tests/pdf_fixture.h
$(BINDIR)/test_svg_css_oom: $(SRCDIR)/cupidimage_svg.c $(SRCDIR)/cupidimage_svg_base.c tests/fault_alloc.h
$(BINDIR)/test_pdf_clip_oom: $(SRCDIR)/cupidimage_pdf.c tests/fault_alloc.h
$(BINDIR)/test_jpeg_decoder: scripts/test_jpeg_decoder.c $(LIB) | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)
$(BINDIR)/test_pdf_pages: scripts/test_pdf_pages.c $(LIB) | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)
$(BINDIR)/test_sixel: scripts/test_sixel.c $(LIB) | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

$(LIB): $(LIBOBJ) | $(BINDIR)
	$(AR) rcs $@ $(LIBOBJ)
$(CLI): $(CLI_OBJ) $(LIB) | $(BINDIR)
	$(CC) $(CFLAGS) $(CLI_OBJ) $(LIB) -o $@ $(LDLIBS)
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@
$(OBJDIR) $(BINDIR):
	mkdir -p $@
clean:
	rm -f $(OBJDIR)/*.o $(OBJDIR)/*.d $(LIB) $(CLI) $(addprefix $(BINDIR)/,$(TEST_BINS))

-include $(LIBOBJ:.o=.d) $(CLI_OBJ:.o=.d)
