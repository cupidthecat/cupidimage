# TrueType composite glyphs

The native hinting loader assembles composite glyphs from their components and
executes the parent instruction stream. This is still separate from PDF rendering;
page output remains unhinted. See [native loading](pdf-truetype-loader.md) and the
[instruction engine](pdf-truetype-vm.md) for the surrounding state and limits.

## Assembly order

The loader processes components in their stored order. Each child is loaded and
instructed at the selected uniform size before its component transform and placement
are applied. Nested composites follow the same sequence. Component transformations
round each matrix product to F26Dot6 before adding it to the other product.

XY offsets use the parent's coordinate space by default. With
SCALED_COMPONENT_OFFSET, the full component matrix also transforms the offset.
ROUND_XY_TO_GRID rounds the resulting device offset when hinting is enabled.
Conflicting scaled/unscaled flags use the specified default, unscaled behavior.

Point placement aligns a transformed child point with an already incorporated
parent point. The native path also accepts parent and child phantom anchors. A
bounded counting pass establishes the final parent point count so that phantom
indices cannot be confused with future contour points. The first component must
use an XY offset, and out-of-range attachment indices fail explicitly.

USE_MY_METRICS retains the child's instructed phantom metrics. The component's
transform and translation do not alter those inherited metrics. The specification
leaves rotated-component metric inheritance undefined; the tested policy still
retains the child metrics.

## Parent instructions and shared state

The parent's original coordinates are its assembled, already-instructed child
coordinates in device space. Its design-distance scale is unity. Child touched-axis
flags are cleared before parent instructions, while on-curve flags and contour ends
are retained. Nonempty parent instruction streams also round the parent phantom
positions. A parent without instruction bytes retains its assembled phantom values.

A top-level glyph receives private copies of the prepared CVT and storage values.
Its child, sibling and parent instruction phases share those private writes. Each
component resets graphics settings and the operand stack, not the private tables.
Loading the next top-level glyph restores the prepared table snapshots. Twilight
points retain their defined lifetime across these entries.

The instruction stream is recognized when WE_HAVE_INSTRUCTIONS appears on any
component record, not just the last one. Any negative contour count denotes a
composite; -1 is the recommended encoding. A contour-less composite can execute
instructions against its phantom points.

The unhinted reader shares the component-record decoder, including negative contour
counts, transform validation and conflicting-offset defaults. It does not yet use
the native hinting assembly path or its phantom-attachment support. It now retains
the effective unhinted advance with the assembled outline, including nested
USE_MY_METRICS inheritance. The PDF reader's existing damaged-input fallback uses that value
when both Widths and MissingWidth are absent. Explicit PDF metrics retain
precedence; missing required entries do not become conforming through this recovery.

## Bounds

Counting, decoding, component placement and instructions consume one glyph work
budget across the complete tree. Limits are 16 recursive levels, 4,096 load calls
and 65,536 accumulated outline points, followed by four phantom points. Counting
passes consume budget too. Cycles, excessive expansion, truncated records,
conflicting transform encodings, invalid indices and arithmetic overflow fail
without returning a partially successful glyph. Allocation failures unwind both
component buffers and the enclosing instance.

## Tests and comparison scope

The selected pinned-font probe compares 4,242 composite outlines at six sizes in
each of monochrome and grayscale mode. The optional all-composite probe compares
12,596 outlines at 16 ppem in each mode. Those fonts do not contain nested,
transformed or point-attached components, so their passing results are not evidence
for those separate features.

Authored tests cover those missing structures, inherited and rounded metrics,
parent instructions, private table sharing, contour shifts, malformed graphs,
work limits and allocation failures. The independent authored-font probe has 254
matching cases and one separately reported scaled-offset difference. In that case,
native placement applies the full matrix required by the glyph-table contract;
the tested engines use a different offset calculation. That nonmatch is not counted
as agreement. Phantom attachments, early instruction flags and contour-less parent
instructions have analytic coverage rather than a claim of independent parity.

These tests check point geometry and instruction state. They do not verify raster
coverage, dropout control, remaining VM instructions, nonuniform size transforms or
complete PDF/font fidelity.

The data and placement rules follow the official [OpenType glyph table](https://learn.microsoft.com/en-us/typography/opentype/spec/glyf).
Instruction contracts follow the [OpenType instruction set](https://learn.microsoft.com/en-us/typography/opentype/spec/tt_instructions).

The selected outline sweep also runs six unequal x/y size pairs in monochrome and
grayscale. Authored transform cases exercise unequal sizes separately from the
post-instruction device matrix. These remain geometry tests, not PDF raster tests.
