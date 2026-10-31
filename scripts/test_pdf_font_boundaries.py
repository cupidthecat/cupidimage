#!/usr/bin/env python3
"""Check bounded PDF scalar and Type 1 stream-boundary recovery with owned data."""

import itertools
from pathlib import Path
import subprocess
import sys
import tempfile
import zlib
from test_pdf_xref import PDF
from test_pdf_type3 import stream
from test_pdf_type1 import fixture as font_program, encrypt
from test_pdf_type1_charstrings import WIDTH, BOX


def fixture(
    raw, lengths, joined=(), compressed=False, width=b"600", bad_keyword=False, direct=False
):
    payload = zlib.compress(raw) if compressed else raw
    attributes = b" /Filter /FlateDecode" if compressed else b""
    objects = {
        1: b"<< /Type /Catalog /Pages 2 0 R >>",
        2: b"<< /Type /Pages /Count 1 /Kids [3 0 R] >>",
        3: b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 120 90] /Contents 4 0 R "
        b"/Resources << /Font << /F 5 0 R >> >> >>",
        4: stream("BT /F 24 Tf 50 30 Td (AA) Tj ET"),
        5: b"<< /Type /Font /Subtype /Type1 /FontDescriptor 8 0 R /Encoding /WinAnsiEncoding "
        b"/BaseFont /CupidShapes /FirstChar 65 /LastChar 65 /Widths [13 0 R] >>",
        8: b"<< /Type /FontDescriptor /FontName /CupidShapes /Flags 32 /FontBBox [0 -200 1000 1000] "
        b"/Ascent 800 /Descent -200 /ItalicAngle 0 /CapHeight 600 /StemV 80 /FontFile 9 0 R >>",
        9: b"<< /Length 12 0 R /Length1 10 0 R /Length2 11 0 R /Length3 "
        + str(lengths[2]).encode()
        + attributes
        + b" >>\nstream\n"
        + payload
        + b"\nendstream",
        10: str(lengths[0]).encode(),
        11: str(lengths[1]).encode(),
        12: str(len(payload)).encode(),
        13: width,
    }
    if direct:
        objects[9] = objects[9].replace(b"/Length1 10 0 R", b"/Length1 " + str(lengths[0]).encode())
        objects[9] = objects[9].replace(b"/Length2 11 0 R", b"/Length2 " + str(lengths[1]).encode())
    pdf = PDF()
    for number, body in sorted(objects.items()):
        if number in joined:
            at = len(pdf.data)
            pdf.data += (
                f"{number} 0 obj\n".encode()
                + body
                + b"endobj"
                + (b"X" if bad_keyword else b"")
                + b"\n"
            )
            pdf.objects[number] = (1, at, 0)
        else:
            pdf.obj(number, body)
    pdf.table()
    return bytes(pdf.data)


def main():
    pages, allocator, parser, font_allocator = [str(Path(p).resolve()) for p in sys.argv[1:5]]
    checks = allocations = font_allocations = 0
    raw, clear, encrypted = font_program(glyphs=[(".notdef", WIDTH + b"\x0e"), ("A", BOX)])
    with tempfile.TemporaryDirectory(prefix="cupid-font-boundaries-") as temporary:
        path = Path(temporary) / "case.pdf"
        font = Path(temporary) / "font.pfa"
        font.write_bytes(raw)
        control = subprocess.check_output(
            [parser, str(font), str(clear), str(encrypted), "--outline"]
        )

        def render(data, options=(0, 0, 0, 0, 0), expected=0):
            nonlocal checks, allocations
            path.write_bytes(data)
            result = subprocess.run(
                [pages, str(path), "0", *map(str, options)], capture_output=True, timeout=30
            )
            assert result.returncode == expected, (result.returncode, result.stderr)
            if expected:
                assert not result.stdout
                failed = subprocess.run(
                    [allocator, str(path), "1", *map(str, options)],
                    capture_output=True,
                    text=True,
                    timeout=120,
                )
                assert failed.returncode == 0, (failed.stdout, failed.stderr)
                allocations += int(failed.stdout.split("checks")[0].split()[-1])
            else:
                assert len(result.stdout) == 12 + 160 * 120 * 4
            checks += 1
            return result.stdout

        for bits, compressed in itertools.product(
            itertools.product((False, True), repeat=4), (False, True)
        ):
            joined = tuple(10 + i for i, value in enumerate(bits) if value)
            for options in ((0, 0, 0, 0, 0), (1, 2, 1, 1, 1)):
                assert render(
                    fixture(raw, (clear, encrypted, 0), joined, compressed), options
                ) == render(fixture(raw, (clear, encrypted, 0), (), compressed), options)
        for width in (
            b"600",
            b"+600",
            b"600.",
            b"600.000",
            b"+0600.0",
            b"-.5",
            b"-600",
            b".5",
            b"+.5",
            b"0",
            b"-0",
            b"-0.000",
        ):
            assert render(fixture(raw, (clear, encrypted, 0), (13,), width=width)) == render(
                fixture(raw, (clear, encrypted, 0), width=width)
            )
        for width in (b"+", b".", b"1e2", b"600true", b"1.2.3", b"true", b"null", b"9" * 64):
            render(fixture(raw, (clear, encrypted, 0), (13,), width=width), expected=3)
        render(fixture(raw, (clear, encrypted, 0), (13,), bad_keyword=True), expected=3)
        variants = (
            (0, encrypted, 0),
            (clear - 1, encrypted, 0),
            (clear - 5, encrypted, 0),
            (clear + 2, encrypted - 2, 0),
            (10**12, 10**8, 540),
            (clear, len(raw) + 1, 0),
            (clear, encrypted, len(raw) + 1),
            (clear, 0, 0),
            (clear, 4, 0),
            (clear, encrypted // 2, 0),
        )
        for lengths, compressed, direct in itertools.product(
            variants, (False, True), (False, True)
        ):
            for options in ((0, 0, 0, 0, 0), (1, 2, 1, 1, 1)):
                data = fixture(raw, lengths, (12,), compressed, direct=direct)
                assert render(data, options) == render(fixture(raw, (clear, encrypted, 0)), options)
                render(data, options)
                result = subprocess.run(
                    [allocator, str(path), "0", *map(str, options)],
                    capture_output=True,
                    text=True,
                    timeout=120,
                )
                assert result.returncode == 0, (result.stdout, result.stderr)
                allocations += int(result.stdout.split("checks")[0].split()[-1])
        for l1, l2 in (
            (0, encrypted),
            (clear - 5, encrypted),
            (10**12, 10**8),
            (clear, len(raw) + 1),
        ):
            result = subprocess.run(
                [parser, str(font), str(l1), str(l2), "--pdf-boundaries"],
                capture_output=True,
                timeout=30,
            )
            assert result.returncode == 0 and result.stdout == control
            strict = subprocess.run(
                [parser, str(font), str(l1), str(l2)], capture_output=True, timeout=30
            )
            assert strict.returncode == 2 and not strict.stdout
            result = subprocess.run(
                [font_allocator, str(font), str(l1), str(l2), "1", "--pdf-boundaries"],
                capture_output=True,
                text=True,
                timeout=120,
            )
            assert result.returncode == 0, (result.stdout, result.stderr)
            font_allocations += int(result.stdout)
            checks += 2
        # Recovery needs a complete private program, not just an early CharStrings dictionary.
        assert render(fixture(raw[:-1], (10**12, 10**8, 0))) == render(
            fixture(raw, (clear, encrypted, 0))
        )
        for data in (raw[:-10], raw[: clear + 3], raw[:clear], raw.replace(b"eexec", b"eeXec", 1)):
            render(fixture(data, (10**12, 10**8, 0)), expected=3)
        # Literal strings, procedures and shadowed operators cannot certify an
        # encrypted stream's end. A currentfile operand must precede closefile.
        state = 55665
        private = bytearray()
        for byte in raw[clear:]:
            private.append(byte ^ (state >> 8))
            state = ((state + byte) * 52845 + 22719) & 65535
        head = bytes(private).split(b"end end readonly", 1)[0] + b"end "
        for ending in (
            b"/closefile {} def currentfile closefile",
            b"/currentfile {} def currentfile closefile",
            b"(currentfile closefile)",
            b"{currentfile closefile}",
            b"/OtherFile closefile",
            b"currentfile pop closefile",
        ):
            damaged = raw[:clear] + encrypt(head + ending, 55665)
            render(fixture(damaged, (10**12, 10**8, 0)), expected=3)
        for prelude in (
            b"/eexec {pop} def currentfile eexec",
            b"/currentfile {pop} def currentfile eexec",
            b"otherfile eexec",
        ):
            damaged = raw.replace(b"currentfile eexec", prelude, 1)
            render(fixture(damaged, (10**12, 10**8, 0)), expected=3)
        for lengths in (
            (-1, encrypted, 0),
            (clear, -1, 0),
            (clear, encrypted, -1),
            ("600junk", encrypted, 0),
            (clear, "2.5", 0),
        ):
            render(fixture(raw, lengths), expected=3)
    print(
        f"PDF font boundaries: {checks} checks, {allocations} page allocation failures, "
        f"{font_allocations} parser allocation checks"
    )


if __name__ == "__main__":
    main()
