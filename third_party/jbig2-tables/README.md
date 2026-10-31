# JBIG2 coding tables

The numeric arithmetic-state and standard Huffman tables are adapted from
Apache PDFBox JBIG2 at commit 68dff3095d58e6ad7410fae98b5ee46279c5061e.
The original source URLs and SHA-256 hashes are in manifest.json.

arithmetic.inc contains the 47 states from ITU-T T.88 Table E.1, ordered as
probability, next MPS state, next LPS state, and MPS-switch flag. huffman.inc
contains the 184 ranges from Tables B.1 through B.15, ordered as prefix length,
range length, range flag, and lower bound. Range flags are 0 for normal or upper
ranges, 1 for lower ranges, and 2 for out-of-band values. The offsets array
separates the fifteen tables.

LICENSE.txt and NOTICE.txt retain the upstream Apache-2.0 license and notices
without changes. The C table layout is a CupidImage adaptation. No Java code is
needed to build or run CupidImage. Run scripts/check_pdf_jbig2_sources.py to
verify the checked-in table and notice bytes.
