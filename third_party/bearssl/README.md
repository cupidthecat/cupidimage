# PDF cryptographic components

The unmodified components in this directory come from BearSSL commit
7bea48e5e850ab4cafbe68d3765cdaba13a86d6f:
https://www.bearssl.org/git/BearSSL

The manifest records each original path and SHA-256. C files use .inc extensions
so the private adapters control compilation. All compiled external symbols have
a cupidimage_pdf_crypto_ prefix. Source headers and LICENSE.txt retain the MIT
license. Include this notice with source distributions and THIRD_PARTY_NOTICES.md
with binaries.

Only MD5, SHA-256/384/512, portable constant-time AES-CBC and their byte codecs
are compiled. The broader headers are retained unchanged to satisfy the internal
header dependencies; TLS, certificates, networking and asymmetric cryptography
are not linked. No system cryptographic library is required at runtime.

MD5 and the separate legacy stream cipher adapter exist for old PDF security
revisions. They are not recommendations for protecting new data. Passing vector
tests does not constitute a security audit or FIPS validation.
