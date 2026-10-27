/* SPDX-License-Identifier: GPL-3.0-only */
extern "C" {
#include "overlay.h"
#include <ps5/klog.h>
}
#include "shellui_injector.hpp"
#include "stable_sampler/proc_rw_v960.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
std::atomic<bool> running{false};
std::atomic<pid_t> game_pid{-1};
pthread_t worker{};int socket_fd=-1;
std::uint64_t last_ack=0;
void* inject_worker(void*) {
    pid_t last_shell=-1,last_game=-1;unsigned shell_ticks=0,game_ticks=0;
    while(running.load()) {
        pid_t shell=common_fps::ps5::observe_shellui_pid(),game=game_pid.load();
        shell_ticks=shell>0&&shell==last_shell?shell_ticks+1:0;
        game_ticks=game>0&&game==last_game?game_ticks+1:0;
        last_shell=shell;last_game=game;
        if(shell_ticks>=10&&game_ticks>=3) {
            (void)common_fps::ps5::ensure_shellui_renderer();
            shell_ticks=10;game_ticks=3;
        }
        for(unsigned i=0;i<10&&running.load();++i)usleep(100000);
    }
    return nullptr;
}
}
extern "C" int ks_overlay_start(void) {
    const auto sdk=common_fps::ps5::stable_sampler::firmware_sdk_version();
    const auto family=sdk&0xffff0000U;
    if(!(family==0x09600000U||(family>=0x11000000U&&family<=0x13600000U))){
        klog_printf("[KeyboardSense] Native sidebar disabled on unverified firmware 0x%08x\n",sdk);
        errno=ENOTSUP;return -1;
    }
    socket_fd=socket(AF_INET,SOCK_DGRAM,0);if(socket_fd<0)return -1;
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(socket_fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))!=0||fcntl(socket_fd,F_SETFL,O_NONBLOCK)!=0){close(socket_fd);socket_fd=-1;return -1;}
    running.store(true);
    int rc=pthread_create(&worker,nullptr,inject_worker,nullptr);
    if(rc){running.store(false);close(socket_fd);socket_fd=-1;errno=rc;return -1;}
    return 0;
}
extern "C" void ks_overlay_game(pid_t pid){game_pid.store(pid);}
extern "C" bool ks_overlay_ready(std::uint64_t now){return socket_fd>=0&&game_pid.load()>0&&last_ack&&now>=last_ack&&now-last_ack<1000000;}
extern "C" void ks_overlay_publish(const KsSidebarFrame* frame,std::uint64_t now){
    if(socket_fd<0)return;
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(KS_SIDEBAR_PORT);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    (void)sendto(socket_fd,frame,sizeof(*frame),MSG_DONTWAIT,reinterpret_cast<sockaddr*>(&address),sizeof(address));
    for(unsigned i=0;i<8;++i){
        KsSidebarAck ack{};sockaddr_in peer{};socklen_t size=sizeof(peer);
        ssize_t n=recvfrom(socket_fd,&ack,sizeof(ack),MSG_DONTWAIT,reinterpret_cast<sockaddr*>(&peer),&size);
        if(n<0)break;
        if(n==sizeof(ack)&&peer.sin_addr.s_addr==htonl(INADDR_LOOPBACK)&&peer.sin_port==htons(KS_SIDEBAR_PORT)&&ack.magic==KS_SIDEBAR_MAGIC&&ack.version==KS_SIDEBAR_VERSION&&ack.sequence>0&&ack.sequence<=frame->sequence&&frame->sequence-ack.sequence<250&&ack.visible<=1&&ack.active<=1)last_ack=ack.active?now:0;
    }
}
extern "C" void ks_overlay_stop(void){
    if(socket_fd<0)return;
    // Let any in-progress attach/patch transaction finish and restore ShellUI.
    running.store(false);pthread_join(worker,nullptr);
    close(socket_fd);socket_fd=-1;last_ack=0;
}
