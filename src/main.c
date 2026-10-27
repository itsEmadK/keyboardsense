/* SPDX-License-Identifier: GPL-3.0-only */
#include "input.h"
#include "usb_input.h"
#include "vpad.h"
#include "web.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>
#include <ps5/klog.h>

#define CONFIG_PATH "/data/keyboardsense/config.ini"
#define TICK_US 4000
#define USB_SCAN_US 1000000
#define KEY_F9 0x42
#define KEY_F10 0x43
#define KEY_F12 0x45

static volatile sig_atomic_t running = 1;
static void stop(int sig) { (void)sig; running = 0; }
static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

typedef struct { char reserved[45], message[3075]; } Notification;
int sceKernelSendNotificationRequest(int, Notification *, size_t, int);
static void notify(const char *message)
{
    Notification request = {0};
    snprintf(request.message, sizeof(request.message), "KeyboardSense: %s", message);
    sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
    klog_printf("[KeyboardSense] %s\n", message);
}

int main(void)
{
    signal(SIGINT, stop); signal(SIGTERM, stop);
    int lock_fd = open("/data/keyboardsense.lock", O_CREAT | O_RDWR, 0600);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        notify("already running, or cannot acquire /data/keyboardsense.lock");
        if (lock_fd >= 0) close(lock_fd);
        return 1;
    }
    KsConfig config;
    ks_config_defaults(&config);
    char error[200];
    if (access(CONFIG_PATH, F_OK) == 0 && ks_config_load(CONFIG_PATH, &config, error, sizeof(error)) != 0)
        notify(error);
    KsVpad pad = {.fd = -1, .handle = -1};
    if (ks_vpad_open(&pad) != 0) {
        klog_printf("[KeyboardSense] virtual DualSense creation failed errno=%d\n", errno);
        notify("cannot create DualSense; check firmware/HEN and /dev/hid permissions");
        close(lock_fd); return 1;
    }
    KsUsb usb;
    ks_usb_init(&usb);
    KsMapper mapper;
    ks_mapper_reset(&mapper);
    KsKeyboard previous = {0};
    bool enabled = true;
    unsigned failures = 0;
    uint64_t next_scan = 0;
    uint64_t started = now_us(), sequence = 0;
    int result = 0;
    if (ks_web_start(KS_WEB_PORT, false) != 0) {
        klog_printf("[KeyboardSense] web server start failed errno=%d\n", errno);
        notify("input active, but web UI could not bind port 1405");
    } else notify("v0.2.0 running. Test at http://<PS5-IP>:1405/. Assign the new controller to your player. F12 stops.");
    while (running) {
        uint64_t start = now_us();
        if (start >= next_scan) {
            if (ks_usb_scan(&usb)) ks_mapper_reset(&mapper);
            next_scan = now_us() + USB_SCAN_US;
        }
        KsKeyboard keys;
        uint8_t buttons;
        int dx, dy;
        if (ks_usb_poll(&usb, &keys, &buttons, &dx, &dy)) ks_mapper_reset(&mapper);
        if (keys.keys[KEY_F12]) break;
        if (keys.keys[KEY_F10] && !previous.keys[KEY_F10]) {
            enabled = !enabled;
            ks_mapper_reset(&mapper);
            notify(enabled ? "input resumed" : "input paused (F10 resumes)");
        }
        if (keys.keys[KEY_F9] && !previous.keys[KEY_F9]) {
            if (ks_config_load(CONFIG_PATH, &config, error, sizeof(error)) != 0) notify(error);
            else { ks_mapper_reset(&mapper); notify("configuration reloaded"); }
        }
        previous = keys;
        KsPad state;
        if (enabled) ks_map(&mapper, &config, &keys, buttons, dx, dy, now_us(), &state);
        else ks_pad_neutral(&state);
        int submit_result = ks_vpad_submit(&pad, &state);
        if (submit_result != 0) {
            if (++failures == 1)
                klog_printf("[KeyboardSense] pad submit failed errno=%d\n", errno);
            if (failures >= 250) {
                notify("virtual pad stopped accepting input; shutting down");
                result = 1; break;
            }
        } else failures = 0;
        KsWebSnapshot snapshot = {.keys = keys, .pad = state, .config = config,
            .mouse_buttons = buttons, .mouse_dx = dx, .mouse_dy = dy,
            .enabled = enabled, .submit_ok = submit_result == 0,
            .uptime_us = now_us() - started, .sequence = ++sequence};
        for (unsigned i = 0; i < KS_DEVICE_LIMIT; ++i) {
            const KsUsbDevice *d = &usb.devices[i];
            if (!d->active) continue;
            KsWebDevice *device = &snapshot.devices[snapshot.device_count++];
            snprintf(device->name, sizeof(device->name), "%s", d->name);
            device->protocol = d->iface.protocol;
            device->interface_number = d->iface.number;
        }
        ks_web_publish(&snapshot);
        uint64_t elapsed = now_us() - start;
        if (elapsed < TICK_US) usleep((unsigned)(TICK_US - elapsed));
    }
    ks_vpad_close(&pad);
    ks_web_stop();
    ks_usb_shutdown(&usb);
    close(lock_fd); /* Keep lock inode; removing it introduces a race. */
    notify("stopped. Replug USB devices to restore native keyboard/mouse handling.");
    return result;
}
