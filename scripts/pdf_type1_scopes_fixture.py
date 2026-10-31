#!/usr/bin/env python3
"""Owned Type 1 programs for dictionary-scope and inherited-alias checks."""

from test_pdf_type1 import fixture, encrypt
from test_pdf_type1_charstrings import command, WIDTH

READER = b"{string currentfile exch readstring pop} executeonly def\n"
RD = b"/RD " + READER
ND = b"/ND {noaccess def} executeonly def\n"
HINT = b"/BlueValues [-20 0 600 620] def\n"
SUBR = command([400], 6) + command([600], 7) + command([-400], 6) + b"\x0b"
GLYPH = WIDTH + command([0, 0], 21) + command([0], 10) + b"\x09\x0e"
GLYPHS = [(".notdef", WIDTH + b"\x0e"), ("A", GLYPH)]
POSITIVE = (
    "nested-leniv",
    "procedure-matrix",
    "defined-matrix",
    "many-definers",
    "late-nested-leniv",
    "literal-keys",
    "clear-literal-matrix",
    "clear-sibling-matrix",
    "nested-subrs",
    "nested-binary-subrs",
    "nested-charstrings",
    "global-reader",
    "nested-reader-shadow",
    "active-clear-reader",
    "global-definer",
    "active-clear-definer",
    "nested-definer-shadow",
    "local-reader-redefinition",
    "userdict-reader",
    "userdict-definer",
    "userdict-begin-reader",
    "userdict-begin-definer",
    "currentdict-reader",
    "currentdict-private",
    "global-reader-under-local",
)
NEGATIVE = (
    "reader-shadow",
    "global-reader-shadow",
    "unexecuted-reader",
    "expired-reader",
    "computed-leniv",
    "unclosed-array",
    "computed-matrix",
    "computed-type",
)


def decrypt(data, key):
    result = bytearray()
    for cipher in data:
        result.append(cipher ^ (key >> 8))
        key = ((key + cipher) * 52845 + 22719) & 65535
    return bytes(result)


def program(variant="control", len_iv=4, outer=True):
    data, clear_length, encrypted_length = fixture(
        glyphs=GLYPHS, subrs=(SUBR,), len_iv=len_iv, extra=HINT, outer_encrypted=outer
    )
    if encrypted_length:
        clear, body = data[:clear_length], decrypt(data[clear_length:], 55665)[4:]
    else:
        start = data.index(b"dup /Private")
        clear, body = data[:start], data[start:]
    wrong = 4 if len_iv < 0 else -1
    nested = f"1 dict begin /lenIV {wrong} def end\n".encode()
    if variant == "procedure-matrix":
        clear = clear.replace(
            b"/FontMatrix [.001 0 0 .001 0 0]", b"/FontMatrix {.001 0 0 .001 0 0}"
        )
    elif variant == "defined-matrix":
        first = clear.index(b"\n") + 1
        clear = clear[:first] + b"/DefineMatrix {def} bind def\n" + clear[first:]
        clear = clear.replace(
            b"/FontMatrix [.001 0 0 .001 0 0] readonly def",
            b"/FontMatrix [.001 0 0 .001 0 0] readonly DefineMatrix",
        )
    elif variant == "many-definers":
        body = body.replace(b"/Private 10 dict", b"/Private 64 dict")
        aliases = b"".join(f"/D{i} {{def}} def\n".encode() for i in range(24))
        body = body.replace(HINT, aliases + HINT.replace(b" def", b" D23"))
    elif variant == "computed-matrix":
        clear = clear.replace(
            b"/FontMatrix [.001 0 0 .001 0 0] readonly def",
            b"/FontMatrix [.001 0 0 .001 0 0] [1 0 0 1 5 7] matrix concatmatrix def",
        )
    elif variant == "computed-type":
        clear = clear.replace(b"/FontType 1 def", b"/FontType 1 1 add def")
    elif variant == "nested-leniv":
        body = body.replace(b"/Subrs", nested + b"/Subrs", 1)
    elif variant == "late-nested-leniv":
        body = body.replace(b"end end readonly", b"end\n" + nested + b"end readonly")
    elif variant == "literal-keys":
        body = body.replace(
            b"/Subrs",
            b"[ /lenIV -1 /Subrs 0 ] pop << /lenIV -1 >> pop {/lenIV -1 def} pop\n/Subrs",
            1,
        )
    elif variant == "clear-literal-matrix":
        clear = clear.replace(b"/Encoding", b"[ /FontMatrix [1 0 0 1 99 99] ] pop\n/Encoding", 1)
    elif variant == "clear-sibling-matrix":
        clear = clear.replace(
            b"currentdict end\n",
            b"currentdict end\n1 dict begin /FontMatrix [1 0 0 1 99 99] def /FontType 2 def end\n",
        )
    elif variant == "nested-subrs":
        body = body.replace(
            b"2 index /CharStrings", b"1 dict begin /Subrs 0 array def end\n2 index /CharStrings"
        )
    elif variant == "nested-binary-subrs":
        body = body.replace(
            b"2 index /CharStrings",
            b"1 dict begin /Subrs 1 array dup 0 1 RD \x0b NP ND end\n2 index /CharStrings",
        )
    elif variant == "nested-charstrings":
        body = body.replace(
            b"end end readonly",
            b"end\n1 dict begin /CharStrings 1 dict dup begin /Unused 1 RD \x0b ND end def end\nend readonly",
        )
    elif variant in (
        "global-reader",
        "nested-reader-shadow",
        "active-clear-reader",
        "expired-reader",
    ):
        declaration = b"/OuterRead " + READER
        if variant in ("active-clear-reader", "expired-reader"):
            clear = clear.replace(b"/FontType", declaration + b"/FontType", 1)
        else:
            first = clear.index(b"\n") + 1
            clear = clear[:first] + declaration + clear[first:]
        body = body.replace(RD, b"").replace(b" RD ", b" OuterRead ")
        if variant == "nested-reader-shadow":
            body = body.replace(b"/Subrs", b"1 dict begin /OuterRead {pop} def end\n/Subrs", 1)
        if variant == "active-clear-reader":
            clear = clear.replace(b"currentdict end\n", b"currentdict\n")
            body = body.replace(b"noaccess put dup /FontName", b"noaccess put end dup /FontName")
    elif variant in ("global-definer", "active-clear-definer", "nested-definer-shadow"):
        declaration = b"/OuterDef {def} bind def\n"
        if variant == "active-clear-definer":
            clear = clear.replace(b"/FontType", declaration + b"/FontType", 1)
            clear = clear.replace(b"currentdict end\n", b"currentdict\n")
            body = body.replace(b"noaccess put dup /FontName", b"noaccess put end dup /FontName")
        else:
            first = clear.index(b"\n") + 1
            clear = clear[:first] + declaration + clear[first:]
        body = body.replace(HINT, HINT.replace(b" def", b" OuterDef"))
        if variant == "nested-definer-shadow":
            body = body.replace(
                b"/BlueValues", b"1 dict begin /OuterDef 17 def end\n/BlueValues", 1
            )
    elif variant in (
        "userdict-reader",
        "userdict-begin-reader",
        "currentdict-reader",
        "global-reader-shadow",
        "readonly-context-reader",
    ):
        declaration = b"/OuterRead " + READER
        body = body.replace(RD, b"").replace(b" RD ", b" OuterRead ")
        if variant == "readonly-context-reader":
            body = declaration + body
        elif variant == "currentdict-reader":
            body = body.replace(
                b"/ND", b"currentdict " + declaration.replace(b" def", b" put") + b"/ND", 1
            )
        elif variant == "userdict-begin-reader":
            body = b"userdict begin " + declaration + b"end\n" + body
        else:
            prefix = b"userdict " + declaration.replace(b" def", b" put")
            if variant == "global-reader-shadow":
                prefix += b"userdict /OuterRead {pop} put\n"
            body = prefix + body
    elif variant in ("userdict-definer", "userdict-begin-definer"):
        declaration = b"/OuterDef {def} bind def\n"
        prefix = (
            b"userdict begin " + declaration + b"end\n"
            if variant == "userdict-begin-definer"
            else b"userdict " + declaration.replace(b" bind def", b" bind put")
        )
        body = prefix + body.replace(HINT, HINT.replace(b" def", b" OuterDef"))
    elif variant == "currentdict-private":
        body = body.replace(
            f"/lenIV {len_iv} def".encode(),
            f"/lenIV {wrong} def currentdict begin /lenIV {len_iv} def end".encode(),
        )
    elif variant == "global-reader-under-local":
        body = body.replace(RD, RD + b"userdict /RD {pop} put\n")
    elif variant == "local-reader-redefinition":
        body = body.replace(RD, RD + b"/RD {pop} def\n" + RD)
    elif variant == "reader-shadow":
        body = body.replace(RD, RD + b"/RD {pop} def\n")
    elif variant == "unexecuted-reader":
        body = body.replace(RD, b"/Unused " + READER.replace(b"executeonly def", b"pop")).replace(
            b" RD ", b" Unused "
        )
    elif variant == "computed-leniv":
        body = body.replace(
            f"/lenIV {len_iv} def".encode(), f"/lenIV {len_iv+1} 1 sub def".encode()
        )
    elif variant == "unclosed-array":
        body = body.replace(b"/Subrs", b"/RD [1 2 >> def\n/Subrs", 1)
    elif variant != "control":
        raise ValueError(variant)
    if outer:
        body = encrypt(b"\0\1\2\3" + body, 55665)
        return clear + body, len(clear), len(body)
    return clear + body, len(clear + body), 0
