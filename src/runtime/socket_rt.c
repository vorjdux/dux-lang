/*
 * socket_rt.c — POSIX BSD socket wrappers for Dux stdlib net.socket
 *
 * Wraps socket(2), bind(2), connect(2), listen(2), accept(2), send(2),
 * recv(2), sendto(2), recvfrom(2), setsockopt(2), getsockopt(2),
 * shutdown(2), fcntl(2), getpeername(2), getsockname(2).
 */
#include "duxrt.h"
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define DUXSOCK_ERR_LEN  256
#define DUXSOCK_ADDR_LEN  64

/* Opaque socket handle */
typedef struct DuxSocket {
    int fd;
} DuxSocket;

/* Thread-local error and sender buffers */
static __thread char g_sock_err[DUXSOCK_ERR_LEN];
static __thread char g_sock_sender[DUXSOCK_ADDR_LEN];

static void sock_save_errno(void) {
    strerror_r(errno, g_sock_err, DUXSOCK_ERR_LEN);
}

/* ── Core socket lifecycle ───────────────────────────────────────────────── */

void* duxrt_sock_new(int32_t af, int32_t sock_type, int32_t proto) {
    int fd = socket((int)af, (int)sock_type, (int)proto);
    if (fd < 0) { sock_save_errno(); return NULL; }
    DuxSocket* s = (DuxSocket*)malloc(sizeof(DuxSocket));
    if (!s) { close(fd); return NULL; }
    s->fd = fd;
    return s;
}

void duxrt_sock_close(void* s) {
    if (!s) return;
    DuxSocket* sock = (DuxSocket*)s;
    if (sock->fd >= 0) { close(sock->fd); sock->fd = -1; }
    free(sock);
}

/* ── Bind ────────────────────────────────────────────────────────────────── */

int32_t duxrt_sock_bind_ip(void* s, DuxStr* addr_str, int32_t port) {
    if (!s || !addr_str) return -1;
    DuxSocket* sock = (DuxSocket*)s;
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", (int)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    struct addrinfo* res = NULL;
    const char* addr = duxrt_str_cstr(addr_str);
    int r = getaddrinfo(addr[0] ? addr : NULL, portbuf, &hints, &res);
    if (r != 0) {
        snprintf(g_sock_err, DUXSOCK_ERR_LEN, "%s", gai_strerror(r));
        return -1;
    }
    int rc = bind(sock->fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (rc < 0) { sock_save_errno(); return -1; }
    return 0;
}

int32_t duxrt_sock_bind_unix(void* s, DuxStr* path_str) {
    if (!s || !path_str) return -1;
    DuxSocket* sock = (DuxSocket*)s;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, duxrt_str_cstr(path_str), sizeof(addr.sun_path) - 1);
    int r = bind(sock->fd, (struct sockaddr*)&addr, sizeof(addr));
    if (r < 0) { sock_save_errno(); return -1; }
    return 0;
}

/* ── Connect ─────────────────────────────────────────────────────────────── */

int32_t duxrt_sock_connect_ip(void* s, DuxStr* addr_str, int32_t port) {
    if (!s || !addr_str) return -1;
    DuxSocket* sock = (DuxSocket*)s;
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", (int)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* res = NULL;
    int r = getaddrinfo(duxrt_str_cstr(addr_str), portbuf, &hints, &res);
    if (r != 0) {
        snprintf(g_sock_err, DUXSOCK_ERR_LEN, "%s", gai_strerror(r));
        return -1;
    }
    int rc = connect(sock->fd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (rc < 0) { sock_save_errno(); return -1; }
    return 0;
}

int32_t duxrt_sock_connect_unix(void* s, DuxStr* path_str) {
    if (!s || !path_str) return -1;
    DuxSocket* sock = (DuxSocket*)s;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, duxrt_str_cstr(path_str), sizeof(addr.sun_path) - 1);
    int r = connect(sock->fd, (struct sockaddr*)&addr, sizeof(addr));
    if (r < 0) { sock_save_errno(); return -1; }
    return 0;
}

/* ── Listen / Accept ─────────────────────────────────────────────────────── */

int32_t duxrt_sock_listen(void* s, int32_t backlog) {
    if (!s) return -1;
    DuxSocket* sock = (DuxSocket*)s;
    int r = listen(sock->fd, (int)backlog);
    if (r < 0) { sock_save_errno(); return -1; }
    return 0;
}

void* duxrt_sock_accept(void* s) {
    if (!s) return NULL;
    DuxSocket* sock = (DuxSocket*)s;
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    int fd = accept(sock->fd, (struct sockaddr*)&addr, &addrlen);
    if (fd < 0) { sock_save_errno(); return NULL; }
    DuxSocket* client = (DuxSocket*)malloc(sizeof(DuxSocket));
    if (!client) { close(fd); return NULL; }
    client->fd = fd;
    return client;
}

/* ── Send / Recv ─────────────────────────────────────────────────────────── */

int64_t duxrt_sock_send_str(void* s, DuxStr* data, int32_t flags) {
    if (!s || !data) return -1;
    DuxSocket* sock = (DuxSocket*)s;
    const char* buf = duxrt_str_cstr(data);
    ssize_t n = send(sock->fd, buf, (size_t)data->len, (int)flags);
    if (n < 0) { sock_save_errno(); return -1; }
    return (int64_t)n;
}

DuxStr* duxrt_sock_recv_str(void* s, int64_t max_bytes, int32_t flags) {
    if (!s || max_bytes <= 0) return duxrt_str_new("", 0);
    DuxSocket* sock = (DuxSocket*)s;
    char* buf = (char*)malloc((size_t)max_bytes + 1);
    if (!buf) return duxrt_str_new("", 0);
    ssize_t n = recv(sock->fd, buf, (size_t)max_bytes, (int)flags);
    if (n < 0) { sock_save_errno(); free(buf); return NULL; }
    buf[n] = '\0';
    DuxStr* result = duxrt_str_new(buf, (int64_t)n);
    free(buf);
    return result;
}

/* UDP sendto with getaddrinfo-based address resolution */
int64_t duxrt_sock_sendto_ip(void* s, DuxStr* data, DuxStr* addr_str, int32_t port) {
    if (!s || !data || !addr_str) return -1;
    DuxSocket* sock = (DuxSocket*)s;
    char portbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%d", (int)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    struct addrinfo* res = NULL;
    int r = getaddrinfo(duxrt_str_cstr(addr_str), portbuf, &hints, &res);
    if (r != 0) {
        snprintf(g_sock_err, DUXSOCK_ERR_LEN, "%s", gai_strerror(r));
        return -1;
    }
    const char* buf = duxrt_str_cstr(data);
    ssize_t n = sendto(sock->fd, buf, (size_t)data->len, 0,
                       res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (n < 0) { sock_save_errno(); return -1; }
    return (int64_t)n;
}

/* UDP recvfrom — stores sender address in thread-local g_sock_sender */
DuxStr* duxrt_sock_recvfrom_str(void* s, int64_t max_bytes) {
    if (!s || max_bytes <= 0) return duxrt_str_new("", 0);
    DuxSocket* sock = (DuxSocket*)s;
    char* buf = (char*)malloc((size_t)max_bytes + 1);
    if (!buf) return duxrt_str_new("", 0);

    struct sockaddr_storage src;
    socklen_t srclen = sizeof(src);
    ssize_t n = recvfrom(sock->fd, buf, (size_t)max_bytes, 0,
                          (struct sockaddr*)&src, &srclen);
    if (n < 0) { sock_save_errno(); free(buf); return duxrt_str_new("", 0); }
    buf[n] = '\0';

    /* Encode sender "ip:port" into thread-local */
    if (src.ss_family == AF_INET) {
        struct sockaddr_in* s4 = (struct sockaddr_in*)&src;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &s4->sin_addr, ip, sizeof(ip));
        snprintf(g_sock_sender, DUXSOCK_ADDR_LEN, "%s:%d", ip, ntohs(s4->sin_port));
    } else if (src.ss_family == AF_INET6) {
        struct sockaddr_in6* s6 = (struct sockaddr_in6*)&src;
        char ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &s6->sin6_addr, ip, sizeof(ip));
        snprintf(g_sock_sender, DUXSOCK_ADDR_LEN, "[%s]:%d", ip, ntohs(s6->sin6_port));
    } else {
        g_sock_sender[0] = '\0';
    }

    DuxStr* result = duxrt_str_new(buf, (int64_t)n);
    free(buf);
    return result;
}

/* Return last sender address set by duxrt_sock_recvfrom_str */
DuxStr* duxrt_sock_last_sender(void) {
    return duxrt_str_new(g_sock_sender, (int64_t)strlen(g_sock_sender));
}

/* ── Socket options ──────────────────────────────────────────────────────── */

int32_t duxrt_sock_setsockopt_int(void* s, int32_t level, int32_t optname, int32_t val) {
    if (!s) return -1;
    DuxSocket* sock = (DuxSocket*)s;
    int v = (int)val;
    int r = setsockopt(sock->fd, (int)level, (int)optname, &v, sizeof(v));
    if (r < 0) { sock_save_errno(); return -1; }
    return 0;
}

int32_t duxrt_sock_getsockopt_int(void* s, int32_t level, int32_t optname) {
    if (!s) return -1;
    DuxSocket* sock = (DuxSocket*)s;
    int val = 0;
    socklen_t len = sizeof(val);
    int r = getsockopt(sock->fd, (int)level, (int)optname, &val, &len);
    if (r < 0) { sock_save_errno(); return -1; }
    return (int32_t)val;
}

int32_t duxrt_sock_shutdown(void* s, int32_t how) {
    if (!s) return -1;
    DuxSocket* sock = (DuxSocket*)s;
    int r = shutdown(sock->fd, (int)how);
    if (r < 0) { sock_save_errno(); return -1; }
    return 0;
}

int32_t duxrt_sock_set_nonblocking(void* s, int32_t enable) {
    if (!s) return -1;
    DuxSocket* sock = (DuxSocket*)s;
    int flags = fcntl(sock->fd, F_GETFL, 0);
    if (flags < 0) { sock_save_errno(); return -1; }
    flags = enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    int r = fcntl(sock->fd, F_SETFL, flags);
    if (r < 0) { sock_save_errno(); return -1; }
    return 0;
}

/* direction: 0 = recv timeout (SO_RCVTIMEO), 1 = send timeout (SO_SNDTIMEO) */
int32_t duxrt_sock_set_timeout_ms(void* s, int32_t direction, int64_t ms) {
    if (!s) return -1;
    DuxSocket* sock = (DuxSocket*)s;
    struct timeval tv;
    tv.tv_sec  = (time_t)(ms / 1000);
    tv.tv_usec = (suseconds_t)((ms % 1000) * 1000);
    int optname = (direction == 0) ? SO_RCVTIMEO : SO_SNDTIMEO;
    int r = setsockopt(sock->fd, SOL_SOCKET, optname, &tv, sizeof(tv));
    if (r < 0) { sock_save_errno(); return -1; }
    return 0;
}

/* ── Address helpers ─────────────────────────────────────────────────────── */

static DuxStr* format_sockaddr(struct sockaddr_storage* addr) {
    char result[DUXSOCK_ADDR_LEN];
    if (addr->ss_family == AF_INET) {
        struct sockaddr_in* s4 = (struct sockaddr_in*)addr;
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &s4->sin_addr, ip, sizeof(ip));
        snprintf(result, sizeof(result), "%s:%d", ip, ntohs(s4->sin_port));
    } else if (addr->ss_family == AF_INET6) {
        struct sockaddr_in6* s6 = (struct sockaddr_in6*)addr;
        char ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &s6->sin6_addr, ip, sizeof(ip));
        snprintf(result, sizeof(result), "[%s]:%d", ip, ntohs(s6->sin6_port));
    } else {
        result[0] = '\0';
    }
    return duxrt_str_new(result, (int64_t)strlen(result));
}

DuxStr* duxrt_sock_peer_addr(void* s) {
    if (!s) return duxrt_str_new("", 0);
    DuxSocket* sock = (DuxSocket*)s;
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    if (getpeername(sock->fd, (struct sockaddr*)&addr, &addrlen) < 0)
        return duxrt_str_new("", 0);
    return format_sockaddr(&addr);
}

DuxStr* duxrt_sock_local_addr(void* s) {
    if (!s) return duxrt_str_new("", 0);
    DuxSocket* sock = (DuxSocket*)s;
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(sock->fd, (struct sockaddr*)&addr, &addrlen) < 0)
        return duxrt_str_new("", 0);
    return format_sockaddr(&addr);
}

int32_t duxrt_sock_fd(void* s) {
    if (!s) return -1;
    return (int32_t)((DuxSocket*)s)->fd;
}

DuxStr* duxrt_sock_last_error(void) {
    return duxrt_str_new(g_sock_err, (int64_t)strlen(g_sock_err));
}

/* ── Multicast helpers ───────────────────────────────────────────────────── */

int32_t duxrt_sock_join_multicast(void* s, DuxStr* group_str) {
    if (!s || !group_str) return -1;
    DuxSocket* sock = (DuxSocket*)s;
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(duxrt_str_cstr(group_str));
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    int r = setsockopt(sock->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                       &mreq, sizeof(mreq));
    if (r < 0) { sock_save_errno(); return -1; }
    return 0;
}

int32_t duxrt_sock_leave_multicast(void* s, DuxStr* group_str) {
    if (!s || !group_str) return -1;
    DuxSocket* sock = (DuxSocket*)s;
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(duxrt_str_cstr(group_str));
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    int r = setsockopt(sock->fd, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                       &mreq, sizeof(mreq));
    if (r < 0) { sock_save_errno(); return -1; }
    return 0;
}
