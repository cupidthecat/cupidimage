#include "security.h"
#include "../pdf_content_syntax.h"
#include "password.h"
#include "syntax.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int security_complete_value(const pdf_object_view *owner, const char *key,
                                   const pdf_object_view *value, size_t at) {
    size_t source;
    pdf_object_id reference;
    if (!cupidimage_pdf_page_value(owner, key, &source) ||
        !cupidimage_pdf_page_reference(owner, source, &reference))
        return 1;
    cupidimage_pdf_skip_ws_and_comments(value->data, value->len, &at);
    return at == value->len;
}

static int security_name(const pdf_object_view *view, const char *key, char name[128],
                         const char *fallback) {
    pdf_object_view value = {0};
    int present =
        cupidimage_pdf_optional_value(view->xref->data, view->xref->size, view, key, &value);
    size_t at = 0;
    int valid = present > 0 && cupidimage_pdf_read_name(&value, &at, name) &&
                security_complete_value(view, key, &value, at);
    if (!present && fallback) {
        strcpy(name, fallback);
        valid = 1;
    }
    cupidimage_pdf_object_view_release(&value);
    return valid;
}

static int security_number(const pdf_object_view *view, const char *key, double fallback,
                           double minimum, double maximum, double *number) {
    pdf_object_view value = {0};
    int present =
        cupidimage_pdf_optional_value(view->xref->data, view->xref->size, view, key, &value);
    size_t at = 0;
    *number = fallback;
    cupidimage_pdf_skip_ws_and_comments(value.data, value.len, &at);
    size_t start = at;
    int valid =
        present == 0 ||
        (present > 0 && cupidimage_pdf_parse_pdf_number(value.data, value.len, &at, number));
    for (size_t i = start; valid && i < at; i++)
        if (value.data[i] == '.')
            valid = 0;
    valid = valid && (!present || security_complete_value(view, key, &value, at)) &&
            isfinite(*number) && floor(*number) == *number && *number >= minimum &&
            *number <= maximum;
    cupidimage_pdf_object_view_release(&value);
    return valid;
}

static int security_entry_string(const pdf_object_view *view, const char *key,
                                 unsigned char *output, size_t expected) {
    size_t at;
    pdf_object_id reference;
    if (!cupidimage_pdf_page_value(view, key, &at) ||
        cupidimage_pdf_page_reference(view, at, &reference))
        return 0;
    char *text = NULL;
    size_t length = 0;
    /* Fixed-size security entries never need an unbounded lexical token. */
    size_t end = view->len;
    if (end - at > 4096)
        end = at + 4096;
    int ok = at < end && (view->data[at] == '('
                              ? cupidimage_pdf_content_literal(view->data, end, &at, &text, &length)
                              : cupidimage_pdf_content_hex(view->data, end, &at, &text, &length));
    if (ok && length == expected)
        memcpy(output, text, expected);
    else
        ok = 0;
    if (text)
        cupidimage_pdf_crypto_erase(text, length);
    free(text);
    return ok;
}

static int security_filter_details(const pdf_security *security, const char *name,
                                   pdf_crypt_method *method, int *deferred) {
    if (deferred)
        *deferred = 0;
    if (!strcmp(name, "Identity")) {
        *method = PDF_CRYPT_IDENTITY;
        return 1;
    }
    const pdf_object_view *view = &security->dictionary;
    pdf_object_view filters = {0}, filter = {0};
    int ok = cupidimage_pdf_optional_dictionary(view->xref->data, view->xref->size, view, "CF",
                                                &filters) > 0 &&
             cupidimage_pdf_optional_dictionary(view->xref->data, view->xref->size, &filters, name,
                                                &filter) > 0;
    ok = ok && cupidimage_pdf_security_dictionary_valid(&filters) &&
         cupidimage_pdf_security_dictionary_valid(&filter);
    char cfm[128], event[128], type[128];
    double length;
    if (ok)
        ok = security_name(&filter, "CFM", cfm, "None") &&
             security_name(&filter, "AuthEvent", event, "DocOpen") &&
             (!strcmp(event, "DocOpen") || !strcmp(event, "EFOpen")) &&
             security_name(&filter, "Type", type, "CryptFilter") && !strcmp(type, "CryptFilter") &&
             security_number(&filter, "Length", security->parameters.key_length, 5, 32, &length) &&
             length == security->parameters.key_length;
    if (ok) {
        if (security->parameters.revision == 4 && !strcmp(cfm, "V2"))
            *method = PDF_CRYPT_RC4;
        else if (security->parameters.revision == 4 && !strcmp(cfm, "AESV2"))
            *method = PDF_CRYPT_AES128;
        else if (security->parameters.revision >= 5 && !strcmp(cfm, "AESV3"))
            *method = PDF_CRYPT_AES256;
        else
            ok = 0;
    }
    if (ok && deferred)
        *deferred = !strcmp(event, "EFOpen");
    cupidimage_pdf_object_view_release(&filter);
    cupidimage_pdf_object_view_release(&filters);
    return ok;
}

int cupidimage_pdf_security_filter(const pdf_security *security, const char *name,
                                   pdf_crypt_method *method) {
    return security_filter_details(security, name, method, NULL);
}

static int security_identifier(const pdf_object_view *trailer, unsigned char **identifier,
                               size_t *length) {
    size_t at;
    *identifier = NULL;
    *length = 0;
    if (!cupidimage_pdf_page_value(trailer, "ID", &at) || at >= trailer->len ||
        trailer->data[at++] != '[')
        return 0;
    cupidimage_pdf_skip_ws_and_comments(trailer->data, trailer->len, &at);
    char *text = NULL;
    size_t end = trailer->len;
    if (end - at > 4096)
        end = at + 4096;
    int ok =
        at < end && (trailer->data[at] == '('
                         ? cupidimage_pdf_content_literal(trailer->data, end, &at, &text, length)
                         : cupidimage_pdf_content_hex(trailer->data, end, &at, &text, length));
    if (ok && *length <= 1024) {
        *identifier = (unsigned char *)text;
        return 1;
    }
    free(text);
    return 0;
}

static int security_parameters(pdf_security *security) {
    pdf_object_view *view = &security->dictionary;
    pdf_security_parameters *p = &security->parameters;
    double version, revision, length, permissions;
    char handler[128];
    if (!security_name(view, "Filter", handler, NULL) || strcmp(handler, "Standard") ||
        !security_number(view, "V", 0, 1, 5, &version) ||
        !security_number(view, "R", 0, 2, 6, &revision) ||
        !security_number(view, "P", NAN, INT32_MIN, UINT32_MAX, &permissions))
        return 0;
    p->revision = (unsigned)revision;
    p->permissions = permissions < 0 ? (uint32_t)(int32_t)permissions : (uint32_t)permissions;
    if (!((revision == 2 && version == 1) || (revision == 3 && (version == 1 || version == 2)) ||
          (revision == 4 && version == 4) || (revision >= 5 && version == 5)))
        return 0;
    double default_length = version == 5 ? 256 : version == 4 ? 128 : 40;
    if (!security_number(view, "Length", default_length, 40, 256, &length) ||
        fmod(length, 8) != 0 || (version != 2 && length != default_length) ||
        (version == 2 && length > 128))
        return 0;
    p->key_length = (unsigned)length / 8;
    p->encrypt_metadata = 1;
    pdf_object_view metadata = {0};
    int present = cupidimage_pdf_optional_value(view->xref->data, view->xref->size, view,
                                                "EncryptMetadata", &metadata);
    size_t at = 0;
    cupidimage_pdf_skip_ws_and_comments(metadata.data, metadata.len, &at);
    int ok = present >= 0;
    if (present > 0) {
        if (metadata.len - at >= 4 && !memcmp(metadata.data + at, "true", 4) &&
            (metadata.len == at + 4 || cupidimage_pdf_is_ws(metadata.data[at + 4]) ||
             cupidimage_pdf_is_delim(metadata.data[at + 4])))
            p->encrypt_metadata = 1, at += 4;
        else if (metadata.len - at >= 5 && !memcmp(metadata.data + at, "false", 5) &&
                 (metadata.len == at + 5 || cupidimage_pdf_is_ws(metadata.data[at + 5]) ||
                  cupidimage_pdf_is_delim(metadata.data[at + 5])))
            p->encrypt_metadata = 0, at += 5;
        else
            ok = 0;
        ok = ok && security_complete_value(view, "EncryptMetadata", &metadata, at);
    }
    cupidimage_pdf_object_view_release(&metadata);
    if (p->revision < 4)
        p->encrypt_metadata = 1;
    if (!ok || !security_entry_string(view, "O", p->owner, revision <= 4 ? 32 : 48) ||
        !security_entry_string(view, "U", p->user, revision <= 4 ? 32 : 48))
        return 0;
    if (revision >= 5 && (!security_entry_string(view, "OE", p->owner_key, 32) ||
                          !security_entry_string(view, "UE", p->user_key, 32) ||
                          !security_entry_string(view, "Perms", p->perms, 16)))
        return 0;
    if (revision <= 3) {
        security->strings = security->streams = security->embedded = PDF_CRYPT_RC4;
        return 1;
    }
    char strings[128], streams[128], embedded[128];
    return security_name(view, "StrF", strings, "Identity") &&
           security_name(view, "StmF", streams, "Identity") &&
           security_name(view, "EFF", embedded, streams) &&
           cupidimage_pdf_security_filter(security, strings, &security->strings) &&
           cupidimage_pdf_security_filter(security, streams, &security->streams) &&
           security_filter_details(security, embedded, &security->embedded,
                                   &security->embedded_deferred);
}

void cupidimage_pdf_security_close(pdf_xref *xref) {
    if (xref->security) {
        cupidimage_pdf_object_view_release(&xref->security->dictionary);
        cupidimage_pdf_crypto_erase(xref->security, sizeof(*xref->security));
        free(xref->security);
        xref->security = NULL;
    }
}

int cupidimage_pdf_security_open(pdf_xref *xref, const cupidimage_pdf_password *password,
                                 char *error, size_t capacity) {
    pdf_object_view trailer = {xref->trailer_data, xref->trailer_length, NULL, xref, 0};
    pdf_object_view encryption = {0};
    int present =
        cupidimage_pdf_optional_value(xref->data, xref->size, &trailer, "Encrypt", &encryption);
    if (!present)
        return 1;
    size_t at, end;
    pdf_object_id identity = 0;
    pdf_security *security = calloc(1, sizeof(*security));
    int ok = security && present > 0 && cupidimage_pdf_page_value(&trailer, "Encrypt", &at);
    int indirect = ok && cupidimage_pdf_page_reference(&trailer, at, &identity);
    if (indirect)
        identity = encryption.identity;
    const pdf_xref_entry *entry = indirect ? cupidimage_pdf_xref_find(xref, identity) : NULL;
    pdf_object_view dictionary = {0};
    ok = ok && (!indirect || (entry && entry->kind == 1)) &&
         cupidimage_pdf_dictionary_view(&encryption, 0, &dictionary, &end) &&
         dictionary.len <= 65536;
    if (ok && indirect) {
        cupidimage_pdf_skip_ws_and_comments(encryption.data, encryption.len, &end);
        ok = end == encryption.len;
    }
    if (ok) {
        security->dictionary = dictionary;
        security->dictionary.owned = encryption.owned;
        memset(&encryption, 0, sizeof(encryption));
        security->encryption_object = identity;
        ok = cupidimage_pdf_security_dictionary_valid(&security->dictionary) &&
             security_parameters(security);
    }
    unsigned char *identifier = NULL, prepared[127];
    size_t id_length = 0, prepared_length = 0;
    if (ok && security->parameters.revision <= 4) {
        ok = security_identifier(&trailer, &identifier, &id_length);
        security->parameters.identifier = (pdf_crypto_slice){identifier, id_length};
    }
    if (ok)
        ok = cupidimage_pdf_security_password(
            security->parameters.revision, password ? password->data : NULL,
            password ? password->length : 0, password ? password->legacy_bytes : 0, prepared,
            &prepared_length);
    if (ok) {
        security->authenticated = cupidimage_pdf_security_authenticate(
            &security->parameters, prepared, prepared_length, security->key, &security->owner);
        if (!security->authenticated &&
            (security->strings != PDF_CRYPT_IDENTITY || security->streams != PDF_CRYPT_IDENTITY ||
             (security->embedded != PDF_CRYPT_IDENTITY && !security->embedded_deferred))) {
            cupidimage_set_err(error, capacity, "pdf: password required or incorrect");
            ok = 0;
        }
    } else
        cupidimage_set_err(error, capacity, "pdf: unsupported or invalid password security");
    cupidimage_pdf_crypto_erase(prepared, sizeof(prepared));
    free(identifier);
    if (security)
        security->parameters.identifier = (pdf_crypto_slice){0};
    cupidimage_pdf_object_view_release(&encryption);
    xref->security = security;
    if (ok && !security->parameters.encrypt_metadata) {
        pdf_object_view catalog = {0}, metadata = {0};
        ok = cupidimage_pdf_lookup_object(xref, xref->data, xref->size, xref->root_obj, &catalog) ==
             1;
        size_t position;
        pdf_object_id reference;
        if (ok && cupidimage_pdf_page_value(&catalog, "Metadata", &position) &&
            cupidimage_pdf_page_reference(&catalog, position, &reference)) {
            int status = cupidimage_pdf_resolve_object(xref, xref->data, xref->size, reference,
                                                       &metadata, &security->metadata_object, NULL);
            ok = status >= 0;
        }
        cupidimage_pdf_object_view_release(&metadata);
        cupidimage_pdf_object_view_release(&catalog);
        if (!ok)
            cupidimage_set_err(error, capacity, "pdf: invalid or exhausted metadata reference");
    }
    if (!ok)
        cupidimage_pdf_security_close(xref);
    return ok;
}

int cupidimage_pdf_security_stream_method(const pdf_object_view *view, const char *explicit_name,
                                          pdf_crypt_method *method) {
    const pdf_security *security = view->xref ? view->xref->security : NULL;
    *method = PDF_CRYPT_IDENTITY;
    if (!view->identity)
        return explicit_name == NULL;
    if (!security && !explicit_name)
        return 1;
    size_t at = 0;
    char type[128] = {0};
    if (view->xref) {
        if (!security_name(view, "Type", type, ""))
            return 0;
    } else if (cupidimage_pdf_page_value(view, "Type", &at))
        cupidimage_pdf_read_name(view, &at, type);
    if (!strcmp(type, "XRef"))
        return explicit_name == NULL;
    if (explicit_name) {
        if (!strcmp(explicit_name, "Identity"))
            return 1;
        return security && cupidimage_pdf_security_filter(security, explicit_name, method);
    }
    if (!security)
        return 1;
    if (view->identity == security->metadata_object && !strcmp(type, "Metadata") &&
        !security->parameters.encrypt_metadata)
        return 1;
    *method = !strcmp(type, "EmbeddedFile") ? security->embedded : security->streams;
    return 1;
}
