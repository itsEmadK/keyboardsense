/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "web.h"
#include "sce_pad.h"
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
static int connect_local(void)
{
    int fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);
    struct sockaddr_in addr;memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;addr.sin_port=htons((uint16_t)ks_web_port());
    assert(inet_pton(AF_INET,"127.0.0.1",&addr.sin_addr)==1);
    struct timeval timeout={.tv_sec=2};
    assert(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))==0);
    assert(connect(fd,(struct sockaddr *)&addr,sizeof(addr))==0);return fd;
}
static void get_state(char *body,size_t size)
{
    int fd=connect_local();const char request[]="GET /api/state HTTP/1.1\r\nHost: localhost\r\n\r\n";
    assert(send(fd,request,sizeof(request)-1,0)==(ssize_t)sizeof(request)-1);
    size_t used=0;ssize_t n;
    while((n=recv(fd,body+used,size-used-1,0))>0){used+=(size_t)n;assert(used<size-1);}
    assert(n==0);body[used]=0;close(fd);assert(strstr(body,"HTTP/1.1 200 OK"));
}
int main(void)
{
    assert(ks_web_start(0,true)==0);assert(ks_web_port()!=0);
    assert(ks_web_start(0,true)==-1); /* Does not disturb the existing listener. */
    KsWebSnapshot state={0};ks_config_defaults(&state.config);ks_pad_neutral(&state.pad);
    state.uptime_us=1000000;state.sequence=1;state.keys.keys[26]=true;state.pad.buttons=SCE_PAD_BUTTON_CROSS;
    ks_web_publish(&state);
    char body[10000];get_state(body,sizeof(body));
    assert(strstr(body,"\"keys\":[26]")&&strstr(body,"\"pulse_keys\":[26]"));
    state.uptime_us+=4000;state.sequence++;state.keys.keys[26]=false;state.pad.buttons=0;
    ks_web_publish(&state);get_state(body,sizeof(body));
    assert(strstr(body,"\"keys\":[]")&&strstr(body,"\"pulse_keys\":[26]"));
    assert(strstr(body,"\"pulse_buttons\":16384"));
    state.uptime_us+=101000;state.sequence++;
    ks_web_publish(&state);get_state(body,sizeof(body));
    assert(strstr(body,"\"pulse_keys\":[]")&&strstr(body,"\"pulse_buttons\":0"));
    int unfinished=connect_local();assert(send(unfinished,"GET /",5,0)==5);
    struct timespec before,after;clock_gettime(CLOCK_MONOTONIC,&before);
    ks_web_stop();clock_gettime(CLOCK_MONOTONIC,&after);close(unfinished);
    double elapsed=after.tv_sec-before.tv_sec+(after.tv_nsec-before.tv_nsec)/1e9;
    assert(elapsed<1&&ks_web_port()==0);
    ks_web_publish(&state); /* Safe after shutdown. */
    assert(ks_web_start(0,true)==0);get_state(body,sizeof(body));
    assert(strstr(body,"\"keys\":[]")&&strstr(body,"\"pulse_keys\":[]"));
    ks_web_stop();ks_web_stop();
    puts("Web lifecycle passed: snapshots, short-tap afterglow/expiry, bounded shutdown, restart.");
    return 0;
}
