# Full-hinting compatibility identifiers

The family-name substrings and cvt/fpgm/prep signature triplets are adapted from
FreeType 2.14.3, `src/truetype/ttobjs.c`, under the FreeType Project License.
Original copyright and license headers remain in both data files. LICENSE.TXT
contains the unchanged selected license. The source URL, source hash and local
file hashes are recorded in manifest.json.

These identifiers select fonts whose outlines require full instruction control.
They contain no font programs or glyph outlines. The surrounding bounded parser
and native instruction engine do not require an external font library at runtime.
