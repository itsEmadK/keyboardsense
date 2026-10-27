/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "web.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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
    printf("Local URL: http://127.0.0.1:%u/\n", ks_web_port()); fflush(stdout);
    KsWebSnapshot snapshot = {0};
    ks_config_defaults(&snapshot.config); ks_pad_neutral(&snapshot.pad);
    char error[128];
    if (ks_config_load("config/config.ini", &snapshot.config, error, sizeof(error)) != 0)
        fprintf(stderr, "%s (using defaults)\n", error);
    while (running) {
        snapshot.sequence++; snapshot.uptime_us += 20000;
        ks_web_publish(&snapshot);
        struct timespec delay = {.tv_sec = 0, .tv_nsec = 20000000}; nanosleep(&delay, NULL);
    }
    ks_web_stop(); return 0;
}
