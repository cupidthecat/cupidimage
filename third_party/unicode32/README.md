# Unicode 3.2 password-preparation data

These files pin the version required by RFC 3454 and RFC 4013:

- UnicodeData-3.2.0.txt defines character properties and decompositions.
- CompositionExclusions-3.2.0.txt lists excluded canonical compositions.
- NormalizationTest-3.2.0.txt supplies published normalization expectations.
- stringprep-tables.json contains the numerical ranges from RFC 3454 tables
  A.1, B.1, C.1.2, C.2.1 through C.9, D.1 and D.2.

The manifest records source URLs and SHA-256 hashes. Unicode source files are
byte-exact copies from https://www.unicode.org/Public/3.2-Update/. The RFC table
ranges were extracted between their named start/end markers; the source RFC hash
is recorded as well. LICENSE retains Unicode License V3, and RFC3454-NOTICE.txt
retains the Internet Society's notice and permissions for the extracted tables.

The generator and tests use these inputs rather than the host Python Unicode
implementation. This avoids version-dependent regeneration of the same claimed
Unicode 3.2 data. The migration to pinned inputs produced byte-identical compiled
runtime tables. Normal tests also check the official normalization vectors, not
just a second implementation of the generator.

Run python3 scripts/generate_pdf_password_tables.py --check from the repository
root. No download is needed during tests or runtime.
