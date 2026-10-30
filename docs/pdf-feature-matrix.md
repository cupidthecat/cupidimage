# PDF feature inventory

The [machine-readable inventory](pdf-feature-matrix.json) records 263 requirements
from the expanded feature checklist. It is a starting inventory, not an exhaustive
PDF specification or a completed clause-by-clause audit. The standards index is
included; published editions and extension applicability still need verification
where marked unassessed.

## Reading the status

Each requirement has a stable section/item ID, implementation status, source/test
evidence, limitations, dependencies and space for exact standard clauses.

- **Missing:** a known unsupported capability. Implementing its prerequisites
  does not count as implementing it.
- **Partial:** selected paths exist and have evidence, but the complete stated
  requirement and its interactions have not been established.
- **Implemented-unverified:** implementation exists without enough qualification
  to call the requirement verified.
- **Verified:** the stated scope has standards, positive, negative and boundary
  evidence, with the comparison configuration recorded.
- **Null status / unassessed audit:** no implementation judgment has been made
  for that row. It must not be counted as supported or silently treated as missing.

A partial row's linked tests cover selected behavior, not necessarily every phrase
in its requirement. The initial pass has no verified rows. This does not erase
focused test results; it prevents those results from being mistaken for full
coverage of broad compound requirements. Counts are not completion percentages.

Run `make test-pdf-feature-matrix` to validate IDs, statuses, dependencies and
local evidence paths. This checks the inventory, not PDF conformance.

## Acceptance targets

1. **Static page imaging:** fonts/text, page graphics, images, colors, transparency,
   clipping, layers, annotations and current form appearances.
2. **Extraction, navigation and accessibility:** a separate target from pixels.
3. **Interactive documents:** explicit policies and deliberate user actions are
   required for scripts, external navigation, launch actions and data submission.
4. **Editing and writing:** separate from rendering, including actual redaction,
   annotation editing and saved form changes.

A static appearance is not proof of interactive support. A visible signature is
not cryptographic verification. Rendering a document from a conformance family
is not validation of that family's requirements.

## Work order and evidence

P0 parsing, stream, page/resource and text prerequisites come first. The font and
text work is tracked in [issue #50](https://github.com/cupidthecat/cupidimage/issues/50).
P1 completes page imaging and appearances; P2 covers the separate viewer targets.
Dependencies can cross these labels. The current implementation and focused test
commands are described in the [font](pdf-fonts.md), [image](pdf-images.md),
[object](pdf-objects.md), [shading](pdf-shading.md), [pattern](pdf-patterns.md) and
[transparency](pdf-transparency.md) guides.

Add newly discovered specification rules or corpus cases even if they are absent
from this inventory. Match raster dimensions, page boxes, rotation, fonts, layers,
color settings and appearance settings in comparisons. Compare canonical page
pixels before terminal encoding. Keep the full 1,007-document manual rating pass
deferred while major rendering features remain missing.
