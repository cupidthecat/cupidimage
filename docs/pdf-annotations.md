# PDF annotation appearances

Static page rendering paints supplied annotation and widget appearances after the
page content, in Annots array order. This adds visible form fields, stamps and
markup when the document already contains their drawing instructions. It does not
generate missing appearances or edit form values.

## Appearance selection and placement

The renderer selects AP/N, the normal appearance. When N is a state dictionary,
AS names the selected stream. An absent or unknown state draws nothing; the
renderer does not guess the first entry. Rollover and pressed appearances are not
selected in a static preview.

The appearance is a Form XObject. Its BBox is transformed by Matrix, and the
smallest upright rectangle around the four transformed corners is mapped onto
Rect. Reversed rectangle corners are normalized. Zero-area rectangles and boxes
draw nothing. Form clipping still applies to the transformed BBox, including
rotated or sheared boxes.

Each appearance starts with the initial page graphics state. Page-content
transforms, clipping, colors, opacity, blend modes and soft masks do not leak into
it. Its own resources, nested Forms, fonts, images, patterns and graphics state
use the existing rendering pipeline and retain that pipeline's limits. Indirect
aliases preserve the final Form identity for resource lookup and recursion checks.

## Screen visibility and rotation

Hidden and NoView suppress painting. Invisible suppresses an unrecognized
annotation subtype; it does not hide a standard subtype. Print does not suppress
an onscreen appearance. Both an annotation's OC entry and the selected Form's OC
entry use the [default optional-content configuration](pdf-optional-content.md).

NoRotate keeps the appearance upright about its upper-left anchor during page
rotation. Text annotations have this behavior even when the flag is absent.
Quarter turns use exact coefficients. Crop origins and UserUnit still apply.
There is no interactive PDF zoom state: NoZoom does not disable UserUnit or the
CLI's later resizing of the completed page image.

Popup annotations describe editing windows and must not have appearance streams.
The static renderer does not open those windows or paint a supplied Popup AP.
Other supported names include Widget, Stamp and the standard markup types; an
unknown subtype can still display its normal appearance unless Invisible is set.

## Transparency

An appearance without Group is a non-isolated, non-knockout group. An explicit
transparency Group supplies its own isolation, knockout and blending-space
properties. The result is composited with Normal blend mode, alpha 1 and no soft
mask against the page content and earlier annotations. Page-content knockout does
not replace that immediate annotation backdrop.

The page's blending space is retained until the final output composite. The
appearance's own transparency instructions control painting; a markup annotation's
CA entry does not override an existing AP stream.

These rules follow ISO 32000-1, 12.5.2, 12.5.3 and 12.5.5, with Popup behavior from
12.5.6.14. The corresponding ISO 32000-2 clauses retain these requirements. See the
[PDF specification archive](https://pdfa.org/resource/pdf-specification-archive/)
and the [ISO 32000-1 text](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf).

## Limits and verification

Annotation traversal allows 65,536 entries, a 16 MiB metadata-work budget and at
most 16 indirect-reference resolutions per chain. Appearance execution shares the
page's existing Form call, decoded-stream, recursion, color-work and transparency
memory budgets. It does not receive a fresh budget for each annotation. Invalid
selected streams or exhausted limits return a rendering error. Missing appearances
and unresolved appearance states draw nothing.

Run `make test-pdf-annotations`: 279 page checks and 799 allocation-failure checks
cover normal and named-state selection,
flags, optional content, aliases, rectangle transforms, page rotation, UserUnit,
initial-state isolation, text/image/pattern/mask resources, ordering and transparency.
Compressed dictionaries, incremental updates, deleted objects and traversal limits
also have targeted cases. The two owned
reference assets have literal pixel expectations. Allocation-failure tests exercise
cleanup through the same page entry point used by the renderer.

Focused independent comparisons are not all exact. Right-angle NoRotate cases
show boundary-pixel differences in four of twelve small comparisons. Non-isolated
blend samples differ by at most two channel values; explicit isolated groups show
larger differences. Their behavior is checked against the specification and the
same group's ordinary Form rendering rather than changing isolation to match a
comparison image. Seven pinned annotation documents render with all five PDF
options enabled, but all seven retain pixel differences.

## Remaining work

- Appearance generation from annotation geometry, colors, borders and text.
- AcroForm field inheritance, DA/DR-based regeneration, NeedAppearances handling
  and updates when values change.
- Interactive rollover, pressed states, popup windows, actions and editing.
- Print/export modes and their visibility rules.
- Broader color, font, clipping and transparency qualification.

A displayed redaction appearance is only a marker. It does not remove underlying
content. A displayed signature appearance does not validate a digital signature.
