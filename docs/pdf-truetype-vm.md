# TrueType instruction engine

The native instruction engine is a tested foundation for hinting. **The page
renderer does not invoke it yet.** Glyphs still use unhinted outlines. Successful
font-program initialization must not be reported as grid fitting or hinted-rendering
support.

## Current scope

`src/formats/pdf/fonts/truetype_vm` contains per-instance stack, storage, control
value and function-definition state. It supports:

- Fixed and variable byte/word pushes, with unsigned bytes and signed words.
- Duplicate, pop, swap, clear, depth, indexed copy/move and roll operations.
- Signed comparisons, Boolean operations, minimum/maximum, checked addition,
  subtraction, multiplication, division, absolute value and negation, plus F26Dot6
  floor and ceiling.
- The six predefined rounding modes, SROUND, ROUND/NROUND and ODD/EVEN.
- Axis-aligned, stack-defined and point-derived projection/freedom vectors,
  dual projection, vector reads and copying projection to freedom.
- Owned twilight/glyph point zones, current/original coordinate reads, SCFS,
  MDAP, MIAP, MDRP, MIRP, MSIRP, MD, ALIGNRP, IP, IUP, UTP, DELTAP1/2/3, SHP and SHPIX.
- Reference-point and zone selectors, loop count, minimum distance, control-value
  and single-width cut-ins, delta base/shift and automatic sign flipping.
- Storage reads/writes, pixel- and font-unit control-value writes, uniform CVT
  scaling and all three CVT delta instruction ranges.
- Pixel/point-size queries, single-width scaling, legacy grayscale queries,
  instruction-control flags and retained scan-conversion settings.
- IF/ELSE/EIF, relative conditional/unconditional jumps, function definitions,
  calls and repeated calls.

PUSH payload bytes are decoded correctly while skipping branches or collecting
function bodies. They cannot be mistaken for structural opcodes. Function IDs are
bounded by the declared limit, nested definitions are rejected, and definitions
survive between executions. Each low-level execution starts with an empty operand stack. Other state persists
until a size or glyph-entry operation resets it. Storage starts at zero as a
deterministic implementation choice.

The font loader retains immutable views of `cvt `, `fpgm` and `prep` alongside the
other TrueType tables. This does not execute those tables during PDF loading.
The standalone tests run the `fpgm` initialization stream of all 12 pinned Latin
faces and install 71 function definitions from each. They also run the complete
`prep` streams at sizes 1 through 96 ppem in monochrome and grayscale configurations,
for 2,304 preparation runs. The pinned faces' complete glyph instruction streams are not executed yet.

## Ownership and limits

The caller owns the VM and the program bytes. Definition bodies borrow their
original byte ranges, so the source bytes must remain alive until the VM is freed.
Initialization unwinds all allocations on failure. A failed execution invalidates
the instance; callers must destroy and reinitialize it before reuse.

Stack, storage, control-value and function limits are bounded to 65,535 entries.
A program is limited to 1 MiB, individual function bodies to 64 KiB, and calls may
nest up to 64 levels. A caller-supplied
work budget covers execution, PUSH values, indexed stack movement, definition
scans, repeated dispatch and each CVT delta pair. Relative jumps cannot escape their current stream.
Truncated operands, bad indices, undefined calls, overflow, exhausted work and
unsupported instructions have distinct failure categories. The engine does not
silently ignore an unsupported instruction.

Arithmetic that cannot fit the supported signed 32-bit result fails explicitly.
The implementation uses wider intermediates to avoid undefined C overflow. Division
truncates toward zero. Multiplication quantizes to the nearest F26Dot6 value, with
ties away from zero; this precision choice is independent of the graphics rounding
mode. Engine-color compensation is zero. Rounding is symmetric around zero, with
half-grid and phased modes preserving their nearest valid signed value. Reserved
ROUND/NROUND/MIRP color selectors and the reserved SROUND period are rejected.

SROUND uses exact F26Dot6 periods of 32, 64 or 128 units, including all four phases
and sixteen threshold selections. S45ROUND is still unsupported: its diagonal
period and intermediate quantization need separate verification. Coordinate-axis
projection updates also update the dual projection vector. Stack-defined vectors
use signed 2.14 components and bounded fixed-point normalization. The math routine
has required attribution in [the third-party notices](../THIRD_PARTY_NOTICES.md).

## Uniform size preparation

`cupidimage_pdf_tt_vm_set_size` takes units per em, F26Dot6 pixel and point sizes,
a grayscale flag and unscaled control values. It forms a checked 16.16 scaling
coefficient and converts the control values to F26Dot6, rounding ties away from
zero. Size changes reset graphics state and storage, clear twilight coordinates and
release the previous glyph geometry. Function definitions remain available.
WCVTF and SSW use the same coefficient. DELTAC1/2/3 apply only at the selected ppem
and support delta shifts 0 through 6. Every pair is bounded and charged to the
execution budget, including pairs that do not match the current size.

This interface currently represents uniform, unrotated sizing. GETINFO exposes the
legacy version-35 configuration and its grayscale flag; it does not advertise
variations or LCD rendering. MPS returns the supplied F26Dot6 point size. Nonuniform
transforms, projection-dependent CVT scaling and rotation metadata remain open.

Execution distinguishes font, preparation and glyph stages. Font and preparation
streams may define functions. INSTCTRL records valid flags in the permitted stage;
glyph execution is skipped when preparation has inhibited it. The defaults-selection
flag is applied at explicit glyph entry. SCANCTRL and
SCANTYPE preserve their settings, but no scan converter consumes them. A preparation
program has no associated glyph outline: glyph-coordinate queries return zero and
outline-specific point operations have no effect. Twilight operations remain
available when that zone has been allocated.

## Prepared state and glyph entry

After a successful preparation program, `cupidimage_pdf_tt_vm_save_preparation`
records its control values, storage and graphics settings in owned snapshots.
`cupidimage_pdf_tt_vm_begin_glyph` replaces only glyph-zone geometry and restores
those snapshots. Writes made by one glyph therefore do not change the prepared
CVT or storage seen by the next glyph.

Glyph entry initializes vectors, reference points, zone selectors and loop count,
and starts rounding at round-to-grid. Other prepared graphics settings are retained
unless INSTCTRL requests implementation defaults. Glyph inhibition remains effective
even when the other prepared settings are discarded. The operand stack is cleared.

Twilight points deliberately persist between glyphs, as specified by the font
instruction model. New glyph geometry must not reset that zone. A size change
clears it and invalidates the saved preparation; a new preparation program and
snapshot are required before another glyph entry. Running a new font/preparation
stream also invalidates the previous snapshot.

Snapshot and geometry allocations have bounded sizes and unwind on failure.
Repeated snapshots reuse their buffers. These are explicit low-level interfaces;
the [native simple-glyph loader](pdf-truetype-loader.md) uses them, but the PDF
renderer does not. Composite glyph loading remains open.

## Point operations

`cupidimage_pdf_tt_vm_set_zones` copies caller-supplied, already-scaled geometry
into owned twilight and glyph zones. Current and original coordinates, contour
flags and touched-axis flags are retained separately. The glyph count may include
phantom points; the outline count excludes them. Twilight points start at zero.
Allocations unwind on failure, and failed replacement retains the previous buffers
until the invalidated instance is destroyed. Limits are 65,535 twilight points and
65,540 glyph points, including up to four phantom points.

`cupidimage_pdf_tt_vm_set_design` optionally installs a separate, owned array of
original design coordinates and its positive uniform 16.16 scale. Simple glyphs
need this plane: rounding each point before subtracting loses small design-space
distances. MDRP and original-distance MD measure in this plane before scaling.
IP uses design-space ratios without rounding them to device coordinates first.
When no design array is supplied, original coordinates describe an already-scaled
outline, as needed during a composite instruction phase. Geometry replacement and
size changes release the design array. This interface does not extract glyphs.

GC selects current or original coordinates and the corresponding projection.
SCFS moves along the freedom vector and records touched axes; twilight movement
also establishes the original coordinate. MDAP applies optional rounding and sets reference points 0 and 1. MIAP uses a CVT
coordinate, with optional cut-in and rounding based on the coordinate entering the
instruction. In the twilight zone it creates the original coordinate from the CVT
and freedom vector before applying the final move. UTP clears touched axes selected
by the freedom vector.
Line-vector instructions use their selected zones and preserve the distinct
original/current directions for dual projection. Coincident lines use a horizontal
fallback. A zero stack-defined vector preserves the existing direction.

DELTAP1/2/3 use the selected size range, delta base and shift to move only matching
points. They preserve original coordinates. SHP computes the reference point's
displacement once, then applies it to the selected points, including repeated
indices or the reference point itself if listed. SHPIX measures its displacement
along the freedom vector without using projection. SHP and SHPIX reset the loop
count after successful execution. Every target is bounded and charged to the work
budget, even when a size-specific exception does not match.

MIRP applies single-width substitution, automatic sign selection, same-zone CVT
cut-in, optional rounding and minimum distance in instruction order. It reads
original distance through dual projection and current distance through projection.
The compatibility CVT index -1 supplies zero without accessing the table; other
invalid indices fail. MSIRP takes an unrounded distance from the stack, without
applying CVT cut-in or minimum distance. Both update reference points as requested
and construct twilight originals before the current move. ALIGNRP moves a looped
set of points to the projected coordinate of reference point 0.

MDRP measures an original outline distance, then applies single-width substitution,
optional rounding and minimum distance. MD opcode 0x49 measures current positions;
0x4A measures original positions. Both use zone 0 for the deeper stack operand and
zone 1 for the top operand. Original distances involving twilight points use their
scaled original coordinates instead of a font design plane.

IP interpolates the listed points between reference points 1 and 2, preserves
reference settings and resets the loop count. A zero original reference span is
invalid and fails explicitly. IUP walks each contour independently along the
selected axis, leaving touched points and phantom points unchanged. It interpolates
between touched anchors, shifts points outside their original interval and preserves
touched flags. Contour scans and moves are bounded by the work budget; missing
contour ends and a twilight-zone IUP are rejected.

Movement uses checked fixed-point projection and division. Its 2.14 coupling
coefficient uses explicit floor division, including for negative vector products. A changing-coordinate
request with a zero freedom/projection dot product fails explicitly. Near-perpendicular
vector pairs need separate qualification. These operators are tested on supplied
geometry. Native simple-glyph extraction and phantom construction are tested
separately. Composite loading and the remaining point operators are still required
before rendering integration.

## Verification

`make test-pdf-tt-vm` runs deterministic instruction/bounds checks and allocation
failure recovery, then checks the pinned fonts' initialization streams and retained
table extents. Randomized integer inputs are compared with explicit arithmetic and
Boolean expectations. Rounding tests cover all six modes over signed distances,
all 192 defined SROUND selectors, parity, halfway cases, overflow, reserved operands
and stack underflow. Graphics-state tests check defaults, changes and persistence.
The point tests check coordinate/original-coordinate ownership, touched flags,
reference points, bounds and allocation recovery. These are instruction-level
oracles, not complete hinted-glyph comparisons.

When the optional `fontTools` and `freetype-py` test packages are available,
`scripts/test_pdf_tt_vm_reference.py` also compares 4,068 native expression results
with a separate interpreter. It places each result in a synthetic outline point
using SCFS and checks that coordinate after monochrome hint execution. This
qualifies the tested arithmetic and rounding instructions only; page rendering
remains unhinted.

`scripts/test_pdf_tt_prep_reference.py` checks 181,676 prepared control values across
the 12 pinned faces, 22 sizes and both monochrome and grayscale configurations.
Temporary, renamed copies of the fonts add probe glyphs and clear glyph inhibition
after preparation so each CVT entry can be observed independently. That extra
instruction does not change the CVT. Only the independent interpreter executes the
probe glyphs in this CVT comparison.

`scripts/test_pdf_tt_points_reference.py` separately runs 23,704 authored
programs at 13, 37, 64 and 97 ppem on supplied triangle/twilight geometry. There are
94,812 matching comparisons and four recorded IUP differences in the tested engines. It covers point movement, CVT
cut-in boundaries, all delta size/step selections, looped shifts, relative
positioning flags, design-space scaling, interpolation, single-width boundaries, mixed-zone/twilight cases, rounding,
original-coordinate reads and stack/line vector selection. It does not run the
pinned fonts' full glyph programs or the PDF renderer.

The IUP difference concerns repeated interpolation with a single touched anchor.
The instruction contract says a later IUP ignores previous interpolation of an
untouched point. CupidImage recomputes the point from its original coordinate;
the tested engines add the anchor's shift again. An analytic regression checks the
specified result, and the independent test reports that difference without counting
it as agreement. Repeated interpolation with two anchors matches independently.

`scripts/test_pdf_tt_glyphs_reference.py` compares 9,330 simple-glyph outlines from
the 12 pinned fonts, selecting Unicode mappings U+0020 through U+00FF, at 6, 9,
13, 16, 24 and 37 ppem in legacy monochrome mode.
It supplies independently extracted geometry, original design points and phantom
points, then runs the original font, preparation and glyph instruction streams.
The comparison checks every resulting outline coordinate. A broader diagnostic at
`--all-simple` mode at 16 ppem matched 17,462 simple-glyph outlines and rejected four programs with a zero
original reference span at IP, which the instruction contract declares invalid.
Those rejected programs are not counted as matched or fully supported. Neither probe
tests the native glyph loader, composite assembly, grayscale execution or PDF rasterization.

Remaining work includes diagonal rounding, custom instruction definitions,
nonuniform sizing, composite assembly and phantom propagation, the remaining point
instructions and integration with transformed outline rendering.
Existing PDF advances and resource ownership must remain intact
when that integration is implemented.

`scripts/test_pdf_tt_state_reference.py` checks paired-glyph sequences for prepared
CVT/storage isolation, entry defaults, instruction controls, definitions and
persistent twilight coordinates. In the tested configurations, 103 sequences match
independently and one specified-defaults case differs. Targeted allocation-failure
tests cover snapshots and glyph replacement. Controls affecting unfinished
movement or scan-conversion features still need rendering-level verification.

The differing case sets a three-pixel minimum distance in preparation, then uses
INSTCTRL selector 2 to request implementation defaults. CupidImage restores the
one-pixel default, as required by the [instruction contract](https://learn.microsoft.com/en-us/typography/opentype/spec/tt_instructions).
The tested comparison engines retain the modified minimum. The regression test
checks the specified native result and reports that reference difference separately;
it does not count it as agreement. A comparison engine that produces the specified
result is also accepted.

The instruction contracts follow the [OpenType instruction set](https://learn.microsoft.com/en-us/typography/opentype/spec/tt_instructions),
its [graphics-state defaults](https://learn.microsoft.com/en-us/typography/opentype/spec/tt_graphics_state)
and Apple's [TrueType instruction reference](https://developer.apple.com/fonts/TrueType-Reference-Manual/RM05/Chap5.html).
The [font guide](pdf-fonts.md) and [issue #50](https://github.com/cupidthecat/cupidimage/issues/50)
track the broader rendering requirements and limitations.
