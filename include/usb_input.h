/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KS_USB_INPUT_H
#define KS_USB_INPUT_H
#include "input.h"
#include "usb_descriptors.h"
typedef struct {
    int control_fd, input_fd;
    char name[24];
    KsBootInterface iface;
    KsKeyboard keyboard;
    uint8_t mouse_buttons;
    bool active, seen;
} KsUsbDevice;
typedef struct { KsUsbDevice devices[KS_DEVICE_LIMIT]; } KsUsb;
void ks_usb_init(KsUsb *);
/* Returns true if a removed mouse requires resetting filtered motion. */
bool ks_usb_scan(KsUsb *);
/* Aggregate current held keys/buttons and this tick's mouse deltas. Returns
 * true when a mouse disconnect requires clearing any filtered motion. */
bool ks_usb_poll(KsUsb *, KsKeyboard *, uint8_t *buttons, int *dx, int *dy);
void ks_usb_shutdown(KsUsb *);
#endif
