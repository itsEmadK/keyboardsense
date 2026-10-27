/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from BallerIsLeet/AnyController, src/vpad.c. See NOTICE. */
#include "vpad.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define HID_ADD_DEVICE  0xC018482AUL
#define HID_INSERT_DATA 0x8018482CUL
#define HID_DEL_DEVICE  0x80104850UL

typedef struct { uint32_t cmd, reserved; void *data, *out; } HidRequest;
typedef struct { uint8_t cmd, pad[7]; uint32_t handle, pad2; } HidDelRequest;
_Static_assert(sizeof(HidRequest) == 24, "HID request ABI");
_Static_assert(sizeof(HidDelRequest) == 16, "HID delete ABI");

int ks_vpad_open(KsVpad *v)
{
    v->handle = -1;
    v->fd = open("/dev/hid", O_RDWR);
    if (v->fd < 0) return -1;
    uint8_t descriptor[168] = {0};
    uint32_t user = 1; /* Registration user from AnyController/FGG-XSense.
                       * Shell's assignment UI selects the actual player. */
    descriptor[0] = 3;
    memcpy(descriptor + 0x1c, &user, sizeof(user));
    descriptor[0x20] = 1;
    HidRequest request = {.cmd = 3, .data = descriptor, .out = &v->handle};
    if (ioctl(v->fd, HID_ADD_DEVICE, &request) != 0 || v->handle < 0) {
        int saved = errno;
        close(v->fd); v->fd = -1;
        errno = saved ? saved : EIO;
        return -1;
    }
    return 0;
}

int ks_vpad_submit(KsVpad *v, const KsPad *pad)
{
    KsVpadReport report = {0};
    report.buttons = pad->buttons;
    report.lx = pad->lx; report.ly = pad->ly;
    report.rx = pad->rx; report.ry = pad->ry;
    report.l2 = pad->l2; report.r2 = pad->r2;
    report.active = 1; report.orient_w = 1;
    HidRequest request = {.cmd = (uint32_t)v->handle, .data = &report};
    return ioctl(v->fd, HID_INSERT_DATA, &request);
}

void ks_vpad_close(KsVpad *v)
{
    if (v->fd < 0) return;
    if (v->handle >= 0) {
        KsPad neutral;
        ks_pad_neutral(&neutral);
        ks_vpad_submit(v, &neutral);
        HidDelRequest del = {.cmd = 5, .handle = (uint32_t)v->handle};
        ioctl(v->fd, HID_DEL_DEVICE, &del);
    }
    close(v->fd); v->fd = -1; v->handle = -1;
}
