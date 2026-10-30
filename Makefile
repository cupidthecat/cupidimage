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
TEST_TARGETS = test-sixel-dither  test-pdf-type1-page-hinting test-pdf-type1-gridfit test-pdf-cff-page-hinting test-pdf-ps-gridfit test-pdf-page-hinting test-pdf-text-antialias test-pdf-coverage test-pdf-tt-loader test-pdf-tt-vm test-pdf-empty-glyphs test-pdf-combined-paint test-pdf-font-descriptors test-pdf-icc-mab test-pdf-intents test-pdf-icc-lut test-pdf-nchannel test-pdf-font-resources test-pdf-substitution test-pdf-devicen test-pdf-separation test-pdf-font-advances test-pdf-text-surfaces test-pdf-truetype-bearings test-pdf-default-colors test-pdf-icc test-pdf-lab test-pdf-calibrated test-pdf-flate-extent test-pdf-content-scopes test-pdf-flatness test-pdf-font-cache test-pdf-feature-matrix test-pdf-user-unit test-pdf-image-values test-pdf-dct-arithmetic test-pdf-jpeg-sampling test-jpeg-math test-pdf-dct test-pdf-truetype-selection test-pdf-type3-objects test-pdf-font-defaults test-pdf-xref test-pdf-shading-patterns test-pdf-pattern-colors test-pdf-pattern-backdrops test-pdf-device-color test-pdf-patch test-pdf-mesh test-pdf-content-syntax test-pdf-text-knockout test-pdf-knockout test-pdf-page-groups test-pdf-soft-masks test-pdf-groups test-pdf-graphics-fonts test-pdf-standard-fonts test-pdf-type1 test-pdf-cff test-pdf-predefined-cmaps test-pdf-cmaps test-pdf-truetype test-pdf-cid test-pdf-strokes test-pdf-patterns test-pdf-type3 test-pdf-text-state test-pdf-inline test-pdf-ccitt test-pdf-calculator test-pdf-sampled-functions test-pdf-functions test-pdf-shading test-pdf-object-syntax test-pdf-font-maps test-pdf-transparency test-pdf-indexed test-pdf-lzw test-pdf-filters test-pdf-masks test-pdf-packed-images test-visual-review test-asset-corpus test-pdf-audit test-pdf-streams test-jpeg-reference test-format-pixels test-assets test-webp-pixels test-webp-container test-inflate test-png test-cli test-decoder-safety test-deflate-containers \
 test-pdf-graphics test-jpeg test-pdf-pages test-pdf-images test-sixel \
 test-gif-regressions test-terminal-regressions test-svg-regressions test-svg-safety test-webp test-pdf-safety test-pdf-forms \
 test-tiff-bounds test-svg-css-oom test-pdf-clipping test-pdf-clip-oom test-svg-numeric test-pdf-colors
TEST_BINS = test_sixel_dither  test_pdf_type1_counter test_pdf_type1_gridfit test_pdf_type1_trace test_pdf_type1_lexer test_pdf_ps_gridfit test_pdf_page_hint test_pdf_coverage test_pdf_coverage_oom test_pdf_tt_loader test_pdf_tt_glyphs test_pdf_tt_vm test_pdf_icc_mab test_pdf_icc_lut test_pdf_flatness test_pdf_font_cache test_jpeg_math test_jpeg_samples test_pdf_content_syntax test_pdf_standard test_pdf_type1 test_pdf_cff test_pdf_cmap test_pdf_truetype test_pdf_ccitt test_pdf_calculator test_pdf_filters test_asset_probe test_format_pixels test_webp_pixels test_webp_container test_inflate test_png test_decoder_safety test_deflate_containers test_pdf_graphics \
 test_jpeg_decoder test_pdf_pages test_sixel test_svg_safety test_webp_lossless test_tiff_bounds test_svg_css_oom test_pdf_clip_oom test_svg_numeric

.PHONY: all lib cli test test-pdf-corpus $(TEST_TARGETS) test-svg test-pdf clean
all: lib cli
lib: $(LIB)
cli: $(CLI)
test: $(TEST_TARGETS)

$(BINDIR)/test_sixel_dither: tests/test_sixel_dither.c src/output/sixel/cupidimage_sixel.c
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< -o $@ $(LDLIBS)

test-sixel-dither: $(BINDIR)/test_sixel_dither
	$<

$(BINDIR)/test_pdf_type1_gridfit: tests/test_pdf_type1_gridfit.c $(LIB) $(shell find $(SRCDIR)/formats/pdf/fonts -name '*.h') $(shell find $(SRCDIR)/formats/pdf/fonts -name '*.c')
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

test-pdf-type1-gridfit: $(BINDIR)/test_pdf_type1_gridfit
	$(BINDIR)/test_pdf_type1_gridfit
	python3 scripts/test_pdf_type1_gridfit_reference.py $(BINDIR)/test_pdf_type1_gridfit
	python3 scripts/test_pdf_type1_gridfit_reference.py $(BINDIR)/test_pdf_type1_gridfit --nonuniform
	python3 scripts/test_pdf_type1_gridfit_reference.py $(BINDIR)/test_pdf_type1_gridfit --bitmaps

test-pdf-type1-page-hinting: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom $(CLI)
	python3 scripts/generate_pdf_type1_page_hinting_fixture.py --check
	python3 scripts/test_pdf_type1_page_hinting.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom $(CLI)

test-pdf-page-hinting: $(BINDIR)/test_pdf_page_hint $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom $(CLI)
	$(BINDIR)/test_pdf_page_hint
	python3 scripts/generate_pdf_page_hinting_fixture.py --check
	python3 scripts/test_pdf_page_hinting.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom $(CLI)

test-pdf-text-antialias: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom $(CLI)
	python3 scripts/generate_pdf_text_coverage_fixture.py --check
	python3 scripts/test_pdf_text_antialias.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom $(CLI)

test-pdf-coverage: $(BINDIR)/test_pdf_coverage $(BINDIR)/test_pdf_coverage_oom $(BINDIR)/test_pdf_tt_glyphs
	$<
	$(BINDIR)/test_pdf_coverage_oom
	python3 scripts/test_pdf_coverage_bounds.py $<
	python3 scripts/test_pdf_coverage_reference.py $<
	python3 scripts/test_pdf_tt_bitmap_reference.py $(BINDIR)/test_pdf_tt_glyphs $<

test-jpeg-reference: $(BINDIR)/test_format_pixels
	python3 scripts/test_jpeg_reference.py $<

test-format-pixels: $(BINDIR)/test_format_pixels
	python3 scripts/test_format_pixels.py $<

test-asset-corpus: $(BINDIR)/test_asset_probe
	python3 scripts/test_asset_corpus.py --probe $< --memory-mib $(if $(findstring sanitize,$(CFLAGS)),0,1024) --timeout $(if $(findstring sanitize,$(CFLAGS)),30,10)

test-visual-review: $(BINDIR)/test_pdf_pages $(BINDIR)/test_asset_probe $(CLI)
	python3 scripts/test_visual_review.py
	python3 scripts/test_pdf_render_options.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_asset_probe $(CLI)

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

test-pdf-sampled-functions: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_sampled_functions.py $<

test-pdf-functions: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_functions.py $<

test-pdf-shading: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_shading.py $<

test-pdf-object-syntax: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_object_syntax.py $<

test-pdf-font-maps: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_font_maps.py $<

test-pdf-transparency: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_transparency.py $<

test-pdf-indexed: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_indexed.py $<

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
$(BINDIR)/test_pdf_clip_oom: $(SRCDIR)/core/image.c $(filter $(SRCDIR)/formats/pdf/%,$(LIBSRC)) $(shell find $(SRCDIR)/formats/pdf -name '*.h') tests/fixtures/pdf-truetype.inc tests/fixtures/pdf-cff.inc tests/fixtures/pdf-type1.inc $(shell find $(SRCDIR)/formats/pdf -name '*.inc') $(SRCDIR)/compression/ccitt.c $(SRCDIR)/compression/ccitt_tables.h $(filter $(SRCDIR)/formats/jpeg/%,$(LIBSRC)) $(wildcard $(SRCDIR)/formats/jpeg/*.h) tests/fault_alloc.h
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

test-pdf-calculator: $(BINDIR)/test_pdf_calculator $(BINDIR)/test_pdf_pages
	$(BINDIR)/test_pdf_calculator
	python3 scripts/test_pdf_calculator.py $(BINDIR)/test_pdf_pages

$(BINDIR)/test_pdf_calculator: tests/test_pdf_calculator.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

test-pdf-ccitt: $(BINDIR)/test_pdf_ccitt $(BINDIR)/test_pdf_pages
	$<
	python3 scripts/test_pdf_ccitt.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_ccitt

test-pdf-inline: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_inline.py $<

test-pdf-text-state: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_text_state.py $<

test-pdf-type3: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_type3.py $<

test-pdf-patterns: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_patterns.py $<

test-pdf-strokes: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_strokes.py $<

test-pdf-cid: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_cid.py $<

$(BINDIR)/test_pdf_truetype: tests/test_pdf_truetype.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

test-pdf-truetype: $(BINDIR)/test_pdf_truetype $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_truetype.py $(BINDIR)/test_pdf_truetype
	python3 scripts/test_pdf_truetype_cmaps.py $(BINDIR)/test_pdf_truetype
	python3 scripts/generate_pdf_post_names.py --check
	python3 scripts/test_pdf_truetype_names.py $(BINDIR)/test_pdf_truetype
	python3 scripts/test_pdf_embedded_fonts.py $(BINDIR)/test_pdf_pages

$(BINDIR)/test_pdf_cmap: tests/test_pdf_cmap.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

test-pdf-cmaps: $(BINDIR)/test_pdf_cmap $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_cmaps.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_cmap

test-pdf-predefined-cmaps: $(BINDIR)/test_pdf_cmap $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_predefined_cmaps.py $(BINDIR)/test_pdf_cmap $(BINDIR)/test_pdf_pages

$(BINDIR)/test_pdf_cff: tests/test_pdf_cff.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

test-pdf-cff: $(BINDIR)/test_pdf_cff $(BINDIR)/test_pdf_pages
	$(BINDIR)/test_pdf_cff
	python3 scripts/test_pdf_cff.py $(BINDIR)/test_pdf_cff
	python3 scripts/test_pdf_cff_hints.py $(BINDIR)/test_pdf_cff
	python3 scripts/test_pdf_type2.py $(BINDIR)/test_pdf_cff
	python3 scripts/test_pdf_type2_hints.py $(BINDIR)/test_pdf_cff
	python3 scripts/compare_pdf_cff_hints.py $(BINDIR)/test_pdf_cff
	python3 scripts/test_pdf_cff_render.py $(BINDIR)/test_pdf_pages

$(BINDIR)/test_pdf_type1: tests/test_pdf_type1.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

$(BINDIR)/test_pdf_type1_lexer: tests/test_pdf_type1_lexer.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

$(BINDIR)/test_pdf_type1_trace: tests/test_pdf_type1_trace.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

$(BINDIR)/test_pdf_type1_counter: tests/test_pdf_type1_counter.c $(LIB) $(shell find $(SRCDIR)/formats/pdf/fonts -name '*.h') $(shell find $(SRCDIR)/formats/pdf/fonts -name '*.c')
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

test-pdf-type1: $(BINDIR)/test_pdf_clip_oom $(BINDIR)/test_pdf_type1_counter $(BINDIR)/test_pdf_type1_trace $(BINDIR)/test_pdf_type1 $(BINDIR)/test_pdf_type1_lexer $(BINDIR)/test_pdf_pages
	python3 scripts/generate_pdf_type1_counter_fixture.py --check
	$(BINDIR)/test_pdf_type1_counter
	$(BINDIR)/test_pdf_type1_lexer
	$(BINDIR)/test_pdf_type1_trace
	python3 scripts/test_pdf_type1.py $(BINDIR)/test_pdf_type1
	python3 scripts/test_pdf_type1_hints.py $(BINDIR)/test_pdf_type1
	python3 scripts/test_pdf_type1_trace.py $(BINDIR)/test_pdf_type1
	python3 scripts/test_pdf_type1_charstrings.py $(BINDIR)/test_pdf_type1
	python3 scripts/test_pdf_type1_render.py $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_type1_currentpoint.py $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_type1_counters.py $(BINDIR)/test_pdf_type1 $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

$(BINDIR)/test_pdf_standard: tests/test_pdf_standard.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

test-pdf-standard-fonts: $(BINDIR)/test_pdf_standard $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom $(BINDIR)/test_pdf_cff
	python3 scripts/generate_pdf_standard_metrics.py --check
	python3 scripts/generate_pdf_standard_outlines.py --check
	python3 scripts/generate_pdf_extended_fonts.py --check
	python3 scripts/generate_pdf_standard_marks.py --check
	python3 scripts/test_pdf_standard_marks.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/compare_pdf_standard_marks.py $(BINDIR)/test_pdf_cff
	python3 scripts/test_pdf_standard_metrics.py $(BINDIR)/test_pdf_standard
	python3 scripts/test_pdf_standard_fonts.py $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_standard_outlines.py $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_extended_fonts.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-pdf-graphics-fonts: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_graphics_fonts.py $(BINDIR)/test_pdf_pages

test-pdf-groups: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_groups.py $(BINDIR)/test_pdf_pages

test-pdf-soft-masks: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_soft_masks.py $(BINDIR)/test_pdf_pages

test-pdf-page-groups: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_page_groups.py $(BINDIR)/test_pdf_pages

test-pdf-knockout: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_knockout.py $(BINDIR)/test_pdf_pages

test-pdf-text-knockout: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_text_knockout.py $(BINDIR)/test_pdf_pages

test-pdf-content-syntax: $(BINDIR)/test_pdf_content_syntax $(BINDIR)/test_pdf_pages
	$(BINDIR)/test_pdf_content_syntax
	python3 scripts/test_pdf_content_syntax.py $(BINDIR)/test_pdf_pages

$(BINDIR)/test_pdf_content_syntax: tests/test_pdf_content_syntax.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

test-pdf-mesh: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_mesh.py $(BINDIR)/test_pdf_pages

test-pdf-patch: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_patch.py $(BINDIR)/test_pdf_pages

CLANG_FORMAT ?= clang-format
.PHONY: format format-check
format:
	CLANG_FORMAT="$(CLANG_FORMAT)" python3 scripts/format.py --write

format-check:
	CLANG_FORMAT="$(CLANG_FORMAT)" python3 scripts/format.py --check

test-pdf-device-color: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_device_color.py $(BINDIR)/test_pdf_pages

test-pdf-pattern-backdrops: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_pattern_backdrops.py $(BINDIR)/test_pdf_pages

test-pdf-pattern-colors: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_pattern_colors.py $(BINDIR)/test_pdf_pages

test-pdf-shading-patterns: $(BINDIR)/test_pdf_pages
	python3 scripts/test_pdf_shading_patterns.py $(BINDIR)/test_pdf_pages

test-pdf-xref: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_xref.py $< $(BINDIR)/test_pdf_clip_oom

test-pdf-font-defaults: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_font_defaults.py $< $(BINDIR)/test_pdf_clip_oom

test-pdf-type3-objects: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_type3_objects.py $< $(BINDIR)/test_pdf_clip_oom

test-pdf-truetype-selection: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_truetype_selection.py $< $(BINDIR)/test_pdf_clip_oom

$(BINDIR)/test_jpeg_samples: tests/test_jpeg_samples.c $(LIB) | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

test-pdf-dct: $(BINDIR)/test_jpeg_samples $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_dct.py $(BINDIR)/test_jpeg_samples $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-jpeg-math: $(BINDIR)/test_jpeg_math
	$<

$(BINDIR)/test_jpeg_math: tests/test_jpeg_math.c $(filter $(SRCDIR)/formats/jpeg/%,$(LIBSRC)) $(wildcard $(SRCDIR)/formats/jpeg/*.h) $(LIB) | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

test-pdf-jpeg-sampling: $(BINDIR)/test_jpeg_samples $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_jpeg_sampling.py $(BINDIR)/test_jpeg_samples $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-pdf-dct-arithmetic: $(BINDIR)/test_jpeg_samples $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_dct_arithmetic.py $(BINDIR)/test_jpeg_samples $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-pdf-image-values: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_image_values.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-pdf-user-unit: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_user_unit.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-pdf-feature-matrix:
	python3 scripts/test_pdf_feature_matrix.py

test-pdf-font-cache: $(BINDIR)/test_pdf_font_cache
	$(BINDIR)/test_pdf_font_cache

$(BINDIR)/test_pdf_font_cache: tests/fixtures/pdf-truetype.inc tests/fixtures/pdf-cff.inc tests/fixtures/pdf-type1.inc $(SRCDIR)/formats/pdf/fonts/program.h

test-pdf-flatness: $(BINDIR)/test_pdf_flatness $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	$(BINDIR)/test_pdf_flatness
	python3 scripts/test_pdf_flatness.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-pdf-content-scopes: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_content_scopes.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-pdf-flate-extent: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom $(BINDIR)/test_inflate
	python3 scripts/test_pdf_flate_extent.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom $(BINDIR)/test_inflate

test-pdf-calibrated: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_calibrated.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-pdf-lab: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_lab.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

.PHONY: download-pdf-corpus
download-pdf-corpus:
	python3 scripts/audit_pdf_corpus.py --download-only

test-pdf-icc: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_icc.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-pdf-default-colors: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_default_colors.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom


test-pdf-truetype-bearings: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_truetype $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_truetype_bearings.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_truetype $(BINDIR)/test_pdf_clip_oom


test-pdf-text-surfaces: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_text_surfaces.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom


test-pdf-font-advances: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_font_advances.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom


test-pdf-separation: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_separation.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom


test-pdf-devicen: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_devicen.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-pdf-tt-loader: $(BINDIR)/test_pdf_tt_loader $(BINDIR)/test_pdf_tt_glyphs
	$(BINDIR)/test_pdf_tt_loader
	python3 scripts/test_pdf_tt_glyphs_reference.py $(BINDIR)/test_pdf_tt_glyphs --native
	python3 scripts/test_pdf_tt_loader_reference.py $(BINDIR)/test_pdf_tt_glyphs
	python3 scripts/test_pdf_tt_glyphs_reference.py $(BINDIR)/test_pdf_tt_glyphs --native --gray
	python3 scripts/test_pdf_tt_composites_reference.py $(BINDIR)/test_pdf_tt_glyphs
	python3 scripts/test_pdf_tt_composites_reference.py $(BINDIR)/test_pdf_tt_glyphs --gray
	python3 scripts/test_pdf_tt_composite_transforms.py $(BINDIR)/test_pdf_tt_glyphs
	python3 scripts/test_pdf_tt_instructions_reference.py $(BINDIR)/test_pdf_tt_glyphs
	python3 scripts/test_pdf_tt_nonuniform_reference.py $(BINDIR)/test_pdf_tt_glyphs
	python3 scripts/test_pdf_tt_fractional_reference.py $(BINDIR)/test_pdf_tt_glyphs
	python3 scripts/test_pdf_tt_transform_reference.py $(BINDIR)/test_pdf_tt_glyphs
	python3 scripts/test_pdf_tt_glyphs_reference.py $(BINDIR)/test_pdf_tt_glyphs --native --nonuniform
	python3 scripts/test_pdf_tt_glyphs_reference.py $(BINDIR)/test_pdf_tt_glyphs --native --nonuniform --gray
	python3 scripts/test_pdf_tt_composites_reference.py $(BINDIR)/test_pdf_tt_glyphs --nonuniform
	python3 scripts/test_pdf_tt_composites_reference.py $(BINDIR)/test_pdf_tt_glyphs --nonuniform --gray
	python3 scripts/test_pdf_tt_composite_transforms.py $(BINDIR)/test_pdf_tt_glyphs --nonuniform

$(BINDIR)/test_pdf_tt_loader: tests/test_pdf_tt_loader.c $(wildcard tests/pdf_tt_loader/*.c) tests/fault_alloc.h tests/fixtures/pdf-truetype.inc $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

test-pdf-tt-vm: $(BINDIR)/test_pdf_tt_vm $(BINDIR)/test_pdf_tt_glyphs
	$(BINDIR)/test_pdf_tt_vm
	python3 scripts/test_pdf_tt_vm.py $(BINDIR)/test_pdf_tt_vm
	python3 scripts/test_pdf_tt_vm_reference.py $(BINDIR)/test_pdf_tt_vm
	python3 scripts/test_pdf_tt_cvt_precision.py $(BINDIR)/test_pdf_tt_vm
	python3 scripts/test_pdf_tt_projection_precision.py $(BINDIR)/test_pdf_tt_glyphs
	python3 scripts/test_pdf_tt_prep_reference.py $(BINDIR)/test_pdf_tt_vm
	python3 scripts/test_pdf_tt_points_reference.py $(BINDIR)/test_pdf_tt_vm
	python3 scripts/test_pdf_tt_state_reference.py $(BINDIR)/test_pdf_tt_vm
	python3 scripts/test_pdf_tt_cvt_lifetime.py $(BINDIR)/test_pdf_tt_vm
	python3 scripts/test_pdf_tt_glyphs_reference.py $(BINDIR)/test_pdf_tt_glyphs

test-pdf-empty-glyphs: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_empty_glyphs.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-pdf-combined-paint: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_combined_paint.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-pdf-font-descriptors: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_font_descriptors.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-pdf-substitution: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_substitution.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-pdf-font-resources: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_font_resources.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-pdf-nchannel: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_nchannel.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

$(BINDIR)/test_pdf_tt_glyphs: tests/test_pdf_tt_glyphs.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

$(BINDIR)/test_pdf_tt_vm: tests/test_pdf_tt_vm.c $(wildcard tests/pdf_tt_vm/*.c) $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

$(BINDIR)/test_pdf_icc_mab: tests/test_pdf_icc_mab.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

test-pdf-icc-mab: $(BINDIR)/test_pdf_icc_mab $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_icc_mab.py $(BINDIR)/test_pdf_icc_mab $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

$(BINDIR)/test_pdf_icc_lut: tests/test_pdf_icc_lut.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

test-pdf-intents: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_intents.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

test-pdf-icc-lut: $(BINDIR)/test_pdf_icc_lut $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom
	python3 scripts/test_pdf_icc_lut.py $(BINDIR)/test_pdf_icc_lut $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom

$(BINDIR)/test_pdf_coverage_oom: src/formats/pdf/raster/coverage.c src/formats/pdf/raster/coverage.h third_party/freetype-raster/ftgrays.inc third_party/freetype-raster/ftimage.h tests/fault_alloc.h

$(BINDIR)/test_pdf_tt_vm: tests/pdf_tt_vm_sources.inc
$(BINDIR)/test_pdf_tt_loader $(BINDIR)/test_pdf_clip_oom: tests/pdf_tt_loader_sources.inc tests/pdf_tt_vm_sources.inc

$(BINDIR)/test_pdf_page_hint: tests/fixtures/pdf-truetype.inc src/formats/pdf/fonts/page_hint.h src/formats/pdf/fonts/program.h

$(BINDIR)/test_pdf_ps_gridfit: tests/test_pdf_ps_gridfit.c tests/fault_alloc.h $(SRCDIR)/formats/pdf/fonts/cff.c $(SRCDIR)/formats/pdf/fonts/cff.h $(SRCDIR)/formats/pdf/fonts/type2.c $(SRCDIR)/formats/pdf/fonts/type2.h $(wildcard $(SRCDIR)/formats/pdf/fonts/postscript/*) $(wildcard third_party/freetype-pshinter/*) $(LIB) | $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(LIB) -o $@ $(LDLIBS)

test-pdf-ps-gridfit: $(BINDIR)/test_pdf_ps_gridfit
	$(BINDIR)/test_pdf_ps_gridfit
	python3 scripts/test_pdf_ps_gridfit_reference.py $(BINDIR)/test_pdf_ps_gridfit
	python3 scripts/test_pdf_ps_gridfit_reference.py $(BINDIR)/test_pdf_ps_gridfit --nonuniform
	python3 scripts/test_pdf_ps_gridfit_reference.py $(BINDIR)/test_pdf_ps_gridfit --bitmaps

test-pdf-cff-page-hinting: $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom $(CLI)
	python3 scripts/generate_pdf_cff_page_hinting_fixture.py --check
	python3 scripts/test_pdf_cff_page_hinting.py $(BINDIR)/test_pdf_pages $(BINDIR)/test_pdf_clip_oom $(CLI)

$(BINDIR)/test_asset_probe $(BINDIR)/test_pdf_pages: tests/pdf_probe_options.h
