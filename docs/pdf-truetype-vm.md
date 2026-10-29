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
- Axis-aligned projection/freedom vectors, vector reads and copying the projection
  vector to the freedom vector.
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
survive between executions. Each top-level execution starts with an empty operand
stack; storage, control values and graphics state persist. Storage starts at zero
as a deterministic implementation choice.

The font loader retains immutable views of `cvt `, `fpgm` and `prep` alongside the
other TrueType tables. This does not execute those tables during PDF loading.
The standalone tests run the `fpgm` initialization stream of all 12 pinned Latin
faces and install 71 function definitions from each. They also run the complete
`prep` streams at sizes 1 through 96 ppem in monochrome and grayscale configurations,
for 2,304 preparation runs. Point-manipulating instructions
inside those function bodies have not been executed by this implementation.

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
ROUND/NROUND color operands and the reserved SROUND period are rejected.

SROUND uses exact F26Dot6 periods of 32, 64 or 128 units, including all four phases
and sixteen threshold selections. S45ROUND is still unsupported: its diagonal
period and intermediate quantization need separate verification. Coordinate-axis
projection updates also update the dual projection vector. Arbitrary and
point-derived vectors remain unsupported. Point numbers are stored, but their
zone bounds cannot be checked until point execution is implemented.

## Uniform size preparation

`cupidimage_pdf_tt_vm_set_size` takes units per em, F26Dot6 pixel and point sizes,
a grayscale flag and unscaled control values. It forms a checked 16.16 scaling
coefficient and converts the control values to F26Dot6, rounding ties away from
zero. Size changes reset graphics state while retaining storage and definitions.
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
flag is retained for future per-glyph setup, which is not implemented yet. SCANCTRL
and SCANTYPE preserve their settings, but no scan converter consumes them. These
low-level calls do not yet create glyph zones, restore prepared defaults for each
glyph or run point instructions.

## Verification

`make test-pdf-tt-vm` runs deterministic instruction/bounds checks and allocation
failure recovery, then checks the pinned fonts' initialization streams and retained
table extents. Randomized integer inputs are compared with explicit arithmetic and
Boolean expectations. Rounding tests cover all six modes over signed distances,
all 192 defined SROUND selectors, parity, halfway cases, overflow, reserved operands
and stack underflow. Graphics-state tests check defaults, changes and persistence.
Point instructions do not consume that state yet. These are instruction-level
oracles, not independent hinted-glyph comparisons.

When the optional `fontTools` and `freetype-py` test packages are available,
`scripts/test_pdf_tt_vm_reference.py` also compares 4,068 native expression results
with a separate interpreter. It places each result in a synthetic outline point
using SCFS and checks that coordinate after monochrome hint execution. This
qualifies the tested arithmetic and rounding instructions only; the native engine
still does not execute SCFS or render hinted glyphs.

`scripts/test_pdf_tt_prep_reference.py` checks 181,676 prepared control values across
the 12 pinned faces, 22 sizes and both monochrome and grayscale configurations.
Temporary, renamed copies of the fonts add probe glyphs and clear glyph inhibition
after preparation so each CVT entry can be observed independently. That extra
instruction does not change the CVT. Native point instructions remain untested by
this comparison; only the independent interpreter executes the probe glyphs.

Remaining work includes diagonal rounding, custom instruction definitions,
nonuniform sizing and arbitrary vectors, point zones, phantom points, per-glyph
state restoration, glyph instructions and integration with transformed
outline rendering. Existing PDF advances and resource ownership must remain intact
when that integration is implemented.

The instruction contracts follow the [OpenType instruction set](https://learn.microsoft.com/en-us/typography/opentype/spec/tt_instructions),
its [graphics-state defaults](https://learn.microsoft.com/en-us/typography/opentype/spec/tt_graphics_state)
and Apple's [TrueType instruction reference](https://developer.apple.com/fonts/TrueType-Reference-Manual/RM05/Chap5.html).
The [font guide](pdf-fonts.md) and [issue #50](https://github.com/cupidthecat/cupidimage/issues/50)
track the broader rendering requirements and limitations.
