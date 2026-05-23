/*
 * net_tls.c — OpenSSL 3.x TLS wrappers for Dux stdlib net.tls
 *
 * Wraps SSL_CTX / SSL objects from OpenSSL to provide:
 *   - Client TLS context  (duxrt_tls_ctx_new_client)
 *   - Server TLS context  (duxrt_tls_ctx_new_server)
 *   - Connect / accept handshake
 *   - send / recv
 *   - Peer certificate subject
 *   - Last error string (thread-local)
 */
#include "duxrt.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

#define DUXRT_TLS_ERR_LEN 512

static __thread char g_tls_err[DUXRT_TLS_ERR_LEN];

static void tls_save_error(void) {
    unsigned long e = ERR_get_error();
    if (e) {
        ERR_error_string_n(e, g_tls_err, DUXRT_TLS_ERR_LEN);
    } else {
        strerror_r(errno, g_tls_err, DUXRT_TLS_ERR_LEN);
    }
}

static void tls_init_once(void) {
    static int done = 0;
    if (!done) {
        done = 1;
        SSL_library_init();
        SSL_load_error_strings();
        OpenSSL_add_all_algorithms();
    }
}

/* ── Context management ──────────────────────────────────────────────────── */

void* duxrt_tls_ctx_new_client(void) {
    tls_init_once();
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) { tls_save_error(); return NULL; }
    /* Verify peer certificate by default */
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    SSL_CTX_set_default_verify_paths(ctx);
    return ctx;
}

void* duxrt_tls_ctx_new_server(DuxStr* cert_path, DuxStr* key_path) {
    tls_init_once();
    if (!cert_path || !key_path) return NULL;
    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) { tls_save_error(); return NULL; }

    if (SSL_CTX_use_certificate_file(ctx, duxrt_str_cstr(cert_path),
                                     SSL_FILETYPE_PEM) <= 0) {
        tls_save_error();
        SSL_CTX_free(ctx);
        return NULL;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, duxrt_str_cstr(key_path),
                                    SSL_FILETYPE_PEM) <= 0) {
        tls_save_error();
        SSL_CTX_free(ctx);
        return NULL;
    }
    return ctx;
}

void duxrt_tls_ctx_free(void* ctx) {
    if (ctx) SSL_CTX_free((SSL_CTX*)ctx);
}

/* ── Connect (client) ────────────────────────────────────────────────────── */

/*
 * duxrt_tls_connect — create a TCP connection to host:port, perform TLS
 * handshake with SNI.  Returns the SSL* object (TlsStream handle) on
 * success, NULL on failure (error in g_tls_err).
 */
void* duxrt_tls_connect(void* ctx_opaque, DuxStr* host_str, int32_t port) {
    if (!ctx_opaque || !host_str) return NULL;
    SSL_CTX* ctx = (SSL_CTX*)ctx_opaque;
    const char* host = duxrt_str_cstr(host_str);

    /* Resolve and connect */
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", (int)port);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, portbuf, &hints, &res) != 0 || !res) {
        snprintf(g_tls_err, DUXRT_TLS_ERR_LEN, "getaddrinfo failed for %s", host);
        return NULL;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        freeaddrinfo(res);
        strerror_r(errno, g_tls_err, DUXRT_TLS_ERR_LEN);
        return NULL;
    }
    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        freeaddrinfo(res);
        close(fd);
        strerror_r(errno, g_tls_err, DUXRT_TLS_ERR_LEN);
        return NULL;
    }
    freeaddrinfo(res);

    /* TLS handshake */
    SSL* ssl = SSL_new(ctx);
    if (!ssl) { tls_save_error(); close(fd); return NULL; }

    SSL_set_fd(ssl, fd);
    SSL_set_tlsext_host_name(ssl, host);   /* SNI */

    if (SSL_connect(ssl) <= 0) {
        tls_save_error();
        SSL_free(ssl);   /* SSL_free closes the fd too */
        return NULL;
    }
    return ssl;
}

/* ── Accept (server) ─────────────────────────────────────────────────────── */

/*
 * duxrt_tls_accept — perform server-side TLS handshake on an already-accepted
 * TCP file descriptor.  tcp_fd is the raw fd from accept(2).
 * Returns SSL* on success, NULL on failure.
 */
void* duxrt_tls_accept(void* ctx_opaque, int32_t tcp_fd) {
    if (!ctx_opaque || tcp_fd < 0) return NULL;
    SSL_CTX* ctx = (SSL_CTX*)ctx_opaque;
    SSL* ssl = SSL_new(ctx);
    if (!ssl) { tls_save_error(); return NULL; }
    SSL_set_fd(ssl, tcp_fd);
    if (SSL_accept(ssl) <= 0) {
        tls_save_error();
        SSL_free(ssl);
        return NULL;
    }
    return ssl;
}

/* ── I/O ─────────────────────────────────────────────────────────────────── */

int64_t duxrt_tls_send(void* tls, DuxStr* data) {
    if (!tls || !data) return -1;
    SSL* ssl = (SSL*)tls;
    const char* buf = duxrt_str_cstr(data);
    int n = SSL_write(ssl, buf, (int)data->len);
    if (n <= 0) { tls_save_error(); return -1; }
    return (int64_t)n;
}

DuxStr* duxrt_tls_recv(void* tls, int64_t cap) {
    if (!tls || cap <= 0) return duxrt_str_new("", 0);
    SSL* ssl = (SSL*)tls;
    char* buf = (char*)malloc((size_t)cap + 1);
    if (!buf) return duxrt_str_new("", 0);
    int n = SSL_read(ssl, buf, (int)cap);
    if (n <= 0) {
        tls_save_error();
        free(buf);
        return duxrt_str_new("", 0);
    }
    buf[n] = '\0';
    DuxStr* result = duxrt_str_new(buf, (int64_t)n);
    free(buf);
    return result;
}

/* ── Peer certificate ────────────────────────────────────────────────────── */

DuxStr* duxrt_tls_peer_cert_subject(void* tls) {
    if (!tls) return duxrt_str_new("", 0);
    SSL* ssl = (SSL*)tls;
    X509* cert = SSL_get_peer_certificate(ssl);
    if (!cert) return duxrt_str_new("", 0);
    char buf[256];
    X509_NAME_oneline(X509_get_subject_name(cert), buf, sizeof(buf));
    X509_free(cert);
    return duxrt_str_new(buf, (int64_t)strlen(buf));
}

/* ── Shutdown / Free ─────────────────────────────────────────────────────── */

void duxrt_tls_close(void* tls) {
    if (!tls) return;
    SSL* ssl = (SSL*)tls;
    SSL_shutdown(ssl);
    int fd = SSL_get_fd(ssl);
    SSL_free(ssl);   /* also closes underlying fd */
    (void)fd;
}

/* ── Error string ────────────────────────────────────────────────────────── */

DuxStr* duxrt_tls_last_error(void) {
    return duxrt_str_new(g_tls_err, (int64_t)strlen(g_tls_err));
}
