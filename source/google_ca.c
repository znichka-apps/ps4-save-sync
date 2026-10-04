/* Request-owned trusted CA chain. No mbedTLS filesystem loader or seek calls. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mbedtls/ssl.h>
#include "google_ca.h"

int google_ca_backend_supported(void)
{
    const curl_version_info_data *version = curl_version_info(CURLVERSION_FIRST);
    return version && version->ssl_version && !strncmp(version->ssl_version, "mbedTLS/", 8);
}

void google_ca_init(google_ca_context *ca)
{
    memset(ca, 0, sizeof(*ca));
    mbedtls_x509_crt_init(&ca->chain);
}

CURLcode google_ca_load(google_ca_context *ca)
{
    unsigned char *pem = NULL;
    FILE *fp = NULL;
    CURLcode result = CURLE_SSL_CACERT_BADFILE;
    if (ca->loaded || ca->parser_called) {
        ca->problem = "CA context already used"; return result;
    }
    if (!google_ca_backend_supported()) {
        ca->problem = "Expected mbedTLS backend; TLS context refused";
        return CURLE_SSL_CONNECT_ERROR;
    }
    fp = fopen(GOOGLE_CA_PATH, "rb");
    if (!fp) { ca->problem = "CA open failed"; goto done; }
    pem = malloc(GOOGLE_CA_MAX_BYTES + 1);
    if (!pem) { ca->problem = "CA allocation failed"; result = CURLE_OUT_OF_MEMORY; goto done; }
    while (ca->bytes_read < GOOGLE_CA_MAX_BYTES) {
        size_t room = GOOGLE_CA_MAX_BYTES - ca->bytes_read;
        size_t n = fread(pem + ca->bytes_read, 1, room < 4096 ? room : 4096, fp);
        ca->bytes_read += n;
        if (ferror(fp)) { ca->problem = "CA read failed"; goto done; }
        if (feof(fp)) break;
        if (!n) { ca->problem = "CA read made no progress"; goto done; }
    }
    if (ca->bytes_read == GOOGLE_CA_MAX_BYTES) {
        unsigned char extra;
        size_t n = fread(&extra, 1, 1, fp);
        ca->bytes_read += n;
        if (ferror(fp)) { ca->problem = "CA read failed"; goto done; }
        if (n) { ca->problem = "CA exceeds 512 KiB limit"; goto done; }
        if (!feof(fp)) { ca->problem = "CA read made no progress"; goto done; }
    }
    if (fclose(fp) != 0) { fp = NULL; ca->problem = "CA close failed"; goto done; }
    fp = NULL;
    if (!ca->bytes_read) { ca->problem = "CA file is empty"; goto done; }
    if (memchr(pem, 0, ca->bytes_read)) { ca->problem = "CA contains embedded NUL"; goto done; }
    pem[ca->bytes_read] = 0; /* mbedTLS PEM parsing requires length INCLUDING NUL. */
    ca->parser_called = 1;
    ca->parser_result = mbedtls_x509_crt_parse(&ca->chain, pem, ca->bytes_read + 1);
    /* Positive results mean a partially parsed bundle: never accept it. */
    if (ca->parser_result != 0 || !ca->chain.raw.p || !ca->chain.raw.len) {
        ca->problem = "CA parser rejected bundle"; goto done;
    }
    ca->loaded = 1;
    result = CURLE_OK;
done:
    if (fp && fclose(fp) != 0) ca->problem = "CA close failed";
    free(pem); /* Public CA bytes; parser owns its separate certificate copies. */
    return result;
}

CURLcode google_ca_ssl_context(CURL *curl, void *ssl_context, void *data)
{
    google_ca_context *ca = data;
    /* Check the active backend before even interpreting the opaque TLS pointer. */
    if (!google_ca_backend_supported()) {
        if (ca) { ca->problem = "TLS backend changed; context refused"; ca->callback_result = CURLE_SSL_CONNECT_ERROR; }
        return CURLE_SSL_CONNECT_ERROR;
    }
    if (!curl || !ssl_context || !ca || !ca->loaded || ca->parser_result != 0) {
        if (ca) { ca->problem = "CA TLS context setup refused"; ca->callback_result = CURLE_SSL_CACERT_BADFILE; }
        return CURLE_SSL_CACERT_BADFILE;
    }
    ca->callback_result = curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
    if (ca->callback_result != CURLE_OK) { ca->problem = "TLS connection reuse guard failed"; return ca->callback_result; }
    /* Fresh Google handles have no CRL option. Change only trust anchors; leave
       curl's auth mode, verification callbacks, hostname, profile, and RNG intact. */
    mbedtls_ssl_conf_ca_chain((mbedtls_ssl_config*)ssl_context, &ca->chain, NULL);
    ca->attached = 1;
    return CURLE_OK;
}

void google_ca_diagnostic(const google_ca_context *ca, char *out, size_t capacity)
{
    char parser[48];
    if (ca->parser_called) snprintf(parser, sizeof(parser), "%d", ca->parser_result);
    else snprintf(parser, sizeof(parser), "not run");
    snprintf(out, capacity, "CA memory: %s\nBytes read: %zu; parser: %s\nTLS CA callback: %s; curl %d",
        ca->problem ? ca->problem : (ca->loaded ? "parsed" : "not loaded"), ca->bytes_read, parser,
        ca->attached ? "attached" : "not attached", (int)ca->callback_result);
}

void google_ca_free(google_ca_context *ca)
{
    mbedtls_x509_crt_free(&ca->chain);
    ca->loaded = ca->attached = 0;
}
