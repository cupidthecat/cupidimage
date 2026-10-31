# PDF password security

The reader supports the Standard password security handler for revisions 2 through
6, including legacy stream encryption, AES-128 and AES-256. It authenticates owner
or user credentials before decrypting protected page data. Files with an empty
user password work through the existing loading APIs without a password prompt.

This is rendering support, not an encryption writer or signature verifier.
[Issue #80](https://github.com/cupidthecat/cupidimage/issues/80) tracks the scoped
implementation; broader encryption extensions remain in the feature inventory.

## Password algorithms

The private authentication module implements revisions 2 through 6. Legacy
revisions derive per-document keys with the prescribed password padding, MD5
rounds and stream-cipher operations. Modern revisions validate owner/user salts,
recover the file key and check the encrypted permissions block against the
plaintext permissions and metadata flag. Failed authentication clears the output
key. It does not report a partially recovered key as success.

Revisions 5 and 6 use Unicode 3.2 SASLprep. Revision 5 follows Adobe Supplement
1.7, ExtensionLevel 3, algorithm 3.2a; revision 6 follows ISO 32000-2. It removes the
specified characters, maps non-ASCII spaces, performs complete NFKC normalization,
checks prohibited/unassigned characters and applies the bidirectional rules before
truncating the UTF-8 result to 127 bytes. Normalization includes canonical ordering,
composition exclusions and algorithmic Hangul decomposition/composition. It does
not use the host's current Unicode database at runtime.

Legacy UTF-8 input converts to the defined PDFDocEncoding characters. A separate
internal byte-input path preserves already encoded legacy passwords, including
bytes that are not valid UTF-8. Password preparation accepts at most 4,096 input
bytes; output limits are 32 legacy bytes or 127 modern bytes. Temporary password,
hash and key workspaces are cleared before release.

## Cryptographic components

Pinned, byte-exact BearSSL components provide MD5, SHA-256/384/512 and portable
constant-time AES-CBC. They are compiled into private, namespaced translation
units; no system crypto library or network service is used. MD5 and the legacy stream
cipher are retained only for older PDF revisions, not for protecting new data.

These tests are not a security audit, constant-time proof or FIPS validation.
The implementation does not claim that every password-authentication operation
is constant-time. It does not expose a network authentication endpoint.

## Tests

`make test-pdf-security-primitives` checks source hashes, generated Unicode tables,
cryptographic vectors, owner/user authentication and password preparation. Fixed
cipher vectors come from RFC 6229 and NIST SP 800-38A. Larger stored fixtures were
generated independently with cryptography and pypdf; routine tests need neither
package. Hash checks use Python's hashlib. All credentials are synthetic test data.

An optional exhaustive Unicode scalar pass is available with:

```sh
python3 scripts/test_pdf_password.py bin/test_pdf_password --exhaustive
```

The page suite compares independently written encrypted files against their
unencrypted controls, including TrueType, CFF, Type 1 and CJK fonts. Structural
cases cover nonzero generations, encrypted object streams, literal/hex palette
strings, signature-value exemptions, inline images and explicit Crypt filters.
A separate probe checks the actual unencrypted document-level metadata bytes.
Incremental revisions and direct encryption dictionaries have boundary tests.

The optional independent raster pass currently differs on three named StdCF
cases: that renderer skips the explicit decryption and sends ciphertext to Flate.
The tests report those differences and retain the literal red-page expectation
required by the first-Crypt-filter rule in ISO 32000 section 7.4.10. They do not
count the differing images as matches.

A focused run on the pinned public corpus compared the unchanged originals with
independently decrypted controls. The first pages of issue6010_1.pdf,
issue6010_2.pdf, bug1782186.pdf, issue21579.pdf and saslprep-r6.pdf matched their
native control rasters exactly. This isolates password/stream decoding, not
independent font or full-document visual fidelity.

issue15893_reduced.pdf remains blocked by damaged cross-reference metadata in
concatenated file bodies. issue3371.pdf reaches the unsupported JBIG2 image path
in both its encrypted and decrypted forms. encrypted-attachment.pdf has a startxref
offset of 2242 while its actual table starts at 2223; it is still rejected before
password processing. None of these documents is counted as passing, and no
original corpus bytes were changed for the reported successful comparisons.

## Objects, filters and bounds

The latest trailer selects the encryption dictionary. Direct dictionaries and
uncompressed indirect dictionaries are accepted; strings in that dictionary stay
unencrypted. Duplicate decoded keys and malformed controls fail rather than choosing
an arbitrary value. The security dictionary is limited to 64 KiB, 32 nested
containers and 64 entries per dictionary.

Object-number/generation keys apply to legacy strings and streams. AES-256 uses
the document key without that legacy derivation. Strings are decoded lexically
before decryption. Stream data is decrypted before its ordinary filters. The
object-stream container is decrypted once; its member strings are already plaintext.
Inline-image bytes likewise belong to the already decoded page content.

Identity and supported named crypt filters can override stream defaults. A Crypt
filter must come first. The document-level Metadata reference, rather than every
stream with a Metadata type name, identifies the EncryptMetadata exemption.
Original document bytes and byte-range coordinates are never rewritten. Typed
signature dictionaries, and untyped byte-range signatures, retain their hexadecimal
Contents values. This does not validate signatures or parse every signature scheme.

Ciphertext and transformed object views retain the 64 MiB bounds. Invalid AES
lengths or padding fail the load; no unauthenticated partial image is returned.
Legacy encryption and AES-CBC do not authenticate all document contents, so
successful decryption does not prove that a file is intact or trustworthy.

## Credentials and remaining work

The password-aware page and count APIs borrow a length-delimited credential for
the duration of a call. UTF-8 is the default. The explicit legacy-byte option is
available only for revisions 2 through 4. The CLI reads exact bytes from a file or
standard input; it does not accept a literal password in an argument or save it.
See [the API guide](api.md) and [CLI reference](cli.md).

Permissions participate in key derivation and modern permissions-block validation.
The renderer has no editing, printing or attachment-export API whose permissions
could be enforced here. Embedded-file defaults and deferred EFOpen authentication have stream-level tests.
The terminal renderer does not browse or export attachments.
Public-key handlers, AES-GCM/MAC extensions and signature verification are not
implemented. Unsupported handlers fail explicitly. A damaged permissions block
can currently share the incorrect-password error; there is no complete structured
security-error API.

## References

- ISO 32000-1 and ISO 32000-2, clause 7.6, with the
  [published clause 7 errata](https://pdf-issues.pdfa.org/32000-2-2020/clause07.html).
- [Adobe specification archive, including ExtensionLevel 3](https://pdfa.org/resource/pdf-specification-archive/).
- [RFC 3454: stringprep](https://www.rfc-editor.org/info/rfc3454/).
- [RFC 4013: SASLprep](https://www.rfc-editor.org/info/rfc4013/).
- [RFC 6229: legacy stream-cipher test vectors](https://www.rfc-editor.org/info/rfc6229/).
- [NIST SP 800-38A: block cipher modes](https://csrc.nist.gov/pubs/sp/800/38/a/final).

The crypt-filter Length units differ between standard and public-key handlers.
For the standard handler, AESV2 uses 16 bytes and AESV3 uses 32 bytes. The errata
correct the conflicting values in the original PDF 2.0 table. Public-key handlers
and signature verification are outside this implementation task.
