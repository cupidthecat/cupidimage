# PDF standards and reference scope

The implementation uses ISO 32000-1:2008 for PDF 1.7 and ISO 32000-2:2020
for PDF 2.0, with applicable published corrections. The reviewed PDF 2.0 copy
includes Errata Collection 3. Clause numbers can differ between editions;
tests and implementation notes should identify the edition they use.

The [PDF Association specification archive](https://pdfa.org/resource/pdf-specification-archive/)
indexes the base standards, legacy references and extensions. The
[resolved issues](https://pdf-issues.pdfa.org/) supplement the published text.
An archive entry or a downloaded document is not evidence that its features
are implemented. Track that evidence in the [feature inventory](pdf-feature-matrix.md).

## Reviewed reference set

The reference set includes these extensions and supporting documents:

- ISO/TS 32001:2022: additional hash algorithms for PDF 2.0.
- ISO/TS 32002:2022: digital-signature extensions for PDF 2.0.
- ISO/TS 32003:2023: AES-GCM encryption.
- ISO/TS 32004:2024: integrity protection for encrypted documents.
- ISO/TS 32005:2023: PDF 1.7 and PDF 2.0 structure namespaces.
- PDF Association application notes AN001, AN002 and AN003: black point
  compensation, associated files and object metadata locations, respectively.
- Well-Tagged PDF 1.0.0: tagged-document interoperability guidance.
- PDF Declarations: declaring conformance and other PDF-related properties.

Use the numbered title page to identify an edition. Some sponsor front matter
in this set contains a different year from the standard's title page. The
application notes help interpret their subjects but do not replace ISO 32000.
Signature, encryption, tagging and metadata requirements need their own tests;
rendering a page does not establish those capabilities.

## RFC documents

[RFC 7995](https://www.rfc-editor.org/info/rfc7995/) is informational guidance
for producing PDF editions of RFCs and Internet-Drafts. It is useful when
choosing representative documents and checking embedded fonts, Unicode maps,
page layout and metadata. It is not a general PDF decoder specification.
CupidImage must preserve a document's existing layout rather than impose the
RFC production recommendations on arbitrary input.

## Using the specifications

For each feature, record the applicable edition and clause, implementation,
positive and negative tests, resource limits and unresolved interactions.
Test rendering separately from extraction, accessibility, signatures and
interactive behavior. Keep licensed specification copies outside the repository;
public documentation should link to their publishers instead of redistributing
the supplied files.

This set is not exhaustive. Codec, font, color-management, archival,
accessibility and other specialized requirements still need the authoritative
references applicable to their particular features.
