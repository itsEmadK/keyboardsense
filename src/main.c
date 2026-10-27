/* SPDX-License-Identifier: GPL-3.0-only */
#include "input.h"
#include "usb_input.h"
#include "vpad.h"
#include "web.h"
#include "editor.h"
#include "game.h"
#include "overlay.h"
#include "media.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <time.h>
#include <unistd.h>
#include <ps5/klog.h>

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
    KsEditor editor;
    if(ks_editor_init(&editor,"/data/keyboardsense")!=0){notify("cannot initialize profile directory");close(lock_fd);return 1;}
    if(*editor.status)notify(editor.status);
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
    uint64_t mapped_revision=editor.revision,last_command=0;
    int command_status=0;char command_message[128]="";
    int result = 0;
    if(ks_overlay_start()!=0)klog_printf("[KeyboardSense] Native sidebar unavailable errno=%d; web editor remains available\n",errno);
    if (ks_web_start(KS_WEB_PORT, false) != 0) {
        klog_printf("[KeyboardSense] web server start failed errno=%d\n", errno);
        notify("input active, but web UI could not bind port 1405");
    } else {
        char media_error[160];
        if (ks_media_install_ps5(media_error, sizeof(media_error)) < 0) {
            klog_printf("[KeyboardSense] %s\n", media_error);
            notify("input active; Media shortcut unavailable. Open http://<PS5-IP>:1405/. F12 stops.");
        } else notify("v0.3.1 running. Media > KeyboardSense opens settings. F8 / Ctrl+Alt+M: sidebar. F12 stops.");
    }
    while (running) {
        uint64_t start = now_us();
        if (start >= next_scan) {
            if (ks_usb_scan(&usb)) ks_mapper_reset(&mapper);
            KsGame game;
            int discovered=ks_game_query(&game);
            ks_overlay_game(discovered==1?game.pid:-1);
            ks_editor_game(&editor,discovered==1?game.title:"");
            next_scan = now_us() + USB_SCAN_US;
        }
        KsKeyboard keys;
        uint8_t buttons;
        int dx, dy;
        if (ks_usb_poll(&usb, &keys, &buttons, &dx, &dy)) ks_mapper_reset(&mapper);
        if (keys.keys[KEY_F12]) break;
        KsWebCommand command;
        if(ks_web_take_command(&command)){
            last_command=command.id;
            if(editor.open){command_status=-1;snprintf(command_message,sizeof(command_message),"Close the on-console editor first");}
            else{command_status=ks_editor_commit(&editor,&command.config,command.revision,command.title,command.save);snprintf(command_message,sizeof(command_message),"%s",editor.status);}
        }
        bool native_ready=ks_overlay_ready(now_us());
        bool sidebar_shortcut=(keys.keys[0x41]&&!previous.keys[0x41])||((keys.keys[224]||keys.keys[228])&&(keys.keys[226]||keys.keys[230])&&keys.keys[16]&&!previous.keys[16]);
        bool consume=ks_editor_tick(&editor,&keys,buttons,dx,dy,now_us(),native_ready);
        if(sidebar_shortcut&&!native_ready)notify(editor.status);
        if (keys.keys[KEY_F10] && !previous.keys[KEY_F10] && !consume) {
            enabled = !enabled;
            ks_mapper_reset(&mapper);
            notify(enabled ? "input resumed" : "input paused (F10 resumes)");
        }
        if (keys.keys[KEY_F9] && !previous.keys[KEY_F9] && !consume) {
            if (ks_editor_reload(&editor) != 0) notify(editor.status);
            else { ks_mapper_reset(&mapper); notify("configuration reloaded"); }
        }
        if(mapped_revision!=editor.revision){ks_mapper_reset(&mapper);mapped_revision=editor.revision;}
        previous = keys;
        KsPad state;
        if (enabled&&!consume) ks_map(&mapper, &editor.active, &keys, buttons, dx, dy, now_us(), &state);
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
        KsWebSnapshot snapshot = {.keys = keys, .pad = state, .config = editor.active,.global_config=editor.global,
            .mouse_buttons = buttons, .mouse_dx = dx, .mouse_dy = dy,
            .enabled = enabled&&!consume, .submit_ok = submit_result == 0,
            .uptime_us = now_us() - started, .sequence = ++sequence,
            .profile_revision=editor.revision,.game_profile=editor.game_profile,
            .editor_open=editor.open,.native_ready=native_ready,.command_id=last_command,.command_status=command_status};
        snprintf(snapshot.title,sizeof(snapshot.title),"%s",editor.title);
        snprintf(snapshot.profile_status,sizeof(snapshot.profile_status),"%s",editor.status);
        snprintf(snapshot.command_message,sizeof(snapshot.command_message),"%s",command_message);
        for (unsigned i = 0; i < KS_DEVICE_LIMIT; ++i) {
            const KsUsbDevice *d = &usb.devices[i];
            if (!d->active) continue;
            KsWebDevice *device = &snapshot.devices[snapshot.device_count++];
            snprintf(device->name, sizeof(device->name), "%s", d->name);
            device->protocol = d->iface.protocol;
            device->interface_number = d->iface.number;
        }
        ks_web_publish(&snapshot);
        if(sequence%8==0){KsSidebarFrame frame;ks_editor_frame(&editor,sequence,&frame);ks_overlay_publish(&frame,now_us());}
        uint64_t elapsed = now_us() - start;
        if (elapsed < TICK_US) usleep((unsigned)(TICK_US - elapsed));
    }
    ks_vpad_close(&pad);
    editor.open=false;KsSidebarFrame frame;ks_editor_frame(&editor,++sequence,&frame);ks_overlay_publish(&frame,now_us());
    ks_overlay_stop();
    ks_web_stop();
    ks_usb_shutdown(&usb);
    close(lock_fd); /* Keep lock inode; removing it introduces a race. */
    notify("stopped. Replug USB devices to restore native keyboard/mouse handling.");
    return result;
}
