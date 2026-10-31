# JPEG 2000 decoding components

These components come from OpenJPEG 2.5.4, tag v2.5.4, commit
6c4a29b00211eb0430fa0e5e890f1ce5c80f409f:
https://github.com/uclouvain/openjpeg/tree/v2.5.4/src/lib/openjp2

The source headers and LICENSE retain the original two-clause BSD notices.
Include these notices with source distributions and THIRD_PARTY_NOTICES.md with
binaries. This directory is compiled into the library; the PDF adapter does not
load a system codec or launch another program.

The C source components have .inc extensions. Most share one private translation
unit. The progression iterator is compiled separately because it deliberately
changes a public structure field's signed type. One static allocation helper is
renamed around the high-throughput decoder include to keep the two source-local
helpers distinct. Assertions are disabled in these release components; malformed
input must return through decoder checks rather than aborting the calling process.
All external component symbols have a private cupidimage_jpx_ prefix.

Threading support is disabled. A thread-local, nested allocation scope tracks
codec allocations, charges a memory/work budget and frees any remaining owners
on failure. Concurrent calling threads have separate scopes; no worker threads
are started by the codec. The adapter uses compiler thread-local storage supported
by GCC, Clang and MSVC, or the C11 spelling when available. The allocation and
clock implementations are supplied by the adapter, not by the upstream files.

Four decode entry points have small work-budget checks: packet decode, packet
skip, ordinary code-block decode and high-throughput code-block decode. The
manifest records each exact patch, each original hash and each adapted hash.
The code-block hooks do not change decoded coefficients or entropy decisions.
Two additional checks reject tile-part indices beyond a TLM declaration or its
allocated index storage before accessing that storage. Their exact patches are
also recorded in the manifest.

The codec factories assign typed adapter functions to their generic callback
slots. These adapters convert the codec object pointer inside an ordinary C call
instead of calling a typed function through an incompatible function pointer.
All 48 factory assignments are recorded as reversible patches; the 44 shared
adapters live in `src/formats/pdf/images/jpx/codec/dispatch.h`. This covers both
decoder factories and the retained internal encoder factories without disabling
function-type sanitizer checks.

Run python3 scripts/check_pdf_jpx_vendor.py to check every pinned component and
reverse the recorded patches in memory to verify the original source hashes.
The upstream encoder and file-stream entry points remain internal implementation
code; they are not exposed by CupidImage's public API. The PDF adapter supplies
only bounded memory streams and never follows an image's external file or URL.

The wrapper's accepted precision, component counts, memory and work bounds are
separate from the complete codec's feature set. A successful codestream decode
does not establish complete JPX container or PDF rendering support.
