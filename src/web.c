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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define CLIENTS 8
#define REQUEST_SIZE 16384
#define JSON_SIZE 12288
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
    append(&w, "],\"mouse\":[%u,%u,%u]},\"profile\":{\"revision\":%llu,\"title\":", (unsigned)c->mouse[0], (unsigned)c->mouse[1], (unsigned)c->mouse[2],(unsigned long long)s->profile_revision);
    string(&w,s->title,sizeof(s->title));
    append(&w,",\"saved\":%s,\"editor_open\":%s,\"native_ready\":%s,\"status\":",s->game_profile?"true":"false",s->editor_open?"true":"false",s->native_ready?"true":"false");
    string(&w,s->profile_status,sizeof(s->profile_status));
    append(&w,"},\"command\":{\"id\":%llu,\"status\":%d,\"message\":",(unsigned long long)s->command_id,s->command_status);
    string(&w,s->command_message,sizeof(s->command_message));append(&w,"}}");
    return w.failed ? -1 : (int)w.used;
}

typedef struct {
    int fd;
    char request[REQUEST_SIZE], header[512], json[JSON_SIZE];
    size_t received, header_size, header_sent, body_sent, body_size;
    size_t request_header_size, expected_size;
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
    KsWebCommand command;
    bool pending;
    uint64_t next_command_id;
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
static bool header_value(const Client *c,const char *name,char *out,size_t capacity)
{
    const char *p=strstr(c->request,"\r\n");if(!p)return false;
    p+=2;bool found=false;size_t wanted=strlen(name);
    while(*p&&p<c->request+c->request_header_size-2){
        const char *end=strstr(p,"\r\n");if(!end)return false;
        if((size_t)(end-p)>wanted&&p[wanted]==':'&&!strncasecmp(p,name,wanted)){
            if(found)return false;
            const char *v=p+wanted+1;while(v<end&&(*v==' '||*v=='\t'))v++;
            size_t length=(size_t)(end-v);if(length>=capacity)return false;
            memcpy(out,v,length);out[length]=0;found=true;
        }
        p=end+2;
    }
    return found;
}
static bool number_line(const char **p,const char *end,const char *key,uint64_t *value)
{
    size_t prefix=strlen(key);
    if((size_t)(end-*p)<=prefix||strncmp(*p,key,prefix))return false;
    const char *start=*p+prefix,*line=memchr(start,'\n',(size_t)(end-start));
    if(!line||line==start||line-start>20)return false;
    uint64_t n=0;
    for(const char *s=start;s<line;++s){
        if(*s<'0'||*s>'9'||n>(UINT64_MAX-(unsigned)(*s-'0'))/10)return false;
        n=n*10+(unsigned)(*s-'0');
    }
    *value=n;*p=line+1;return true;
}
static void settings_route(Client *c)
{
    char value[100];
    if(!header_value(c,"X-KeyboardSense",value,sizeof(value))||strcmp(value,"editor-v1")||
       !header_value(c,"Content-Type",value,sizeof(value))||strcmp(value,"application/x-keyboardsense-config")){
        error_response(c,403,"Editor Header Required");return;
    }
    const char *p=c->request+c->request_header_size,*end=c->request+c->expected_size;
    if(memchr(p,0,(size_t)(end-p))){error_response(c,400,"Invalid Settings");return;}
    KsWebCommand command={0};uint64_t save;
    if(!number_line(&p,end,"revision=",&command.revision)||!number_line(&p,end,"save=",&save)||save>2||end-p<7||strncmp(p,"title=",6)){
        error_response(c,400,"Invalid Settings");return;
    }
    p+=6;const char *line=memchr(p,'\n',(size_t)(end-p));
    if(!line||line-p>=KS_TITLE_SIZE){error_response(c,400,"Invalid Title");return;}
    memcpy(command.title,p,(size_t)(line-p));p=line+1;command.save=(unsigned)save;
    if((*command.title&&!ks_title_valid(command.title))||ks_config_parse(p,(size_t)(end-p),&command.config,value,sizeof(value))!=0){
        error_response(c,400,"Invalid Settings");return;
    }
    pthread_mutex_lock(&server.mutex);
    unsigned failure=0;
    if(server.pending)failure=503;
    else if(command.revision!=server.state.profile_revision||strcmp(command.title,server.state.title)||server.state.editor_open)failure=409;
    else{command.id=++server.next_command_id;server.command=command;server.pending=true;}
    pthread_mutex_unlock(&server.mutex);
    if(failure){error_response(c,failure,failure==503?"Editor Busy":"Profile Changed");return;}
    int n=snprintf(c->json,sizeof(c->json),"{\"id\":%llu}",(unsigned long long)command.id);
    response(c,202,"Accepted","application/json",(const unsigned char*)c->json,(size_t)n);
}
static void route(Client *c)
{
    /* Parse exactly one bounded request line. Query strings do not affect the
     * resource; no path is translated into a filesystem path. */
    char *line_end = strstr(c->request, "\r\n");
    if (!line_end) { error_response(c, 400, "Bad Request"); return; }
    char method[8], path[256], version[16], extra;
    char first_line[300];size_t line_size=(size_t)(line_end-c->request);
    if(line_size>=sizeof(first_line)){error_response(c,400,"Bad Request");return;}
    memcpy(first_line,c->request,line_size);first_line[line_size]=0;
    if (sscanf(first_line, "%7s %255s %15s %c", method, path, version, &extra) != 3 ||
        (strcmp(version, "HTTP/1.1") && strcmp(version, "HTTP/1.0"))) {
        error_response(c, 400, "Bad Request"); return;
    }
    char *query = strchr(path, '?'); if (query) *query = 0;
    if(!strcmp(method,"POST")&&!strcmp(path,"/api/config")){settings_route(c);return;}
    if (strcmp(method, "GET")) { error_response(c, 405, "Method Not Allowed"); return; }
    if (!strcmp(path, "/api/state")||!strcmp(path,"/api/defaults")||!strcmp(path,"/api/global")) {
        KsWebSnapshot state;
        pthread_mutex_lock(&server.mutex); state = server.state; pthread_mutex_unlock(&server.mutex);
        if(!strcmp(path,"/api/defaults"))ks_config_defaults(&state.config);
        if(!strcmp(path,"/api/global"))state.config=state.global_config;
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
    if(!c->request_header_size){
        char *end=strstr(c->request,"\r\n\r\n");
        if(!end){if(c->received>=2048)error_response(c,431,"Request Header Too Large");return;}
        c->request_header_size=(size_t)(end-c->request)+4;
        if(c->request_header_size>2048){error_response(c,431,"Request Header Too Large");return;}
        c->expected_size=c->request_header_size;
        if(!strncmp(c->request,"POST ",5)){
            char length[32],encoding[64];
            if(header_value(c,"Transfer-Encoding",encoding,sizeof(encoding))||!header_value(c,"Content-Length",length,sizeof(length))){error_response(c,400,"Invalid Body Length");return;}
            char *last;errno=0;unsigned long size=strtoul(length,&last,10);
            if(errno||!*length||*last||size>12000){error_response(c,413,"Settings Too Large");return;}
            c->expected_size+=size;
        }
    }
    if(c->received>=c->expected_size)route(c);
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
    server.pending=false;server.next_command_id=0;
    memset(&server.state, 0, sizeof(server.state));
    memset(server.key_seen, 0, sizeof(server.key_seen));
    memset(server.button_seen, 0, sizeof(server.button_seen));
    ks_config_defaults(&server.state.config);server.state.global_config=server.state.config; ks_pad_neutral(&server.state.pad);
    for (unsigned i = 0; i < CLIENTS; ++i) server.clients[i].fd = -1;
    atomic_store(&server.running, true);
    int result = pthread_create(&server.thread, NULL, serve, NULL);
    if (result) { close(fd); server.listener = -1; atomic_store(&server.running, false); errno = result; return -1; }
    server.started = true;
    return 0;
}
unsigned ks_web_port(void) { return server.started ? server.port : 0; }
bool ks_web_take_command(KsWebCommand *out)
{
    if(!server.started||pthread_mutex_trylock(&server.mutex)!=0)return false;
    bool have=server.pending;
    if(have){*out=server.command;server.pending=false;}
    pthread_mutex_unlock(&server.mutex);return have;
}
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
