# PDF shading

The renderer supports function-based, axial and radial shadings (types 1, 2 and 3),
plus free-form and lattice Gouraud triangle meshes (types 4 and 5). Coons and
tensor-product patch meshes (types 6 and 7) remain unsupported.

Shadings can be painted directly with `sh` or selected through a type 2 shading
pattern. Direct `sh` ignores Background. A shading pattern uses Background where
its geometry does not provide a sample. BBox, transforms, clipping, opacity,
blending and soft masks apply through the normal rendering pipeline.

## Triangle streams

Type 4 reads each vertex's edge flag, coordinates and color samples. Flag 0 starts
a new three-vertex triangle; the next two flags are ignored. Flag 1 continues on
the previous triangle's second/third edge, and flag 2 continues on its first/third
edge. Only the low two flag bits are significant. Flags may occupy 2, 4 or 8 bits.
A continuation without a preceding triangle, flag 3 at a triangle boundary or an
incomplete triangle returns an error.

Type 5 reads complete rows with at least two vertices per row. Adjacent rows form
the two triangles per cell specified by the PDF lattice ordering. Only two rows
are retained at once. Incomplete rows return an error; a complete row without a
following row has no triangles to paint.

Both formats accept coordinate widths of 1, 2, 4, 8, 12, 16, 24 and 32 bits, and
component widths of 1, 2, 4, 8, 12 and 16 bits. Each vertex ends on a byte boundary;
unused low bits in its last byte are ignored. Decode maps the unsigned samples to
coordinate and component ranges. The normal bounded stream-filter pipeline runs
before vertex decoding.

## Colors and interpolation

Triangle colors are sampled at output-pixel centers using barycentric coordinates.
DeviceGray, DeviceRGB, DeviceCMYK and Indexed spaces are supported. Indexed
vertices are converted to their base-space colors before interpolation; palette
indices are never interpolated as continuous colors.

An optional Function replaces vertex colors with one parametric sample per vertex.
The parameter is interpolated first, then evaluated through the supported function
tree. The function domain must contain the Decode interval. This distinction matters
for nonlinear functions: interpolating their endpoint colors gives a different
result. Functions cannot be combined with Indexed shading spaces. Optional null
Function, BBox and Background entries are treated as absent.

Color spaces in shading dictionaries are complete objects: a built-in family name,
a supported array or an indirect object defining one. They are not names looked up
in the current ColorSpace resource dictionary. Resource names belong to content
color-selection operators and inline images. The same object rule applies to group
blending spaces. Single-element DeviceGray, DeviceRGB and DeviceCMYK arrays are
accepted, as well as their abbreviated name-only form.

## Compositing and bounds

Triangles first write a temporary color/coverage surface. Later triangles replace
earlier samples where they overlap. The finished shading is painted once, so
shared edges and overlaps do not repeatedly apply opacity or masks. Degenerate
triangles contribute no area. A singular transformation paints nothing.

Mesh sample buffers and lattice-row storage share the 256 MiB transparency-memory
limit. Decoded mesh bytes consume the shared 64 MiB content budget. Vertex decoding,
triangle bounding-box tests and function evaluation consume the shading-work
budget; the final composite consumes pixel work. Failed decoding or evaluation
releases temporary buffers before any shading pixels reach the parent surface.

This is device-color rendering, not complete color management. Calibrated spaces,
ICC profiles, Separation/DeviceN shading, default-space remapping and patch meshes
still need work. Mesh AntiAlias requests do not enable a separate filtered sampler.
The renderer's pixel-center interpolation is not pixel-identical to the tested
independent rasterizers.

## Verification

Run:

    make test-pdf-mesh
    python3 scripts/test_pdf_mesh.py bin/test_pdf_pages --reference

The mesh suite has 157 analytic checks and 110 optional independent comparisons.
It covers every prescribed bit width, flag reuse, padding, lattice rows, indexed
base-color interpolation, nonlinear functions, transforms, clipping, patterns,
compositing and malformed streams. Independent comparisons allow at most one RGB
unit. The overlap-opacity and pattern-opacity cases use the secondary renderer.

Full-contrast gradients retain exact analytic expectations but are not counted as
matching independent renders. The small gradient fixture differs by up to nine
RGB units from the primary rasterizer and eight from the secondary one, reflecting
different mesh sampling/interpolation. High unused flag bits, empty/degenerate
geometry and several precision-sensitive cases also remain analytic-only. The
tolerance was not widened to make those cases pass.

Four assets generated by `scripts/generate_pdf_mesh_fixtures.py` cover free-form,
lattice, nonlinear-function and Indexed meshes. Allocation-failure tests cover
mesh buffers, row storage, functions, patterns, masks and exhausted work/memory
budgets.

A focused check of the pinned
[`mesh_shading_empty.pdf`](https://github.com/mozilla/pdf.js/blob/67667f1e449378a2675f4095cded012b7ce5f37b/test/pdfs/mesh_shading_empty.pdf)
is still rejected by the stream reader: its page content declares Length 64 but
has 103 bytes before endstream. A separate copy with repaired stream lengths
renders, but differs in 24.667% of color channels, with mean absolute error 0.81043.
That repaired copy is diagnostic evidence, not a passing result for the original
corpus document. The full manual corpus review remains deferred.

## Specification

The implementation follows [ISO 32000-1](https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf),
sections 8.6.3, 8.7.4.2 through 8.7.4.5.6 and 11.6.4.2. The interpolation rules
for Indexed spaces and optional functions are distinct from device-color conversion.
