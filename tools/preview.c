/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE
#include "web.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
static volatile sig_atomic_t running = 1;
static void stop(int signal_number) { (void)signal_number; running = 0; }
int main(int argc, char **argv)
{
    unsigned long port = KS_WEB_PORT;
    if (argc > 1) {
        char *end; port = strtoul(argv[1], &end, 10);
        if (!*argv[1] || *end || port > 65535) return 2;
    }
    signal(SIGINT, stop); signal(SIGTERM, stop);
    if (ks_web_start((unsigned)port, true) != 0) {
        fprintf(stderr, "Cannot start preview: %s\n", strerror(errno)); return 1;
    }
    KsWebSnapshot snapshot = {0};
    char root[]="/tmp/keyboardsense-preview-XXXXXX";
    if(!mkdtemp(root)){ks_web_stop();return 1;}
    KsEditor editor;if(ks_editor_init(&editor,root)!=0){ks_web_stop();return 1;}
    ks_pad_neutral(&snapshot.pad);
    char error[128];
    if (ks_config_load("config/config.ini", &editor.active, error, sizeof(error)) != 0)
        fprintf(stderr, "%s (using defaults)\n", error);
    editor.global=editor.draft=editor.active;
    snapshot.config=editor.active;snapshot.global_config=editor.global;snapshot.profile_revision=editor.revision;ks_web_publish(&snapshot);
    printf("Local URL: http://127.0.0.1:%u/\n",ks_web_port());fflush(stdout);
    while (running) {
        KsWebCommand command;
        if(ks_web_take_command(&command)){
            snapshot.command_id=command.id;
            snapshot.command_status=ks_editor_commit(&editor,&command.config,command.revision,command.title,command.save);
            snprintf(snapshot.command_message,sizeof(snapshot.command_message),"%s",editor.status);
        }
        snapshot.config=editor.active;snapshot.global_config=editor.global;snapshot.profile_revision=editor.revision;
        snprintf(snapshot.profile_status,sizeof(snapshot.profile_status),"%s",editor.status);
        snapshot.sequence++; snapshot.uptime_us += 20000;
        ks_web_publish(&snapshot);
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 20000000}; nanosleep(&delay, NULL);
    }
    ks_web_stop();
    char path[512];snprintf(path,sizeof(path),"%s/config.ini",root);unlink(path);
    snprintf(path,sizeof(path),"%s/profiles",root);rmdir(path);rmdir(root);
    return 0;
}
