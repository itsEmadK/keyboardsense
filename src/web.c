/* SPDX-License-Identifier: GPL-3.0-only */
#include "web.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define CLIENTS 8
#define REQUEST_SIZE 2048
#define JSON_SIZE 8192
#define CLIENT_TIMEOUT_MS 3000

typedef struct { char *data; size_t capacity, used; bool failed; } Writer;
static void append(Writer *w, const char *fmt, ...)
{
    if (w->failed) return;
    va_list args; va_start(args, fmt);
    int n = vsnprintf(w->data + w->used, w->capacity - w->used, fmt, args);
    va_end(args);
    if (n < 0 || (size_t)n >= w->capacity - w->used) w->failed = true;
    else w->used += (size_t)n;
}
static void string(Writer *w, const char *s, size_t max)
{
    append(w, "\"");
    for (size_t i = 0; i < max && s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') append(w, "\\%c", c);
        else if (c < 32 || c >= 127) append(w, "\\u%04x", c);
        else append(w, "%c", c);
    }
    append(w, "\"");
}

int ks_web_json(const KsWebSnapshot *s, bool preview, char *data, size_t capacity)
{
    if (!capacity) return -1;
    Writer w = {.data = data, .capacity = capacity};
    append(&w, "{\"source\":\"%s\",\"sequence\":%llu,\"uptime_ms\":%llu,\"enabled\":%s,\"submit_ok\":%s,\"keys\":[",
        preview ? "preview" : "ps5", (unsigned long long)s->sequence,
        (unsigned long long)(s->uptime_us / 1000), s->enabled ? "true" : "false", s->submit_ok ? "true" : "false");
    bool comma = false;
    for (unsigned k = 0; k < KS_KEY_COUNT; ++k) if (s->keys.keys[k]) {
        append(&w, "%s%u", comma ? "," : "", k); comma = true;
    }
    append(&w, "],\"pulse_keys\":[");
    comma = false;
    for (unsigned k = 0; k < KS_KEY_COUNT; ++k) if (s->pulse_keys.keys[k]) {
        append(&w, "%s%u", comma ? "," : "", k); comma = true;
    }
    append(&w, "],\"pulse_buttons\":%u,\"mouse\":{\"buttons\":%u,\"dx\":%d,\"dy\":%d},\"pad\":{\"buttons\":%u,\"lx\":%u,\"ly\":%u,\"rx\":%u,\"ry\":%u,\"l2\":%u,\"r2\":%u},\"devices\":[", s->pulse_buttons,
        s->mouse_buttons, s->mouse_dx, s->mouse_dy, s->pad.buttons,
        s->pad.lx, s->pad.ly, s->pad.rx, s->pad.ry, s->pad.l2, s->pad.r2);
    for (unsigned i = 0; i < s->device_count && i < KS_DEVICE_LIMIT; ++i) {
        const KsWebDevice *d = &s->devices[i];
        append(&w, "%s{\"name\":", i ? "," : ""); string(&w, d->name, sizeof(d->name));
        append(&w, ",\"kind\":\"%s\",\"interface\":%u}", d->protocol == 1 ? "keyboard" : "mouse", d->interface_number);
    }
    const KsConfig *c = &s->config;
    append(&w, "],\"config\":{\"sensitivity\":%.6f,\"ads_multiplier\":%.6f,\"deadzone\":%.6f,\"smoothing_ms\":%.6f,\"walk_speed\":%.6f,\"invert_y\":%s,\"keys\":[",
        c->sensitivity, c->ads_multiplier, c->deadzone, c->smoothing_ms, c->walk_speed, c->invert_y ? "true" : "false");
    for (unsigned k = 0; k < KS_KEY_COUNT; ++k) append(&w, "%s%u", k ? "," : "", (unsigned)c->keys[k]);
    append(&w, "],\"mouse\":[%u,%u,%u]}}", (unsigned)c->mouse[0], (unsigned)c->mouse[1], (unsigned)c->mouse[2]);
    return w.failed ? -1 : (int)w.used;
}

typedef struct {
    int fd;
    char request[REQUEST_SIZE], header[512], json[JSON_SIZE];
    size_t received, header_size, header_sent, body_sent, body_size;
    const unsigned char *body;
    uint64_t deadline;
    bool responding;
} Client;
static struct {
    int listener;
    unsigned port;
    bool preview, started;
    atomic_bool running;
    pthread_t thread;
    pthread_mutex_t mutex;
    KsWebSnapshot state;
    uint64_t key_seen[KS_KEY_COUNT], button_seen[32];
    Client clients[CLIENTS];
} server = {.listener = -1, .mutex = PTHREAD_MUTEX_INITIALIZER};

static uint64_t milliseconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
static void close_client(Client *c)
{
    if (c->fd >= 0) close(c->fd);
    c->fd = -1;
}
static int nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
static void response(Client *c, unsigned status, const char *reason, const char *mime,
                     const unsigned char *body, size_t size)
{
    c->body = body; c->body_size = size; c->body_sent = c->header_sent = 0;
    int n = snprintf(c->header, sizeof(c->header),
        "HTTP/1.1 %u %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'\r\n\r\n",
        status, reason, mime, size);
    if (n < 0 || (size_t)n >= sizeof(c->header)) { close_client(c); return; }
    c->header_size = (size_t)n; c->responding = true;
}
static void error_response(Client *c, unsigned status, const char *reason)
{
    response(c, status, reason, "text/plain; charset=utf-8", (const unsigned char *)reason, strlen(reason));
}
static void route(Client *c)
{
    /* Parse exactly one bounded request line. Query strings do not affect the
     * resource; no path is translated into a filesystem path. */
    char *line_end = strstr(c->request, "\r\n");
    if (!line_end) { error_response(c, 400, "Bad Request"); return; }
    *line_end = 0;
    char method[8], path[256], version[16], extra;
    if (sscanf(c->request, "%7s %255s %15s %c", method, path, version, &extra) != 3 ||
        (strcmp(version, "HTTP/1.1") && strcmp(version, "HTTP/1.0"))) {
        error_response(c, 400, "Bad Request"); return;
    }
    if (strcmp(method, "GET")) { error_response(c, 405, "Method Not Allowed"); return; }
    char *query = strchr(path, '?'); if (query) *query = 0;
    if (!strcmp(path, "/api/state")) {
        KsWebSnapshot state;
        pthread_mutex_lock(&server.mutex); state = server.state; pthread_mutex_unlock(&server.mutex);
        int n = ks_web_json(&state, server.preview, c->json, sizeof(c->json));
        if (n < 0) error_response(c, 500, "Snapshot Too Large");
        else response(c, 200, "OK", "application/json", (const unsigned char *)c->json, (size_t)n);
        return;
    }
    if (!strcmp(path, "/")) strcpy(path, "/index.html");
    for (size_t i = 0; i < ks_web_asset_count; ++i) {
        const KsWebAsset *a = &ks_web_assets[i];
        if (!strcmp(path, a->path)) { response(c, 200, "OK", a->mime, a->data, a->size); return; }
    }
    error_response(c, 404, "Not Found");
}
static void receive(Client *c)
{
    ssize_t n = recv(c->fd, c->request + c->received, sizeof(c->request) - c->received - 1, 0);
    if (n <= 0) {
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) close_client(c);
        return;
    }
    c->received += (size_t)n; c->request[c->received] = 0;
    if (strstr(c->request, "\r\n\r\n")) route(c);
    else if (c->received == sizeof(c->request) - 1) error_response(c, 431, "Request Header Too Large");
}
static void transmit(Client *c)
{
    const unsigned char *data;
    size_t size;
    if (c->header_sent < c->header_size) {
        data = (const unsigned char *)c->header + c->header_sent;
        size = c->header_size - c->header_sent;
    } else { data = c->body + c->body_sent; size = c->body_size - c->body_sent; }
    if (!size) { close_client(c); return; }
    if (size > 16384) size = 16384;
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    ssize_t n = send(c->fd, data, size, flags);
    if (n <= 0) {
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) close_client(c);
        return;
    }
    if (c->header_sent < c->header_size) c->header_sent += (size_t)n;
    else c->body_sent += (size_t)n;
    if (c->header_sent == c->header_size && c->body_sent == c->body_size) close_client(c);
}
static void *serve(void *unused)
{
    (void)unused;
    while (atomic_load(&server.running)) {
        struct pollfd fds[CLIENTS + 1] = {{.fd = server.listener, .events = POLLIN}};
        for (unsigned i = 0; i < CLIENTS; ++i)
            fds[i + 1] = (struct pollfd){.fd = server.clients[i].fd,
                .events = server.clients[i].responding ? POLLOUT : POLLIN};
        if (poll(fds, CLIENTS + 1, 20) < 0) continue;
        if (fds[0].revents & POLLIN) {
            /* Accept at most one per iteration; an incoming connection flood
             * cannot create an unbounded accept loop. */
            int fd = accept(server.listener, NULL, NULL);
            if (fd >= 0) {
                unsigned slot = 0;
                while (slot < CLIENTS && server.clients[slot].fd >= 0) ++slot;
                if (slot == CLIENTS || nonblocking(fd) != 0) close(fd);
                else {
#ifdef SO_NOSIGPIPE
                    int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
                    Client *c = &server.clients[slot]; memset(c, 0, sizeof(*c));
                    c->fd = fd; c->deadline = milliseconds() + CLIENT_TIMEOUT_MS;
                    /* This poll slot may describe a previous client. */
                    fds[slot + 1].revents = 0;
                }
            }
        }
        uint64_t now = milliseconds();
        for (unsigned i = 0; i < CLIENTS; ++i) {
            Client *c = &server.clients[i]; if (c->fd < 0) continue;
            short events = fds[i + 1].revents;
            if (now >= c->deadline || (events & (POLLERR | POLLNVAL))) close_client(c);
            else if (c->responding && (events & POLLOUT)) transmit(c);
            else if (!c->responding && (events & POLLIN)) receive(c);
            else if (events & POLLHUP) close_client(c);
        }
    }
    for (unsigned i = 0; i < CLIENTS; ++i) close_client(&server.clients[i]);
    return NULL;
}
int ks_web_start(unsigned port, bool preview)
{
    if (server.started || port > 65535) { errno = EINVAL; return -1; }
    int fd = socket(AF_INET, SOCK_STREAM, 0); if (fd < 0) return -1;
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET; address.sin_port = htons((uint16_t)port);
    address.sin_addr.s_addr = htonl(preview ? INADDR_LOOPBACK : INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(fd, CLIENTS) != 0 || nonblocking(fd) != 0) {
        int saved = errno; close(fd); errno = saved; return -1;
    }
    socklen_t size = sizeof(address);
    if (getsockname(fd, (struct sockaddr *)&address, &size) != 0) {
        int saved = errno; close(fd); errno = saved; return -1;
    }
    server.listener = fd; server.port = ntohs(address.sin_port); server.preview = preview;
    memset(&server.state, 0, sizeof(server.state));
    memset(server.key_seen, 0, sizeof(server.key_seen));
    memset(server.button_seen, 0, sizeof(server.button_seen));
    ks_config_defaults(&server.state.config); ks_pad_neutral(&server.state.pad);
    for (unsigned i = 0; i < CLIENTS; ++i) server.clients[i].fd = -1;
    atomic_store(&server.running, true);
    int result = pthread_create(&server.thread, NULL, serve, NULL);
    if (result) { close(fd); server.listener = -1; atomic_store(&server.running, false); errno = result; return -1; }
    server.started = true;
    return 0;
}
unsigned ks_web_port(void) { return server.started ? server.port : 0; }
void ks_web_publish(const KsWebSnapshot *state)
{
    if (!server.started) return;
    /* Gameplay never waits for the HTTP worker. A later frame replaces a
     * skipped snapshot if the worker is copying this one. */
    if (pthread_mutex_trylock(&server.mutex) == 0) {
        server.state = *state;
        uint64_t now = state->uptime_us + 1;
        for (unsigned k = 0; k < KS_KEY_COUNT; ++k) {
            if (state->keys.keys[k]) server.key_seen[k] = now;
            server.state.pulse_keys.keys[k] = server.key_seen[k] && now - server.key_seen[k] < 100000;
        }
        server.state.pulse_buttons = 0;
        for (unsigned bit = 0; bit < 32; ++bit) {
            if (state->pad.buttons & (1u << bit)) server.button_seen[bit] = now;
            if (server.button_seen[bit] && now - server.button_seen[bit] < 100000)
                server.state.pulse_buttons |= 1u << bit;
        }
        pthread_mutex_unlock(&server.mutex);
    }
}
void ks_web_stop(void)
{
    if (!server.started) return;
    atomic_store(&server.running, false);
    pthread_join(server.thread, NULL);
    close(server.listener); server.listener = -1; server.started = false;
}
