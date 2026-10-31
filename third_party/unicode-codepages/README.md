# Published Microsoft code-page mappings

These unmodified tables are published by Unicode:

- CP932.TXT: https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WINDOWS/CP932.TXT
- CP936.TXT: https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WINDOWS/CP936.TXT
- CP949.TXT: https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WINDOWS/CP949.TXT
- CP950.TXT: https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WINDOWS/CP950.TXT

They identify themselves as table version 2.01 and Unicode version 2.0. CP932 is
dated April 15, 1998; the other three are dated January 7, 2000. manifest.json
pins the original bytes and the accompanying Unicode License V3. Include the
copyright and permission notice in THIRD_PARTY_NOTICES.md with binaries.

scripts/generate_pdf_codepages.py compiles 7,915 CP932, 21,920 CP936, 17,176
CP949 and 13,631 CP950 mappings. Undefined entries remain undefined; host locale
settings, permissive codec extensions and best-fit reverse conversions are not
consulted. The former generate_pdf_cp932.py entry point still generates or checks
CP932 alone.

The published CP932 table leaves byte A0 undefined even though a PDF CMap assigns
it a CID. External Type 2 glyph selection therefore leaves it missing rather than
inventing a Unicode character. CIDs continue to select PDF metrics independently.
The renderer binds these tables only to the corresponding Windows encodings
identified by ISO 32000-1 Table 118. Mac OS, HKSCS, EUC-TW and GB18030 variants
are not inferred from these tables.
