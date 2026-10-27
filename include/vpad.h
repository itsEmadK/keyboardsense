/* SPDX-License-Identifier: GPL-3.0-only
 * /dev/hid ABI adapted from BallerIsLeet/AnyController. See NOTICE. */
#ifndef KS_VPAD_H
#define KS_VPAD_H
#include "input.h"
#include <stddef.h>

typedef struct __attribute__((packed)) {
    uint8_t reserved0[0x0c];
    uint32_t buttons;
    uint8_t lx, ly, rx, ry, l2, r2, reserved1, active;
    uint8_t reserved2[0x60];
    float orient_x, orient_y, orient_z, orient_w, motion_rest[4];
    uint8_t reserved3[8];
} KsVpadReport;
_Static_assert(sizeof(KsVpadReport) == 0xa0, "virtual pad report ABI");
_Static_assert(offsetof(KsVpadReport, buttons) == 0x0c, "buttons ABI");
_Static_assert(offsetof(KsVpadReport, active) == 0x17, "active ABI");
_Static_assert(offsetof(KsVpadReport, orient_w) == 0x84, "orientation ABI");
typedef struct { int fd; int32_t handle; } KsVpad;
int ks_vpad_open(KsVpad *);
int ks_vpad_submit(KsVpad *, const KsPad *);
void ks_vpad_close(KsVpad *);
#endif
