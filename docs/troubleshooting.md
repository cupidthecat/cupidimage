# Troubleshooting and safe use

## Terminal output

If graphics escape sequences appear as text, try `--ansi`. Terminal capability
detection is heuristic, and multiplexers, remote sessions, or terminal settings
can prevent a detected protocol from working. Sixel and Kitty need support in the
terminal that ultimately receives their escape sequences.

Use `--fit` or explicit width/height limits for oversized previews. If colors look
banded in Sixel, increase the palette size or try another dithering mode. ANSI
uses colored character cells rather than native image pixels, so fine detail is
necessarily limited.

Alpha behavior differs by backend. ANSI blends against white. Kitty sends RGBA.
Sixel can blend against a configured background or leave pixels with alpha below
128 unpainted; native Sixel transparency is not full per-pixel alpha blending.

## A file fails to load

Check its actual format and the error printed on stderr. The file extension alone
is not proof of its contents. The asset inventory tests catch this for committed
fixtures. Truncated data, invalid checksums, excessive dimensions, and unsupported
features can all cause rejection.

Consult [format support](formats.md) before assuming a valid document is supported.
A PDF that opens but looks wrong may require fonts, transparency, patterns,
annotations, or other rendering features that are not implemented. Its successful
exit status is not evidence of visual equivalence.

For a useful bug report, include the smallest redistributable input, the exact
command or API call, error output, expected result, platform/compiler, and commit.
Do not upload confidential documents or assets whose redistribution rights are
unknown. A generated reproducer is preferable when it demonstrates the same bug.

## Untrusted files

Decoders process complex, attacker-controlled data. Bounds checks, resource limits,
and sanitizer tests reduce risk; they do not establish that parsing is bug-free.
Run unfamiliar corpora in an isolated environment with restricted filesystem and
network access. Do not run the CLI with elevated privileges to decode a document.

SVG images can reference local files. The renderer does not provide a complete
filesystem sandbox for those references. Do not expose it as an unrestricted
server-side file-conversion service without an external sandbox and access policy.
The library does not implement an SVG scripting engine or PDF JavaScript runtime.

Corpus subprocess limits are documented in the [testing guide](testing.md).
Reference rasterizers are separate programs and should receive the same isolation
as the library under test. Keep reports and rendered artifacts private when using
private input documents.

## The PDF review prompt appears, but no picture is visible

Retry with `--backend ansi`. A terminal can ignore an unsupported image protocol
without reporting an error to the program. Generic `TERM=xterm` and
`TERM=xterm-256color` alone now fall back to ANSI instead of implying Sixel support;
`xterm-kitty` remains eligible for Kitty output. Explicit `--sixel` is still
available when you know your terminal supports it.

If Python cannot find `scripts/review_assets.py`, check the working directory.
Run it inside the clone containing that script, not an older checkout or its
parent directory.
