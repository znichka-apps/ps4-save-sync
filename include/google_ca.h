#ifndef GOOGLE_CA_H
#define GOOGLE_CA_H
#include <stddef.h>
#include <curl/curl.h>
#include <mbedtls/x509_crt.h>

#define GOOGLE_CA_PATH "/mnt/sandbox/PSSY00001_000/app0/assets/google/cacert.pem"
#define GOOGLE_CA_MAX_BYTES (512 * 1024)
typedef struct {
    mbedtls_x509_crt chain;
    size_t bytes_read;
    int parser_called, parser_result, loaded, attached;
    CURLcode callback_result;
    const char *problem;
} google_ca_context;

int google_ca_backend_supported(void);
void google_ca_init(google_ca_context *ca);
CURLcode google_ca_load(google_ca_context *ca);
CURLcode google_ca_ssl_context(CURL *curl, void *ssl_context, void *data);
void google_ca_diagnostic(const google_ca_context *ca, char *out, size_t capacity);
void google_ca_free(google_ca_context *ca);
#endif
