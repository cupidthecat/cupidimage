# PDF shading

The renderer supports function-based, axial and radial shadings (types 1, 2 and 3),
free-form and lattice Gouraud triangle meshes (types 4 and 5), and Coons and
tensor-product patch meshes (types 6 and 7). Supported geometry does not imply
complete color-space support or pixel parity.

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

## Curved patches

Type 6 reads twelve boundary control points and four corner colors. Its Coons
surface is converted to an equivalent bicubic control net. Type 7 reads sixteen
control points, including four independent interior controls. Those interior
points affect the mapping even when the patch boundary stays unchanged.

Flag 0 supplies a complete new patch. Flags 1, 2 and 3 reuse the corresponding
edge's four control points and two colors from the preceding patch. Only the low
two flag bits matter. Coordinates precede corner colors, and each patch record
ends on a byte boundary. A missing first patch, an incomplete record or an empty
patch stream returns an error.

The renderer subdivides control nets with de Casteljau's construction. Convex
bounds discard cells containing no output samples; derivative directions identify
regions that need further subdivision around folds. Within a suitable cell,
bounded Newton iteration finds the parameters of the actual bicubic surface at
the pixel center. The renderer then evaluates the bilinear corner color, or
interpolated function input, at those parameters rather than substituting a flat
quad's color.

Each sample retains its patch number and parameter pair. Later patches replace
earlier ones. Within one folded patch, the largest v wins, then the largest u;
parameter ties within floating-point roundoff use u. Degenerate surfaces with no
area paint nothing. Unresolved inverse mappings return an error instead of
silently choosing an arbitrary sample.

Subdivision is limited to 20 levels and each inverse solve to 24 iterations.
Geometry evaluation consumes the shared shading-work budget. Parameter-priority
storage uses another 20 bytes per output pixel in addition to the four-byte mesh
sample surface; both share the transparency-memory bound. These are bounded
numerical calculations, not a guarantee for every extreme or ill-conditioned PDF
control net.

## Colors and interpolation

Triangle colors are sampled at output-pixel centers using barycentric coordinates.
Patch colors use bilinear interpolation in their recovered parameter space.
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

Mesh samples retain up to 32 double-precision source components and a coverage
flag until compositing. This preserves CMYK-to-gray conversion and avoids rounding
interpolated colors before applying opacity. The allocation charges a sample header, including padding,
plus only the components needed by that mesh. RGB meshes do not reserve space for
32 components.

Mesh sample buffers and lattice-row storage share the 256 MiB transparency-memory
limit. Decoded mesh bytes consume the shared 64 MiB content budget. Vertex decoding,
triangle bounding-box tests and function evaluation consume the shading-work
budget; the final composite consumes pixel work. Failed decoding or evaluation
releases temporary buffers before any shading pixels reach the parent surface.

Device, calibrated, selected ICC, Separation and legacy DeviceN source spaces
share the color conversion path, including supported default remapping. NChannel spot mixtures,
remaining ICC models and general color management are unfinished. See [PDF colors](pdf-colors.md).
Mesh AntiAlias requests do not enable a separate filtered sampler.
The renderer's pixel-center interpolation is not pixel-identical to the tested
independent rasterizers.

## Verification

Run:

    make test-pdf-mesh test-pdf-patch
    python3 scripts/test_pdf_mesh.py bin/test_pdf_pages --reference
    python3 scripts/test_pdf_patch.py bin/test_pdf_pages --reference

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

The patch suite has 219 checks and 124 optional independent comparisons. It covers
both patch types, all field widths, shared edges, curved boundaries, tensor
interior controls, folded surfaces, transforms, patterns and malformed records.
Forty cases compare separable cubic inverse mappings with independent analytic
bisection. Nonlinear/folded gradient cases retain exact analytic expectations.
The 18 shared-edge reference comparisons use an explicit clip to isolate
connectivity from boundary rasterization; six unclipped analytic cases still test
the whole shape. Comparison tolerances are not widened for boundary differences.

Four patch assets generated by `scripts/generate_pdf_patch_fixtures.py` cover
quadratic Coons mapping, tensor interior controls, fold priority and a shared edge.
The pinned
[`coons-allflags-withfunction.pdf`](https://github.com/mozilla/pdf.js/blob/67667f1e449378a2675f4095cded012b7ce5f37b/test/pdfs/coons-allflags-withfunction.pdf)
now renders unmodified. It still differs from the primary rasterizer in 7.664% of
color channels, with mean absolute error 0.26949. Its valid single-element Pattern
color-space array is also covered by the generated tests.

The original `bug1703683_page2_reduced.pdf` remains blocked by its unsupported
CMYK page group. Its DeviceN content has not been requalified as a complete page. Diagnostic wrappers
around its two DeviceCMYK tensor objects render, with differing-channel fractions
of 5.29% and 0.212%. Those isolated checks are not passes for the original page.

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
sections 8.6.3, 8.7.4.2 through 8.7.4.5.8 and 11.6.4.2. The interpolation rules
for Indexed spaces and optional functions are distinct from device-color conversion.

Indexed Coons and tensor patch corners expand through their base space's component
ranges before interpolation, as triangle meshes do. Lab lookup bytes therefore
retain signed a/b ranges rather than being treated as normalized device values.
The DeviceN tests compare both patch families against direct Lab colors; the owned
Indexed Lab patch asset also has an analytic neutral-gray reference.
