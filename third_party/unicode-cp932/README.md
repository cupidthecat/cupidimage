# Published CP932 mappings

CP932.TXT is the unmodified Microsoft mapping table published by Unicode at
https://www.unicode.org/Public/MAPPINGS/VENDORS/MICSFT/WINDOWS/CP932.TXT.
It identifies itself as table version 2.01, Unicode version 2.0, dated April 15,
1998. `manifest.json` pins the downloaded bytes and the accompanying Unicode
License V3. The same copyright and permission notice is included in
THIRD_PARTY_NOTICES.md for binary distributions.

`scripts/generate_pdf_cp932.py` compiles the 7,915 explicitly defined mappings.
Undefined entries remain undefined; host locale settings, permissive codec
extensions and best-fit reverse conversions are not consulted. In particular,
the published table leaves byte A0 undefined even though the PDF 90ms-RKSJ CMap
assigns it a CID. This remains a missing-glyph case for the external Type 2 path.
