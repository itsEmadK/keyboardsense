/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KS_WEB_H
#define KS_WEB_H
#include "input.h"
#include "editor.h"
#define KS_WEB_PORT 1405

typedef struct {
    const char *path, *mime;
    const unsigned char *data;
    size_t size;
} KsWebAsset;
extern const KsWebAsset ks_web_assets[];
extern const size_t ks_web_asset_count;

typedef struct { char name[24]; uint8_t protocol, interface_number; } KsWebDevice;
typedef struct {
    KsKeyboard keys;
    KsKeyboard pulse_keys; /* 100-ms afterglow for taps between HTTP polls. */
    uint32_t pulse_buttons;
    KsPad pad;
    KsConfig config, global_config;
    KsWebDevice devices[KS_DEVICE_LIMIT];
    unsigned device_count;
    uint8_t mouse_buttons;
    int mouse_dx, mouse_dy;
    bool enabled, submit_ok;
    uint64_t uptime_us, sequence;
    char title[KS_TITLE_SIZE], profile_status[128], command_message[128];
    uint64_t profile_revision, command_id;
    bool game_profile, editor_open, native_ready;
    int command_status;
} KsWebSnapshot;
typedef struct {
    KsConfig config;
    char title[KS_TITLE_SIZE];
    uint64_t id, revision;
    unsigned save;
} KsWebCommand;
/* The network worker queues settings; only the input thread applies them. */
bool ks_web_take_command(KsWebCommand *);
/* A bounded, nonblocking worker serves embedded assets, telemetry, and a
 * revision-checked settings queue consumed by the input thread.
 * preview only labels the source; it does not enable remote input injection. */
int ks_web_start(unsigned port, bool preview);
unsigned ks_web_port(void);
void ks_web_publish(const KsWebSnapshot *);
void ks_web_stop(void);
/* Exposed for serialization tests. Returns bytes or -1 on capacity error. */
int ks_web_json(const KsWebSnapshot *, bool preview, char *, size_t);
#endif
