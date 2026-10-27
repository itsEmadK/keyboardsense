/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KS_WEB_H
#define KS_WEB_H
#include "input.h"
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
    KsConfig config;
    KsWebDevice devices[KS_DEVICE_LIMIT];
    unsigned device_count;
    uint8_t mouse_buttons;
    int mouse_dx, mouse_dy;
    bool enabled, submit_ok;
    uint64_t uptime_us, sequence;
} KsWebSnapshot;
/* A bounded, nonblocking worker serves embedded assets and read-only JSON.
 * preview only labels the source; it does not enable remote input injection. */
int ks_web_start(unsigned port, bool preview);
unsigned ks_web_port(void);
void ks_web_publish(const KsWebSnapshot *);
void ks_web_stop(void);
/* Exposed for serialization tests. Returns bytes or -1 on capacity error. */
int ks_web_json(const KsWebSnapshot *, bool preview, char *, size_t);
#endif
