/* Real mbedTLS parser/config, mocked backend and bounded-file failure injection. */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mbedtls/ssl.h>
#include <mbedtls/version.h>
#include "google_ca.h"

static const char *fixture = "assets/google/cacert.pem";
static int wrong_backend, fail_open, fail_allocation, fail_read, fail_close, no_progress, fail_reuse;
static int opened, closed, read_calls;
static size_t short_read;
static curl_version_info_data *mock_version(CURLversion age)
{
    static curl_version_info_data version;
    assert(age == CURLVERSION_FIRST);
    version.ssl_version = wrong_backend ? "OpenSSL/test" : "mbedTLS/test";
    return &version;
}
static FILE *mock_open(const char *path, const char *mode)
{
    assert(!strcmp(path, GOOGLE_CA_PATH) && !strcmp(mode, "rb"));
    if (fail_open) { errno = EACCES; return NULL; }
    opened++;
    return fopen(fixture, mode);
}
static size_t mock_read(void *data, size_t size, size_t count, FILE *fp)
{
    assert(size == 1 && count <= 4096);
    read_calls++;
    if (no_progress) return 0;
    if (short_read && count > short_read) count = short_read;
    return fread(data, size, count, fp);
}
static int mock_error(FILE *fp) { return fail_read ? 1 : ferror(fp); }
static int mock_close(FILE *fp) { closed++; int ret = fclose(fp); return fail_close ? EOF : ret; }
static void *mock_allocate(size_t n)
{
    assert(n == GOOGLE_CA_MAX_BYTES + 1);
    return fail_allocation ? NULL : malloc(n);
}
#undef curl_easy_setopt
static CURLcode mock_setopt(CURL *curl, CURLoption option, ...)
{
    assert(curl && option == CURLOPT_FORBID_REUSE);
    va_list ap; va_start(ap, option); assert(va_arg(ap, long) == 1); va_end(ap);
    return fail_reuse ? CURLE_UNKNOWN_OPTION : CURLE_OK;
}
#define curl_version_info mock_version
#define curl_easy_setopt mock_setopt
#define fopen mock_open
#define fread mock_read
#define ferror mock_error
#define fclose mock_close
#define malloc mock_allocate
#define fseek google_ca_must_not_seek
#define ftell google_ca_must_not_tell
#include "../source/google_ca.c"
#undef fopen
#undef fread
#undef ferror
#undef fclose
#undef malloc

#if MBEDTLS_VERSION_NUMBER >= 0x03000000
#define CONFIG_FIELD(config, name) ((config).MBEDTLS_PRIVATE(name))
#else
#define CONFIG_FIELD(config, name) ((config).name)
#endif

static void reset(void)
{
    fixture = "assets/google/cacert.pem";
    wrong_backend = fail_open = fail_allocation = fail_read = fail_close = no_progress = fail_reuse = 0;
    opened = closed = read_calls = 0; short_read = 0;
}
static void write_fixture(const char *name, const char *data, size_t size)
{
    FILE *fp = fopen(name, "wb"); assert(fp);
    assert(fwrite(data, 1, size, fp) == size); assert(!fclose(fp));
}
static void expect_failure(const char *problem, CURLcode result, int parsed)
{
    google_ca_context ca; google_ca_init(&ca);
    assert(google_ca_load(&ca) == result && !ca.loaded);
    assert(ca.problem && strstr(ca.problem, problem) && ca.parser_called == parsed);
    char diagnostic[192]; google_ca_diagnostic(&ca, diagnostic, sizeof(diagnostic));
    assert(strstr(diagnostic, "Bytes read:") && strstr(diagnostic, "parser:"));
    assert(opened == closed);
    google_ca_free(&ca);
}
int main(void)
{
    reset(); google_ca_context ca; google_ca_init(&ca);
    short_read = 13;
    assert(google_ca_load(&ca) == CURLE_OK && ca.loaded && ca.parser_result == 0);
    assert(ca.bytes_read > 100000 && read_calls > 100 && opened == closed);
    unsigned certs = 0;
    for (mbedtls_x509_crt *crt = &ca.chain; crt; crt = crt->next) { assert(crt->raw.len); certs++; }
    assert(certs == 121);
    mbedtls_ssl_config config; mbedtls_ssl_config_init(&config);
    mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_config before = config;
    assert(google_ca_ssl_context((CURL*)1, &config, &ca) == CURLE_OK && ca.attached);
    CONFIG_FIELD(before, ca_chain) = &ca.chain;
    CONFIG_FIELD(before, ca_crl) = NULL;
    assert(!memcmp(&before, &config, sizeof(config))); /* No other curl validation settings changed. */
    wrong_backend = 1;
    assert(google_ca_ssl_context((CURL*)1, (void*)1, &ca) == CURLE_SSL_CONNECT_ERROR); /* Never cast wrong backend. */
    wrong_backend = 0;
    assert(google_ca_ssl_context((CURL*)1, NULL, &ca) == CURLE_SSL_CACERT_BADFILE);
    assert(google_ca_ssl_context((CURL*)1, &config, NULL) == CURLE_SSL_CACERT_BADFILE);
    mbedtls_ssl_config_free(&config);
    google_ca_free(&ca); assert(!ca.loaded && !ca.chain.raw.p);

    reset(); wrong_backend = 1; expect_failure("Expected mbedTLS", CURLE_SSL_CONNECT_ERROR, 0); assert(!opened);
    reset(); fail_open = 1; expect_failure("open failed", CURLE_SSL_CACERT_BADFILE, 0);
    reset(); fail_allocation = 1; expect_failure("allocation failed", CURLE_OUT_OF_MEMORY, 0);
    reset(); fail_read = 1; expect_failure("read failed", CURLE_SSL_CACERT_BADFILE, 0);
    reset(); fail_close = 1; expect_failure("close failed", CURLE_SSL_CACERT_BADFILE, 0);
    reset(); no_progress = 1; expect_failure("no progress", CURLE_SSL_CACERT_BADFILE, 0);
    write_fixture("build/host/ca-empty.pem", "", 0);
    reset(); fixture = "build/host/ca-empty.pem"; expect_failure("empty", CURLE_SSL_CACERT_BADFILE, 0);
    write_fixture("build/host/ca-nul.pem", "bad\0PEM", 7);
    reset(); fixture = "build/host/ca-nul.pem"; expect_failure("embedded NUL", CURLE_SSL_CACERT_BADFILE, 0);
    const char *bad = "-----BEGIN CERTIFICATE-----\ninvalid_base64\n-----END CERTIFICATE-----\n";
    write_fixture("build/host/ca-invalid.pem", bad, strlen(bad));
    reset(); fixture = "build/host/ca-invalid.pem"; expect_failure("parser rejected", CURLE_SSL_CACERT_BADFILE, 1);
    unsigned char *large = calloc(1, GOOGLE_CA_MAX_BYTES + 100); assert(large);
    write_fixture("build/host/ca-large.pem", (char*)large, GOOGLE_CA_MAX_BYTES + 100); free(large);
    reset(); fixture = "build/host/ca-large.pem";
    google_ca_init(&ca); assert(google_ca_load(&ca) == CURLE_SSL_CACERT_BADFILE);
    assert(ca.bytes_read == GOOGLE_CA_MAX_BYTES + 1 && !ca.parser_called && strstr(ca.problem, "limit"));
    google_ca_free(&ca);

    FILE *src = fopen("assets/google/cacert.pem", "rb"), *dst = fopen("build/host/ca-partial.pem", "wb"); assert(src && dst);
    unsigned char chunk[4096]; size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), src))) assert(fwrite(chunk, 1, n, dst) == n);
    assert(!ferror(src) && !fclose(src)); assert(fwrite(bad, 1, strlen(bad), dst) == strlen(bad)); assert(!fclose(dst));
    reset(); fixture = "build/host/ca-partial.pem";
    google_ca_init(&ca); assert(google_ca_load(&ca) == CURLE_SSL_CACERT_BADFILE && ca.parser_result > 0 && !ca.loaded);
    google_ca_free(&ca);
    reset(); google_ca_init(&ca); assert(google_ca_load(&ca) == CURLE_OK);
    mbedtls_ssl_config_init(&config); before = config;
    fail_reuse = 1;
    assert(google_ca_ssl_context((CURL*)1, &config, &ca) == CURLE_UNKNOWN_OPTION && !ca.attached);
    assert(!memcmp(&before, &config, sizeof(config)));
    mbedtls_ssl_config_free(&config); google_ca_free(&ca);
    google_ca_init(&ca); mbedtls_ssl_config_init(&config);
    assert(google_ca_ssl_context((CURL*)1, &config, &ca) == CURLE_SSL_CACERT_BADFILE);
    mbedtls_ssl_config_free(&config); google_ca_free(&ca);
    puts("Google CA host tests passed (real parser/config; mocked backend and read failures).");
    return 0;
}
