# PDF optional content

The renderer uses the document's default optional-content configuration when
painting a page. Hidden layers no longer paint text, paths, images or Forms.
Hiding content does not undo its text advances, graphics-state changes or
clipping.

This is a static rendering profile. It does not add a layer panel, execute
SetOCGState actions, or apply the interactive View/Print/Export usage adjustments
in AS dictionaries. Alternate configurations and user-controlled layer states
remain unimplemented. ISO 32000-1 section 8.11.4.5 distinguishes the initial
configuration used for final-form output from interactive usage adjustments;
ISO 32000-2 sections 6.3.2.2 and 6.3.2.3 make the corresponding distinction
between rendering and interactive processors.

## Configuration and identity

OCProperties in the catalog supplies the group list and default D configuration.
Groups are identified by their indirect object identities, not their display
names. Object-stream groups, indirect configuration values, reference aliases
and later revisions use the existing revision-aware resolver.

The initial state is ON. The ON and OFF arrays update the listed groups. OFF and
Unchanged are also accepted as compatibility values for BaseState in D, although
the standard requires ON when that entry is present in the default configuration.
Unchanged starts from ON because a fresh static render has no earlier user state.

A group's Intent must intersect the configuration's Intent to participate in
visibility. Both default to View. Groups with no matching intent are excluded
from membership tests, including negated expressions. An empty configuration
intent array makes all optional content visible.

## Membership and nesting

OCMD membership dictionaries support AnyOn, AllOn, AnyOff and AllOff. VE
expressions support nested And, Or and Not, and take precedence over the simpler
membership policy. Missing or empty OCGs lists, or lists containing only null or
deleted objects, have no effect on visibility. Groups excluded by intent likewise
have no effect.

An OC-tagged BDC section can select a group or membership dictionary through the
current Properties resources. Nested sections are visible only when every outer
section is visible. Other marked-content tags and property dictionaries retain
their metadata role. Inline membership dictionaries are also accepted. An
unresolved Properties name currently has no visibility effect; lookup failures
such as allocation errors still fail the page.

EMC restores the previous marked-content visibility. q and Q restore graphics
state, not the marked-content stack. Form and image XObjects also honor their OC
entries before executing their content. The same evaluator is available for
annotation rendering, which is not implemented by this change.

## Painting and state

Hidden text keeps its encoded advances, spacing and TJ adjustments. Text rendering
modes that contribute to clipping still build the glyph clip, even when the glyph
is not painted. This includes embedded TrueType, CFF and Type 1 outlines. Type 3
programs retain their separate text-mode rules.

Visibility is checked when an object requests painting, rather than when earlier
objects are finally composited. A visible glyph painted before a hidden span is
therefore retained when the text object's accumulated surface is composed later.

Colour, transforms, graphics-state settings, clipping and compatibility scopes
inside hidden spans still take effect. Ending a hidden path paint consumes the
path without rasterizing it. Masks and patterns constructed inside a hidden span
start their own painting context: they can still be used after the span ends,
and their own optional-content sections are evaluated normally.

Hidden inline images still require enough parsing to find the true EI boundary.
Their samples are not painted. Some filters require bounded decoding to locate
that boundary; hiding an inline image does not make arbitrary malformed data safe
to skip.

## Limits and verification

A page has a shared 16,777,216-unit optional-content work budget. Configuration
input is limited to 65,536 group entries, 64 intent names of up to 127 decoded
bytes, and the existing indirect-reference depth of 16. Visibility expressions
and marked-content nesting are each bounded at 64 levels. Invalid expressions,
cycles, exhausted work or allocation failures return an error, not a partially
rendered page.

Run `make test-pdf-optional-content`. The tests cover state policies, intent
selection, expression and input limits, deleted references, aliases, compressed
objects, incremental updates, hidden text/clipping, resource scopes, masks,
patterns and allocation failures. The owned optional-content-state.pdf asset has
literal RGB pixel expectations and matches an independent 96-DPI raster exactly.
The optional-content-text.pdf asset is a visual sample whose whole-page raster
remains decoded-unverified. Rebuild them with
`scripts/generate_pdf_optional_fixtures.py`.

Sixteen independent membership-policy pages match exactly. A separate eight-case
intent comparison has two known differences: the independent renderer hides an
AnyOn membership containing only a group excluded by intent, while the native
renderer follows the standard's no-effect rule. These differences are not counted
as matches or hidden by a tolerance.

The implementation follows ISO 32000-1/2 section 8.11. Broader annotation,
interactive configuration, usage-context and whole-document fidelity work remains
open in the [feature inventory](pdf-feature-matrix.md).

A focused first-page audit of eight hash-verified corpus files containing
OCProperties found two rendered pages, five page-rendering failures and one
cross-reference failure. The two successful rasters are unchanged from the
baseline and still differ from the independent raster. The remaining files stop
in pre-existing image, font-resource, transparency or content-operator paths.
This audit is not evidence of full corpus support or eight successful pages.
