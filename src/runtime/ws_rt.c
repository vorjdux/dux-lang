/*
 * ws_rt.c — RFC 6455 WebSocket client and server for Dux stdlib
 *
 * Implements:
 *   - Client: ws:// URL parsing, TCP connect, HTTP Upgrade handshake
 *   - Server: TCP bind/accept, HTTP Upgrade handshake
 *   - RFC 6455 frame encoding/decoding (text 0x1, binary 0x2, close 0x8,
 *     ping 0x9, pong 0xA) with fragmentation support
 *   - Client frames are masked (RFC 6455 §5.3)
 *   - Per-connection mutex protecting the send path
 *
 * All string I/O uses DuxStr* (allocated via duxrt_str_new).
 * duxrt_ws_recv returns duxrt_str_new("",0) on clean close or error so
 * the Dux caller can detect close with  msg == "".
 */
#define _GNU_SOURCE
#include "duxrt.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>

/* ── Base64 (for Sec-WebSocket-Accept) ──────────────────────────────────── */
static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const unsigned char* in, size_t len, char* out) {
    size_t i = 0, j = 0;
    while (i < len) {
        unsigned char a = in[i++];
        unsigned char b = i < len ? in[i++] : 0;
        unsigned char c = i < len ? in[i++] : 0;
        out[j++] = b64_table[a >> 2];
        out[j++] = b64_table[((a & 3) << 4) | (b >> 4)];
        out[j++] = b64_table[((b & 0xF) << 2) | (c >> 6)];
        out[j++] = b64_table[c & 0x3F];
    }
    /* padding */
    size_t pad = len % 3;
    if (pad == 1) { out[j-2] = '='; out[j-1] = '='; }
    else if (pad == 2) { out[j-1] = '='; }
    out[j] = '\0';
}

/* ── SHA-1 (for Sec-WebSocket-Accept key derivation, RFC 6455 §4.2.2) ──── */
typedef struct { uint32_t h[5]; uint64_t len; uint8_t buf[64]; size_t buflen; } Sha1;

static void sha1_init(Sha1* s) {
    s->h[0] = 0x67452301; s->h[1] = 0xEFCDAB89;
    s->h[2] = 0x98BADCFE; s->h[3] = 0x10325476; s->h[4] = 0xC3D2E1F0;
    s->len = 0; s->buflen = 0;
}

#define ROTL32(v,n) (((v)<<(n))|((v)>>(32-(n))))

static void sha1_compress(Sha1* s, const uint8_t* block) {
    uint32_t w[80];
    int i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i*4]   << 24) | ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] <<  8) |  (uint32_t)block[i*4+3];
    for (i = 16; i < 80; i++)
        w[i] = ROTL32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4];
    for (i = 0; i < 80; i++) {
        uint32_t f, k;
        if      (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;             k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;             k = 0xCA62C1D6; }
        uint32_t t = ROTL32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ROTL32(b, 30); b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}

static void sha1_update(Sha1* s, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        s->buf[s->buflen++] = data[i];
        s->len++;
        if (s->buflen == 64) { sha1_compress(s, s->buf); s->buflen = 0; }
    }
}

static void sha1_final(Sha1* s, uint8_t out[20]) {
    uint64_t bitlen = s->len * 8;
    uint8_t pad = 0x80;
    sha1_update(s, &pad, 1);
    while (s->buflen != 56) { uint8_t z = 0; sha1_update(s, &z, 1); }
    for (int i = 7; i >= 0; i--) {
        uint8_t byte_val = (uint8_t)((bitlen >> (i * 8)) & 0xFF);
        sha1_update(s, &byte_val, 1);
    }
    for (int i = 0; i < 5; i++) {
        out[i*4]   = (uint8_t)((s->h[i] >> 24) & 0xFF);
        out[i*4+1] = (uint8_t)((s->h[i] >> 16) & 0xFF);
        out[i*4+2] = (uint8_t)((s->h[i] >>  8) & 0xFF);
        out[i*4+3] = (uint8_t)( s->h[i]        & 0xFF);
    }
}

/* Compute the Sec-WebSocket-Accept response value (base64(SHA1(key + GUID))). */
static void ws_accept_key(const char* client_key, char out_b64[29]) {
    static const char* magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    Sha1 s; sha1_init(&s);
    sha1_update(&s, (const uint8_t*)client_key, strlen(client_key));
    sha1_update(&s, (const uint8_t*)magic, strlen(magic));
    uint8_t digest[20]; sha1_final(&s, digest);
    base64_encode(digest, 20, out_b64);
}

/* ── Thread-local error storage ─────────────────────────────────────────── */
static __thread char g_ws_err[256];

/* ── Connection / server structs ────────────────────────────────────────── */
typedef struct {
    int             fd;
    pthread_mutex_t send_mu;
    int             closed;
} DuxWsConn;

typedef struct {
    int listen_fd;
} DuxWsServer;

/* ── Frame encoding ─────────────────────────────────────────────────────── */

/* Send a single complete (FIN) WebSocket frame.
   mask=1 for client→server (RFC 6455 §5.3 requires client masking). */
static int ws_send_frame(int fd, uint8_t opcode,
                         const char* data, size_t len, int mask) {
    uint8_t header[14];
    size_t  hdr_len = 0;

    header[hdr_len++] = (uint8_t)(0x80 | (opcode & 0x0F)); /* FIN + opcode */

    uint8_t mask_bit = mask ? 0x80 : 0x00;
    if (len <= 125) {
        header[hdr_len++] = (uint8_t)(mask_bit | (uint8_t)len);
    } else if (len <= 65535) {
        header[hdr_len++] = (uint8_t)(mask_bit | 126);
        header[hdr_len++] = (uint8_t)((len >> 8) & 0xFF);
        header[hdr_len++] = (uint8_t)( len       & 0xFF);
    } else {
        header[hdr_len++] = (uint8_t)(mask_bit | 127);
        for (int i = 7; i >= 0; i--)
            header[hdr_len++] = (uint8_t)((len >> (i * 8)) & 0xFF);
    }

    uint8_t masking_key[4] = {0, 0, 0, 0};
    if (mask) {
        uint32_t r = (uint32_t)rand();
        masking_key[0] = (uint8_t)((r >> 24) & 0xFF);
        masking_key[1] = (uint8_t)((r >> 16) & 0xFF);
        masking_key[2] = (uint8_t)((r >>  8) & 0xFF);
        masking_key[3] = (uint8_t)( r        & 0xFF);
        header[hdr_len++] = masking_key[0];
        header[hdr_len++] = masking_key[1];
        header[hdr_len++] = masking_key[2];
        header[hdr_len++] = masking_key[3];
    }

    if (send(fd, header, hdr_len, MSG_NOSIGNAL) < 0) return -1;

    if (len > 0) {
        if (mask) {
            char* masked = (char*)malloc(len);
            if (!masked) return -1;
            for (size_t i = 0; i < len; i++)
                masked[i] = data[i] ^ masking_key[i & 3];
            ssize_t sent = send(fd, masked, len, MSG_NOSIGNAL);
            free(masked);
            if (sent < 0) return -1;
        } else {
            if (send(fd, data, len, MSG_NOSIGNAL) < 0) return -1;
        }
    }
    return 0;
}

/* ── Frame decoding ─────────────────────────────────────────────────────── */

/* Receive exactly n bytes into buf, retrying on short reads. */
static int recv_all(int fd, void* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = recv(fd, (char*)buf + got, n - got, 0);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

/*
 * Read one complete message (possibly reassembled from continuation frames).
 * Auto-handles ping→pong.  Returns malloc'd payload on success (caller frees),
 * or NULL on close/error.  *out_len is set to payload length.
 */
static char* ws_recv_frame(int fd, size_t* out_len) {
    char*  accumulated = NULL;
    size_t acc_len     = 0;

    for (;;) {
        uint8_t hdr[2];
        if (recv_all(fd, hdr, 2) < 0) { free(accumulated); return NULL; }

        int     fin        = (hdr[0] >> 7) & 1;
        uint8_t opcode     = hdr[0] & 0x0F;
        int     has_mask   = (hdr[1] >> 7) & 1;
        uint64_t payload_len = hdr[1] & 0x7F;

        if (payload_len == 126) {
            uint8_t ext[2];
            if (recv_all(fd, ext, 2) < 0) { free(accumulated); return NULL; }
            payload_len = ((uint64_t)ext[0] << 8) | ext[1];
        } else if (payload_len == 127) {
            uint8_t ext[8];
            if (recv_all(fd, ext, 8) < 0) { free(accumulated); return NULL; }
            payload_len = 0;
            for (int i = 0; i < 8; i++) payload_len = (payload_len << 8) | ext[i];
        }

        uint8_t masking_key[4] = {0, 0, 0, 0};
        if (has_mask) {
            if (recv_all(fd, masking_key, 4) < 0) { free(accumulated); return NULL; }
        }

        char* payload = (char*)malloc(payload_len + 1);
        if (!payload) { free(accumulated); return NULL; }
        payload[payload_len] = '\0';

        if (payload_len > 0 && recv_all(fd, payload, (size_t)payload_len) < 0) {
            free(payload); free(accumulated); return NULL;
        }

        if (has_mask) {
            for (size_t i = 0; i < (size_t)payload_len; i++)
                payload[i] ^= masking_key[i & 3];
        }

        /* Control frames (RFC 6455 §5.5) */
        if (opcode == 0x8) {    /* CLOSE */
            free(payload); free(accumulated);
            *out_len = 0; return NULL;
        }
        if (opcode == 0x9) {    /* PING → respond with PONG */
            ws_send_frame(fd, 0xA, payload, (size_t)payload_len, 0);
            free(payload); continue;
        }
        if (opcode == 0xA) {    /* PONG — ignore */
            free(payload); continue;
        }

        /* Data frames: text (0x1), binary (0x2), continuation (0x0) */
        if (opcode == 0x0 || accumulated != NULL) {
            /* continuation frame — append to buffer */
            char* tmp = (char*)realloc(accumulated, acc_len + (size_t)payload_len + 1);
            if (!tmp) { free(payload); free(accumulated); return NULL; }
            accumulated = tmp;
            memcpy(accumulated + acc_len, payload, (size_t)payload_len);
            acc_len += (size_t)payload_len;
            accumulated[acc_len] = '\0';
            free(payload);
        } else {
            /* first (possibly only) data frame */
            if (fin) {
                *out_len = (size_t)payload_len;
                return payload;
            }
            /* start of fragmented message */
            accumulated = payload;
            acc_len     = (size_t)payload_len;
        }

        if (fin) {
            *out_len = acc_len;
            return accumulated;
        }
        /* not fin — loop to read next continuation frame */
    }
}

/* ── TCP helpers ─────────────────────────────────────────────────────────── */

static int tcp_connect(const char* host, int port) {
    char portbuf[8];
    snprintf(portbuf, sizeof(portbuf), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = NULL;
    if (getaddrinfo(host, portbuf, &hints, &res) != 0) return -1;

    int fd = socket(res->ai_family, SOCK_STREAM, 0);
    if (fd < 0) { freeaddrinfo(res); return -1; }

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        close(fd); freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res);
    return fd;
}

/* Build and send the HTTP/1.1 Upgrade request. */
static int send_http_upgrade(int fd,
                              const char* host,
                              const char* path,
                              const char* key) {
    char req[1024];
    int n = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        path, host, key);
    if (n < 0 || (size_t)n >= sizeof(req)) return -1;
    return (send(fd, req, (size_t)n, MSG_NOSIGNAL) < 0) ? -1 : 0;
}

/* Read the server's 101 response and verify Sec-WebSocket-Accept. */
static int read_upgrade_response(int fd, const char* expected_accept) {
    char buf[4096];
    int  total = 0;

    while (total < (int)sizeof(buf) - 1) {
        int n = (int)recv(fd, buf + total, (size_t)(sizeof(buf) - 1 - total), 0);
        if (n <= 0) return -1;
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
    }

    if (!strstr(buf, "101")) {
        snprintf(g_ws_err, sizeof(g_ws_err), "server rejected WebSocket upgrade");
        return -1;
    }

    char* accept_hdr = strcasestr(buf, "Sec-WebSocket-Accept:");
    if (!accept_hdr) {
        snprintf(g_ws_err, sizeof(g_ws_err), "missing Sec-WebSocket-Accept header");
        return -1;
    }
    accept_hdr += 21; /* strlen("Sec-WebSocket-Accept:") */
    while (*accept_hdr == ' ') accept_hdr++;
    char* end = strstr(accept_hdr, "\r\n");
    if (end) *end = '\0';

    if (strcmp(accept_hdr, expected_accept) != 0) {
        snprintf(g_ws_err, sizeof(g_ws_err), "bad Sec-WebSocket-Accept value");
        return -1;
    }
    return 0;
}

/* Parse a ws[s]:// URL into host, path, port. */
static void parse_ws_url(const char* url,
                          char* host, size_t host_cap,
                          char* path, size_t path_cap,
                          int* port, int* use_tls) {
    const char* p = url;
    *use_tls = 0; *port = 80;

    if (strncmp(p, "wss://", 6) == 0) { p += 6; *use_tls = 1; *port = 443; }
    else if (strncmp(p, "ws://", 5) == 0) { p += 5; }

    const char* slash = strchr(p, '/');
    const char* colon = strchr(p, ':');
    size_t host_len;

    if (colon && (!slash || colon < slash)) {
        host_len = (size_t)(colon - p);
        *port = atoi(colon + 1);
    } else {
        host_len = slash ? (size_t)(slash - p) : strlen(p);
    }

    if (host_len >= host_cap) host_len = host_cap - 1;
    memcpy(host, p, host_len);
    host[host_len] = '\0';

    if (slash) {
        snprintf(path, path_cap, "%s", slash);
    } else {
        snprintf(path, path_cap, "/");
    }
}

/* ── Public API ─────────────────────────────────────────────────────────── */

/*
 * duxrt_ws_connect — connect to a ws:// WebSocket server.
 * Returns an opaque DuxWsConn* handle, or NULL on error.
 * Call duxrt_ws_last_error() to retrieve a description after NULL is returned.
 */
void* duxrt_ws_connect(DuxStr* url_str, int32_t use_tls_hint) {
    if (!url_str) {
        snprintf(g_ws_err, sizeof(g_ws_err), "null URL");
        return NULL;
    }

    const char* url = duxrt_str_cstr(url_str);
    char host[256] = "", path[1024] = "/";
    int port = 80, tls = 0;
    parse_ws_url(url, host, sizeof(host), path, sizeof(path), &port, &tls);
    if (use_tls_hint) tls = 1;

    int fd = tcp_connect(host, port);
    if (fd < 0) {
        snprintf(g_ws_err, sizeof(g_ws_err), "TCP connect failed: %s", strerror(errno));
        return NULL;
    }

    /* Generate Sec-WebSocket-Key: 16 random bytes, base64-encoded → 24 chars */
    unsigned char raw_key[16];
    for (int i = 0; i < 16; i++) raw_key[i] = (unsigned char)rand();
    char client_key[25]; base64_encode(raw_key, 16, client_key);

    /* Pre-compute the expected accept value for verification */
    char accept_key[29]; ws_accept_key(client_key, accept_key);

    if (send_http_upgrade(fd, host, path, client_key) < 0) {
        close(fd);
        snprintf(g_ws_err, sizeof(g_ws_err), "failed to send HTTP Upgrade request");
        return NULL;
    }

    if (read_upgrade_response(fd, accept_key) < 0) {
        close(fd);
        return NULL; /* g_ws_err already set by read_upgrade_response */
    }

    DuxWsConn* conn = (DuxWsConn*)malloc(sizeof(DuxWsConn));
    if (!conn) {
        close(fd);
        snprintf(g_ws_err, sizeof(g_ws_err), "out of memory");
        return NULL;
    }
    conn->fd     = fd;
    conn->closed = 0;
    pthread_mutex_init(&conn->send_mu, NULL);
    return conn;
}

/* Return the last WebSocket error string (thread-local). */
DuxStr* duxrt_ws_last_error(void) {
    return duxrt_str_new(g_ws_err, (int64_t)strlen(g_ws_err));
}

/* Close a client connection, sending a RFC 6455 close frame first. */
void duxrt_ws_close(void* ws) {
    if (!ws) return;
    DuxWsConn* conn = (DuxWsConn*)ws;
    if (!conn->closed) {
        ws_send_frame(conn->fd, 0x8, NULL, 0, 1); /* client masks close frame */
        close(conn->fd);
        conn->closed = 1;
    }
    pthread_mutex_destroy(&conn->send_mu);
    free(conn);
}

/* Send a UTF-8 text frame.  Returns 0 on success, -1 on error. */
int32_t duxrt_ws_send_text(void* ws, DuxStr* msg) {
    if (!ws || !msg) return -1;
    DuxWsConn* conn = (DuxWsConn*)ws;
    pthread_mutex_lock(&conn->send_mu);
    int r = ws_send_frame(conn->fd, 0x1,
                          duxrt_str_cstr(msg),
                          (size_t)duxrt_str_length(msg), 1);
    pthread_mutex_unlock(&conn->send_mu);
    if (r < 0) snprintf(g_ws_err, sizeof(g_ws_err), "send failed: %s", strerror(errno));
    return (int32_t)r;
}

/* Send a binary frame.  Returns 0 on success, -1 on error. */
int32_t duxrt_ws_send_binary(void* ws, DuxStr* data) {
    if (!ws || !data) return -1;
    DuxWsConn* conn = (DuxWsConn*)ws;
    pthread_mutex_lock(&conn->send_mu);
    int r = ws_send_frame(conn->fd, 0x2,
                          duxrt_str_cstr(data),
                          (size_t)duxrt_str_length(data), 1);
    pthread_mutex_unlock(&conn->send_mu);
    if (r < 0) snprintf(g_ws_err, sizeof(g_ws_err), "send_binary failed: %s", strerror(errno));
    return (int32_t)r;
}

/*
 * Receive one message.
 * Returns a DuxStr* with the payload on success.
 * Returns duxrt_str_new("",0) on clean close or any error — callers
 * distinguish closed connections by checking for the empty string.
 */
DuxStr* duxrt_ws_recv(void* ws) {
    if (!ws) return duxrt_str_new("", 0);
    DuxWsConn* conn = (DuxWsConn*)ws;
    size_t len  = 0;
    char*  payload = ws_recv_frame(conn->fd, &len);
    if (!payload) return duxrt_str_new("", 0);
    DuxStr* result = duxrt_str_new(payload, (int64_t)len);
    free(payload);
    return result;
}

/* ── Server ─────────────────────────────────────────────────────────────── */

/*
 * duxrt_ws_listen — bind and listen for incoming WebSocket connections.
 * host_str may be NULL or empty for INADDR_ANY ("0.0.0.0").
 * Returns an opaque DuxWsServer* handle, or NULL on error.
 */
void* duxrt_ws_listen(DuxStr* host_str, int32_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        snprintf(g_ws_err, sizeof(g_ws_err), "socket: %s", strerror(errno));
        return NULL;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);

    const char* host = (host_str && duxrt_str_length(host_str) > 0)
                       ? duxrt_str_cstr(host_str) : "0.0.0.0";
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        snprintf(g_ws_err, sizeof(g_ws_err), "invalid host address: %s", host);
        close(fd); return NULL;
    }

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        snprintf(g_ws_err, sizeof(g_ws_err), "bind: %s", strerror(errno));
        close(fd); return NULL;
    }

    if (listen(fd, 16) < 0) {
        snprintf(g_ws_err, sizeof(g_ws_err), "listen: %s", strerror(errno));
        close(fd); return NULL;
    }

    DuxWsServer* srv = (DuxWsServer*)malloc(sizeof(DuxWsServer));
    if (!srv) { close(fd); return NULL; }
    srv->listen_fd = fd;
    return srv;
}

/*
 * duxrt_ws_accept — block until a client connects, perform the HTTP Upgrade
 * handshake, and return an opaque DuxWsConn* for the new connection.
 * Returns NULL on error.
 */
void* duxrt_ws_accept(void* server) {
    if (!server) return NULL;
    DuxWsServer* srv = (DuxWsServer*)server;

    struct sockaddr_storage peer_addr;
    socklen_t addrlen = sizeof(peer_addr);
    int fd = accept(srv->listen_fd, (struct sockaddr*)&peer_addr, &addrlen);
    if (fd < 0) {
        snprintf(g_ws_err, sizeof(g_ws_err), "accept: %s", strerror(errno));
        return NULL;
    }

    /* Read the HTTP Upgrade request */
    char buf[4096];
    int  total = 0;
    while (total < (int)sizeof(buf) - 1) {
        int n = (int)recv(fd, buf + total, (size_t)(sizeof(buf) - 1 - total), 0);
        if (n <= 0) { close(fd); return NULL; }
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
    }

    /* Extract Sec-WebSocket-Key */
    char* key_hdr = strcasestr(buf, "Sec-WebSocket-Key:");
    if (!key_hdr) {
        snprintf(g_ws_err, sizeof(g_ws_err), "client sent no Sec-WebSocket-Key");
        close(fd); return NULL;
    }
    key_hdr += 18; /* strlen("Sec-WebSocket-Key:") */
    while (*key_hdr == ' ') key_hdr++;
    char* key_end = strstr(key_hdr, "\r\n");
    char  client_key[64] = "";
    if (key_end) {
        size_t kl = (size_t)(key_end - key_hdr);
        if (kl >= sizeof(client_key)) kl = sizeof(client_key) - 1;
        memcpy(client_key, key_hdr, kl);
        client_key[kl] = '\0';
    }

    char accept_key[29]; ws_accept_key(client_key, accept_key);

    /* Send 101 Switching Protocols */
    char resp[512];
    int  rn = snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n",
        accept_key);
    if (rn < 0 || send(fd, resp, (size_t)rn, MSG_NOSIGNAL) < 0) {
        close(fd); return NULL;
    }

    DuxWsConn* conn = (DuxWsConn*)malloc(sizeof(DuxWsConn));
    if (!conn) { close(fd); return NULL; }
    conn->fd     = fd;
    conn->closed = 0;
    pthread_mutex_init(&conn->send_mu, NULL);
    return conn;
}

/* Close and free a server handle. */
void duxrt_ws_server_close(void* server) {
    if (!server) return;
    DuxWsServer* srv = (DuxWsServer*)server;
    close(srv->listen_fd);
    free(srv);
}
