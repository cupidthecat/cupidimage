CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -Wall -Wextra -std=c99
LDLIBS ?= -lm
INCLUDES = -Isrc -I.

OBJDIR = obj
BINDIR = bin
SRCDIR = src
LIBSRC = $(shell find $(SRCDIR) -name '*.c' ! -path '$(SRCDIR)/cli/*' | sort)
LIBOBJ = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(LIBSRC))
CLI_OBJ = $(OBJDIR)/cli/main.o
LIB = $(BINDIR)/libcupidimage.a
CLI = $(BINDIR)/cupidimage
TEST_TARGETS = test-pdf-lzw test-pdf-filters test-pdf-masks test-pdf-packed-images test-visual-review test-asset-corpus test-pdf-audit test-pdf-streams test-jpeg-reference test-format-pixels test-assets test-webp-pixels test-webp-container test-inflate test-png test-cli test-decoder-safety test-deflate-containers \
 test-pdf-graphics test-jpeg test-pdf-pages test-pdf-images test-sixel \
 test-gif-regressions test-terminal-regressions test-svg-regressions test-svg-safety test-webp test-pdf-safety test-pdf-forms \
 test-tiff-bounds test-svg-css-oom test-pdf-clipping test-pdf-clip-oom test-svg-numeric test-pdf-colors
TEST_BINS = test_pdf_filters test_asset_probe test_format_pixels test_webp_pixels test_webp_container test_inflate test_png test_decoder_safety test_deflate_containers test_pdf_graphics \
 test_jpeg_decoder test_pdf_pages test_sixel test_svg_safety test_webp_lossless test_tiff_bounds test_svg_css_oom test_pdf_clip_oom test_svg_numeric

.PHONY: all lib cli test test-pdf-corpus $(TEST_TARGETS) test-svg test-pdf clean
all: lib cli
lib: $(LIB)
cli: $(CLI)
test: $(TEST_TARGETS)

test-jpeg-reference: $(BINDIR)/test_format_pixels
	python3 scripts/test_jpeg_reference.py $<

test-format-pixels: $(BINDIR)/test_format_pixels
	python3 scripts/test_format_pixels.py $<

test-asset-corpus: $(BINDIR)/test_asset_probe
	python3 scripts/test_asset_corpus.py --probe $< --memory-mib $(if $(findstring sanitize,$(CFLAGS)),0,1024) --timeout $(if $(findstring sanitize,$(CFLAGS)),30,10)

test-visual-review:
	python3 scripts/test_visual_review.py

test-pdf-audit:
	python3 scripts/test_pdf_audit.py

.PHONY: audit-assets audit-pdf-full review-assets
review-assets: $(CLI) $(BINDIR)/test_asset_probe
	python3 scripts/review_assets.py --cli $(CLI) --probe $(BINDIR)/test_asset_probe $(REVIEW_ARGS)
audit-assets: test-asset-corpus
audit-pdf-full: $(BINDIR)/test_pdf_pages
	python3 scripts/audit_pdf_corpus.py --probe $< --download

test-assets:
	python3 scripts/test_assets.py

test-inflate: $(BINDIR)/test_inflate
	python3 scripts/test_inflate.py $<

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
test-webp-pixels: $(BINDIR)/test_webp_pixels
	python3 scripts/test_webp_pixels.py $<

test-webp-container: $(BINDIR)/test_webp_container
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
test-pdf-packed-images: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_packed_images.py $<

test-pdf-lzw: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_lzw.py $<

test-pdf-filters: $(BINDIR)/test_pdf_filters
	$<

test-pdf-masks: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_masks.py $<

test-pdf-images: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_images.py $<
test-pdf-streams: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_streams.py $<

test-pdf-corpus: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_corpus.py $< --download

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
$(BINDIR)/test_svg_css_oom: $(filter $(SRCDIR)/formats/svg/%,$(LIBSRC)) $(SRCDIR)/formats/svg/svg_internal.h tests/fault_alloc.h
$(BINDIR)/test_pdf_clip_oom: $(filter $(SRCDIR)/formats/pdf/%,$(LIBSRC)) $(SRCDIR)/formats/pdf/pdf_internal.h tests/fault_alloc.h
$(BINDIR)/test_jpeg_decoder: scripts/test_jpeg_decoder.c $(LIB) | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)
$(BINDIR)/test_pdf_pages: scripts/test_pdf_pages.c $(LIB) | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)
$(BINDIR)/test_sixel: scripts/test_sixel.c $(LIB) | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

$(LIB): $(LIBOBJ) $(OBJDIR)/library-sources.list | $(BINDIR)
	rm -f $@
	$(AR) rcs $@ $(LIBOBJ)
$(CLI): $(CLI_OBJ) $(LIB) | $(BINDIR)
	$(CC) $(CFLAGS) $(CLI_OBJ) $(LIB) -o $@ $(LDLIBS)
.PHONY: check-source-list
$(OBJDIR)/library-sources.list: check-source-list | $(OBJDIR)
	@printf '%s\n' $(LIBSRC) > $@.tmp
	@cmp -s $@.tmp $@ && rm -f $@.tmp || mv $@.tmp $@

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@
$(OBJDIR) $(BINDIR):
	mkdir -p $@
clean:
	rm -f $(LIBOBJ) $(CLI_OBJ) $(LIBOBJ:.o=.d) $(CLI_OBJ:.o=.d) $(OBJDIR)/*.o $(OBJDIR)/*.d $(OBJDIR)/library-sources.list $(OBJDIR)/library-sources.list.tmp $(LIB) $(CLI) $(addprefix $(BINDIR)/,$(TEST_BINS))

-include $(LIBOBJ:.o=.d) $(CLI_OBJ:.o=.d)
