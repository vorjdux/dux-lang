/*
 * net_http.c — HTTP/1.1 client and server parsing for Dux stdlib
 *
 * Implements:
 *   - URL parsing (scheme, host, port, path, query)
 *   - HTTP/1.1 request building and sending (via TCP or TLS)
 *   - HTTP/1.1 response parsing (status, headers, body)
 *   - HTTP/1.1 request parsing (for net.server)
 *   - HTTP/1.1 response formatting (for net.server)
 *
 * For TCP/TLS I/O it uses the socket/TLS functions declared in duxrt.h.
 * The DuxHttpResponse and DuxServerRequest structs are opaque handles
 * allocated by the runtime and freed by the caller via duxrt_http_resp_free.
 */
#include "duxrt.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define HTTP_BUF_INIT (64 * 1024)   /* 64 KiB initial response buffer */
#define HTTP_BUF_SIZE HTTP_BUF_INIT /* alias used by request builders (fixed-size) */

/* ── Thread-local error ──────────────────────────────────────────────────── */
static __thread char g_http_err[256];

/* ── URL parsing ─────────────────────────────────────────────────────────── */

typedef struct {
    char scheme[16];   /* "http" or "https" */
    char host[256];
    int  port;
    char path[4096];   /* includes leading '/' */
} DuxUrl;

/*
 * Parse a URL of the form  scheme://host[:port][/path[?query]]
 * Returns 1 on success, 0 on error.
 */
static int url_parse(const char* url, DuxUrl* out) {
    memset(out, 0, sizeof(*out));
    /* scheme */
    const char* p = strstr(url, "://");
    if (!p) { snprintf(g_http_err, sizeof(g_http_err), "missing scheme"); return 0; }
    size_t scheme_len = (size_t)(p - url);
    if (scheme_len >= sizeof(out->scheme)) return 0;
    memcpy(out->scheme, url, scheme_len);
    out->scheme[scheme_len] = '\0';
    p += 3;  /* skip "://" */

    /* host[:port] */
    const char* slash = strchr(p, '/');
    const char* host_end = slash ? slash : (p + strlen(p));
    const char* colon = memchr(p, ':', (size_t)(host_end - p));
    if (colon) {
        size_t hlen = (size_t)(colon - p);
        if (hlen >= sizeof(out->host)) return 0;
        memcpy(out->host, p, hlen);
        out->host[hlen] = '\0';
        out->port = atoi(colon + 1);
    } else {
        size_t hlen = (size_t)(host_end - p);
        if (hlen >= sizeof(out->host)) return 0;
        memcpy(out->host, p, hlen);
        out->host[hlen] = '\0';
        out->port = (strcmp(out->scheme, "https") == 0) ? 443 : 80;
    }

    /* path */
    if (slash) {
        snprintf(out->path, sizeof(out->path), "%s", slash);
    } else {
        out->path[0] = '/';
        out->path[1] = '\0';
    }
    return 1;
}

/* ── Response helpers ────────────────────────────────────────────────────── */

typedef struct {
    int32_t  status;
    DuxStr*  status_text;
    DuxDict* headers;
    DuxStr*  body;
} DuxHttpResp;

static DuxHttpResp* resp_new(int32_t status, const char* st,
                              DuxDict* h, DuxStr* body) {
    DuxHttpResp* r = (DuxHttpResp*)malloc(sizeof(DuxHttpResp));
    if (!r) return NULL;
    r->status      = status;
    r->status_text = duxrt_str_new(st, (int64_t)strlen(st));
    r->headers     = h;
    r->body        = body;
    return r;
}

/* ── Low-level HTTP send/recv over a plain file descriptor ───────────────── */

static DuxStr* http_read_all(int fd) {
    size_t cap = HTTP_BUF_INIT;
    char*  buf = (char*)malloc(cap);
    if (!buf) return duxrt_str_new("", 0);
    size_t total = 0;
    ssize_t n;
    while ((n = recv(fd, buf + total, cap - total - 1, 0)) > 0) {
        total += (size_t)n;
        /* Grow buffer when it fills — no silent truncation */
        if (total >= cap - 1) {
            size_t new_cap = cap * 2;
            char*  new_buf = (char*)realloc(buf, new_cap);
            if (!new_buf) { free(buf); return duxrt_str_new("", 0); }
            buf = new_buf;
            cap = new_cap;
        }
    }
    buf[total] = '\0';
    DuxStr* s = duxrt_str_new(buf, (int64_t)total);
    free(buf);
    return s;
}

/*
 * Parse a raw HTTP/1.1 response string into a DuxHttpResp.
 */
static DuxHttpResp* parse_response(const char* raw) {
    DuxDict* headers = duxrt_dict_new();
    int status = 0;
    char status_text[64] = "";

    /* Status line */
    const char* crlf = strstr(raw, "\r\n");
    if (!crlf) crlf = strchr(raw, '\n');
    if (!crlf) {
        return resp_new(0, "Bad Response", headers,
                        duxrt_str_new(raw, (int64_t)strlen(raw)));
    }
    /* HTTP/1.x NNN reason */
    sscanf(raw, "HTTP/%*s %d %63[^\r\n]", &status, status_text);

    /* Headers */
    const char* p = crlf + ((*crlf == '\r') ? 2 : 1);
    while (*p && !(*p == '\r' && *(p+1) == '\n') && *p != '\n') {
        const char* line_end = strstr(p, "\r\n");
        if (!line_end) line_end = strchr(p, '\n');
        if (!line_end) break;
        const char* colon = memchr(p, ':', (size_t)(line_end - p));
        if (colon) {
            /* name */
            size_t nlen = (size_t)(colon - p);
            char* name = (char*)malloc(nlen + 1);
            if (name) {
                memcpy(name, p, nlen);
                name[nlen] = '\0';
                /* value (skip leading spaces) */
                const char* val = colon + 1;
                while (*val == ' ') val++;
                size_t vlen = (size_t)(line_end - val);
                if (vlen > 0 && val[vlen-1] == '\r') vlen--;
                /* dict keys are const char* — use the name buffer directly */
                DuxStr* vv = duxrt_str_new(val, (int64_t)vlen);
                duxrt_dict_set(headers, name, vv);
                free(name);
            }
        }
        p = line_end + ((*line_end == '\r') ? 2 : 1);
    }

    /* Body — skip blank line */
    const char* body_start = strstr(raw, "\r\n\r\n");
    if (body_start) body_start += 4;
    else {
        body_start = strstr(raw, "\n\n");
        if (body_start) body_start += 2;
    }
    DuxStr* body = body_start
        ? duxrt_str_new(body_start, (int64_t)strlen(body_start))
        : duxrt_str_new("", 0);

    return resp_new(status, status_text, headers, body);
}

/* ── Public C API: HTTP response accessors ───────────────────────────────── */

int32_t duxrt_http_resp_status(void* resp) {
    if (!resp) return 0;
    return ((DuxHttpResp*)resp)->status;
}

DuxStr* duxrt_http_resp_status_text(void* resp) {
    if (!resp) return duxrt_str_new("", 0);
    return ((DuxHttpResp*)resp)->status_text;
}

DuxStr* duxrt_http_resp_header(void* resp, DuxStr* name) {
    if (!resp || !name) return duxrt_str_new("", 0);
    void* v = duxrt_dict_get(((DuxHttpResp*)resp)->headers, duxrt_str_cstr(name));
    return v ? (DuxStr*)v : duxrt_str_new("", 0);
}

DuxStr* duxrt_http_resp_body(void* resp) {
    if (!resp) return duxrt_str_new("", 0);
    return ((DuxHttpResp*)resp)->body;
}

void duxrt_http_resp_free(void* resp) {
    if (!resp) return;
    DuxHttpResp* r = (DuxHttpResp*)resp;
    duxrt_str_release(r->status_text);
    duxrt_str_release(r->body);
    /* headers and their strings are managed by DuxDict */
    free(r);
}

/* ── HTTP request builder ────────────────────────────────────────────────── */

/*
 * duxrt_http_request — build and send an HTTP/1.1 request.
 * method    : "GET", "POST", etc.
 * url       : full URL string
 * headers   : DuxDict* of additional headers (may be NULL)
 * body      : request body (may be NULL / empty)
 * timeout_ms: recv timeout in ms (0 = no timeout)
 * Returns a DuxHttpResp* (opaque), or NULL on error.
 */
void* duxrt_http_request(DuxStr* method, DuxStr* url_str,
                          void* headers_dict, DuxStr* body,
                          int64_t timeout_ms) {
    if (!method || !url_str) {
        snprintf(g_http_err, sizeof(g_http_err), "null method or url");
        return NULL;
    }

    DuxUrl u;
    if (!url_parse(duxrt_str_cstr(url_str), &u)) return NULL;

    /* Build request string */
    const char* meth = duxrt_str_cstr(method);
    char* req = (char*)malloc(HTTP_BUF_SIZE);
    if (!req) return NULL;

    int body_len = body ? (int)body->len : 0;
    int pos = snprintf(req, HTTP_BUF_SIZE,
        "%s %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "Content-Length: %d\r\n",
        meth, u.path, u.host, body_len);

    /* Extra headers from dict — not used from Dux (always null), skip */
    (void)headers_dict;
    pos += snprintf(req + pos, (size_t)(HTTP_BUF_SIZE - pos), "\r\n");

    /* Body */
    if (body && body_len > 0 && pos + body_len < HTTP_BUF_SIZE) {
        memcpy(req + pos, duxrt_str_cstr(body), (size_t)body_len);
        pos += body_len;
    }

    int is_https = (strcmp(u.scheme, "https") == 0);

    if (is_https) {
        /* TLS path */
        void* ctx = duxrt_tls_ctx_new_client();
        if (!ctx) { free(req); return NULL; }
        DuxStr* host_s = duxrt_str_new(u.host, (int64_t)strlen(u.host));
        void* tls = duxrt_tls_connect(ctx, host_s, (int32_t)u.port);
        duxrt_str_release(host_s);
        duxrt_tls_ctx_free(ctx);
        if (!tls) { free(req); return NULL; }

        DuxStr* req_s = duxrt_str_new(req, (int64_t)pos);
        duxrt_tls_send(tls, req_s);
        duxrt_str_release(req_s);
        DuxStr* resp_s = duxrt_tls_recv(tls, HTTP_BUF_SIZE);
        duxrt_tls_close(tls);
        free(req);
        DuxHttpResp* r = parse_response(duxrt_str_cstr(resp_s));
        duxrt_str_release(resp_s);
        return r;
    } else {
        /* Plain TCP path — use POSIX sockets directly */
        char portbuf[16];
        snprintf(portbuf, sizeof(portbuf), "%d", u.port);
        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(u.host, portbuf, &hints, &res) != 0 || !res) {
            snprintf(g_http_err, sizeof(g_http_err), "DNS lookup failed: %.235s", u.host);
            free(req);
            return NULL;
        }
        int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd < 0 || (connect(fd, res->ai_addr, res->ai_addrlen) < 0)) {
            freeaddrinfo(res);
            if (fd >= 0) { close(fd); fd = -1; }
            strerror_r(errno, g_http_err, sizeof(g_http_err));
            free(req);
            return NULL;
        }
        freeaddrinfo(res);

        if (timeout_ms > 0) {
            struct timeval tv;
            tv.tv_sec  = (time_t)(timeout_ms / 1000);
            tv.tv_usec = (suseconds_t)((timeout_ms % 1000) * 1000);
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }

        send(fd, req, (size_t)pos, 0);
        DuxStr* resp_s = http_read_all(fd);
        close(fd);
        free(req);
        DuxHttpResp* r = parse_response(duxrt_str_cstr(resp_s));
        duxrt_str_release(resp_s);
        return r;
    }
}

DuxStr* duxrt_http_last_error(void) {
    return duxrt_str_new(g_http_err, (int64_t)strlen(g_http_err));
}

/* ── Server-side: request parsing ────────────────────────────────────────── */

typedef struct {
    DuxStr*  method;
    DuxStr*  path;
    DuxStr*  query;
    DuxDict* headers;
    DuxStr*  body;
} DuxHttpServerReq;

/*
 * duxrt_http_parse_request — parse raw HTTP/1.1 request bytes.
 * Returns an opaque DuxHttpServerReq* or NULL on parse error.
 */
void* duxrt_http_parse_request(DuxStr* raw) {
    if (!raw) return NULL;
    const char* r = duxrt_str_cstr(raw);
    DuxHttpServerReq* req = (DuxHttpServerReq*)malloc(sizeof(DuxHttpServerReq));
    if (!req) return NULL;

    /* Request line */
    char method_buf[16] = "";
    char path_buf[4096] = "/";
    char proto_buf[16] = "";
    const char* first_crlf = strstr(r, "\r\n");
    if (!first_crlf) first_crlf = strchr(r, '\n');

    if (first_crlf) {
        char line[8192];
        size_t llen = (size_t)(first_crlf - r);
        if (llen >= sizeof(line)) llen = sizeof(line) - 1;
        memcpy(line, r, llen);
        line[llen] = '\0';
        sscanf(line, "%15s %4095s %15s", method_buf, path_buf, proto_buf);
    }

    /* Split path from query */
    char* qmark = strchr(path_buf, '?');
    DuxStr* query;
    if (qmark) {
        query = duxrt_str_new(qmark + 1, (int64_t)strlen(qmark + 1));
        *qmark = '\0';
    } else {
        query = duxrt_str_new("", 0);
    }

    req->method  = duxrt_str_new(method_buf, (int64_t)strlen(method_buf));
    req->path    = duxrt_str_new(path_buf,   (int64_t)strlen(path_buf));
    req->query   = query;
    req->headers = duxrt_dict_new();

    /* Headers */
    const char* p = first_crlf
        ? (first_crlf + ((*first_crlf == '\r') ? 2 : 1))
        : r;
    while (*p && !(*p == '\r' && *(p+1) == '\n') && *p != '\n') {
        const char* le = strstr(p, "\r\n");
        if (!le) le = strchr(p, '\n');
        if (!le) break;
        const char* col = memchr(p, ':', (size_t)(le - p));
        if (col) {
            size_t nlen = (size_t)(col - p);
            char* name = (char*)malloc(nlen + 1);
            if (name) {
                memcpy(name, p, nlen);
                name[nlen] = '\0';
                const char* val = col + 1;
                while (*val == ' ') val++;
                size_t vlen = (size_t)(le - val);
                if (vlen > 0 && val[vlen-1] == '\r') vlen--;
                DuxStr* vv = duxrt_str_new(val, (int64_t)vlen);
                duxrt_dict_set(req->headers, name, vv);
                free(name);
            }
        }
        p = le + ((*le == '\r') ? 2 : 1);
    }

    /* Body */
    const char* bs = strstr(r, "\r\n\r\n");
    if (bs) bs += 4;
    else {
        bs = strstr(r, "\n\n");
        if (bs) bs += 2;
    }
    req->body = bs ? duxrt_str_new(bs, (int64_t)strlen(bs))
                   : duxrt_str_new("", 0);

    return req;
}

DuxStr* duxrt_http_req_method(void* req) {
    return req ? ((DuxHttpServerReq*)req)->method : duxrt_str_new("", 0);
}

DuxStr* duxrt_http_req_path(void* req) {
    return req ? ((DuxHttpServerReq*)req)->path : duxrt_str_new("", 0);
}

DuxStr* duxrt_http_req_query(void* req) {
    return req ? ((DuxHttpServerReq*)req)->query : duxrt_str_new("", 0);
}

DuxStr* duxrt_http_req_header(void* req, DuxStr* name) {
    if (!req || !name) return duxrt_str_new("", 0);
    void* v = duxrt_dict_get(((DuxHttpServerReq*)req)->headers, duxrt_str_cstr(name));
    return v ? (DuxStr*)v : duxrt_str_new("", 0);
}

DuxStr* duxrt_http_req_body(void* req) {
    return req ? ((DuxHttpServerReq*)req)->body : duxrt_str_new("", 0);
}

void duxrt_http_req_free(void* req) {
    if (!req) return;
    DuxHttpServerReq* r = (DuxHttpServerReq*)req;
    duxrt_str_release(r->method);
    duxrt_str_release(r->path);
    duxrt_str_release(r->query);
    duxrt_str_release(r->body);
    free(r);
}

/* ── Server-side: response formatting ───────────────────────────────────── */

/*
 * duxrt_http_format_response — build a raw HTTP/1.1 response string from
 * status code, status text, headers dict, and body.
 */
DuxStr* duxrt_http_format_response(int32_t status, DuxStr* status_text,
                                    void* headers_dict, DuxStr* body) {
    char* buf = (char*)malloc(HTTP_BUF_SIZE);
    if (!buf) return duxrt_str_new("", 0);

    const char* st = status_text ? duxrt_str_cstr(status_text) : "OK";
    int body_len = body ? (int)body->len : 0;

    int pos = snprintf(buf, HTTP_BUF_SIZE,
        "HTTP/1.1 %d %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n",
        (int)status, st, body_len);

    /* headers_dict iteration not used from Dux side (always null) */
    (void)headers_dict;
    pos += snprintf(buf + pos, (size_t)(HTTP_BUF_SIZE - pos), "\r\n");

    if (body && body_len > 0 && pos + body_len < HTTP_BUF_SIZE) {
        memcpy(buf + pos, duxrt_str_cstr(body), (size_t)body_len);
        pos += body_len;
    }

    DuxStr* result = duxrt_str_new(buf, (int64_t)pos);
    free(buf);
    return result;
}
