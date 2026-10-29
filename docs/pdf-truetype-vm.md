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
  subtraction, absolute value and negation, plus F26Dot6 floor and ceiling.
- Storage reads/writes and pixel-unit control-value reads/writes.
- IF/ELSE/EIF, relative conditional/unconditional jumps, function definitions,
  calls and repeated calls.

PUSH payload bytes are decoded correctly while skipping branches or collecting
function bodies. They cannot be mistaken for structural opcodes. Function IDs are
bounded by the declared limit, nested definitions are rejected, and definitions
survive between executions. Each top-level execution starts with an empty operand
stack; storage and control values persist. Storage starts at zero as a deterministic
implementation choice.

The font loader retains immutable views of `cvt `, `fpgm` and `prep` alongside the
other TrueType tables. This does not execute those tables during PDF loading.
The standalone tests run the `fpgm` initialization stream of all 12 pinned Latin
faces and install 71 function definitions from each. Point-manipulating instructions
inside those function bodies have not been executed by this implementation.

## Ownership and limits

The caller owns the VM and the program bytes. Definition bodies borrow their
original byte ranges, so the source bytes must remain alive until the VM is freed.
Initialization unwinds all allocations on failure. A failed execution invalidates
the instance; callers must destroy and reinitialize it before reuse.

Stack, storage, control-value and function limits are bounded to 65,535 entries.
A program is limited to 1 MiB, and calls may nest up to 64 levels. A caller-supplied
work budget covers execution, PUSH values, indexed stack movement, definition
scans and repeated dispatch. Relative jumps cannot escape their current stream.
Truncated operands, bad indices, undefined calls, overflow, exhausted work and
unsupported instructions have distinct failure categories. The engine does not
silently ignore an unsupported instruction.

Arithmetic that cannot fit the supported signed 32-bit result fails explicitly.
The implementation uses wider intermediates to avoid undefined C overflow. Control
values supplied to this foundation are already in pixel units; font-unit scaling
and size-specific preparation remain future work.

## Verification

`make test-pdf-tt-vm` runs deterministic instruction/bounds checks and allocation
failure recovery, then checks the pinned fonts' initialization streams and retained
table extents. Randomized integer inputs are compared with explicit arithmetic and
Boolean expectations. These are instruction-level oracles, not independent hinted
glyph comparisons.

Remaining work includes the rest of the arithmetic/rounding instructions, custom
instruction definitions, graphics state and vectors, point zones, phantom points,
CVT scaling, size preparation, glyph instructions and integration with transformed
outline rendering. Existing PDF advances and resource ownership must remain intact
when that integration is implemented.

The instruction contracts follow Apple's [TrueType instruction reference](https://developer.apple.com/fonts/TrueType-Reference-Manual/RM05/Chap5.html).
The [font guide](pdf-fonts.md) and [issue #50](https://github.com/cupidthecat/cupidimage/issues/50)
track the broader rendering requirements and limitations.
