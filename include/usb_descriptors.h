/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KS_USB_DESCRIPTORS_H
#define KS_USB_DESCRIPTORS_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t index, number, protocol, endpoint;
    uint16_t packet_size;
} KsBootInterface;
/* Parse a complete active USB configuration. Only HID boot keyboard/mouse
 * altsetting 0 with interrupt IN endpoints are selected. -1: malformed. */
int ks_boot_interfaces(const uint8_t *, size_t, KsBootInterface *, size_t);
#endif
