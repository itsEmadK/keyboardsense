/* SPDX-License-Identifier: GPL-3.0-only */
#include "usb_input.h"
#include <usb_platform.h>
#include <assert.h>
#include <stdbool.h>

static const uint8_t descriptor[] = {
    9,2,57,0,3,1,0,0x80,50,
    9,4,0,0,0,0xff,0,0,0,
    9,4,3,0,1,3,1,1,0, 7,5,0x82,3,8,0,1,
    9,4,7,0,2,3,1,2,0, 7,5,0x03,3,8,0,1, 7,5,0x84,3,8,0,1
};
static bool present = true, enumerated, fail_mouse, reject_mouse_protocol;
static int next_fd = 3, endpoint[100], detach_mask, protocol_mask, restore_mask, opened;
static unsigned mouse_protocol_attempts;
static struct { uint8_t data[8][8]; size_t lengths[8]; unsigned head, tail; } queues[16];

int ks_mock_open(const char *path, int flags, ...)
{
    unsigned ep;
    assert(present && sscanf(path, "/dev/usb/1.2.%u", &ep) == 1);
    assert(ep == 0 || ep == 2 || ep == 4);
    assert(ep ? flags == (O_RDONLY | O_NONBLOCK) : flags == O_RDWR);
    assert(next_fd < 100);
    endpoint[next_fd] = (int)ep; ++opened;
    return next_fd++;
}
int ks_mock_close(int fd)
{
    assert(fd >= 3 && endpoint[fd] >= 0);
    endpoint[fd] = -1; --opened;
    return 0;
}
DIR *ks_mock_opendir(const char *path)
{
    assert(!strcmp(path, "/dev")); enumerated = false;
    return (DIR *)(uintptr_t)1;
}
struct dirent *ks_mock_readdir(DIR *dir)
{
    (void)dir;
    if (!present || enumerated) return NULL;
    static struct dirent entry;
    strcpy(entry.d_name, "ugen1.2"); enumerated = true;
    return &entry;
}
int ks_mock_closedir(DIR *dir) { (void)dir; return 0; }
int klog_printf(const char *fmt, ...) { (void)fmt; return 0; }
int ks_mock_ioctl(int fd, unsigned long command, ...)
{
    assert(endpoint[fd] >= 0);
    va_list args; va_start(args, command);
    void *arg = va_arg(args, void *); va_end(args);
    if (!present) { errno = ENXIO; return -1; }
    switch (command) {
    case USB_GET_CONFIG: *(int *)arg = 0; break;
    case USB_GET_FULL_DESC: {
        struct usb_gen_descriptor *d = arg;
        assert(d->ugd_maxlen >= sizeof(descriptor) && d->ugd_config_index == 0);
        memcpy(d->ugd_data, descriptor, sizeof(descriptor)); d->ugd_actlen = sizeof(descriptor);
        break;
    }
    case USB_IFACE_DRIVER_ACTIVE: case USB_CLAIM_INTERFACE: case USB_RELEASE_INTERFACE:
        assert(*(int *)arg == 1 || *(int *)arg == 2); break;
    case USB_IFACE_DRIVER_DETACH:
        assert(*(int *)arg == 1 || *(int *)arg == 2);
        detach_mask |= 1 << *(int *)arg; break;
    case USB_SET_RX_SHORT_XFER:
        assert(endpoint[fd] == 2 || endpoint[fd] == 4); assert(*(int *)arg == 1); break;
    case USB_DO_REQUEST: {
        struct usb_device_request *r = &((struct usb_ctl_request *)arg)->ucr_request;
        assert(r->bmRequestType == 0x21 && r->bRequest == 0x0b && !r->wLength);
        assert(r->wIndex == 3 || r->wIndex == 7);
        assert(r->wValue <= 1);
        if (!r->wValue && r->wIndex == 7) {
            ++mouse_protocol_attempts;
            if (reject_mouse_protocol) { errno = EIO; return -1; }
        }
        if (!r->wValue) protocol_mask |= 1 << r->wIndex;
        else restore_mask |= 1 << r->wIndex;
        break;
    }
    default: assert(false);
    }
    return 0;
}
ssize_t ks_mock_read(int fd, void *data, size_t maxlen)
{
    int ep = endpoint[fd];
    assert(ep == 2 || ep == 4);
    if (!present || (ep == 4 && fail_mouse)) { errno = EIO; return -1; }
    unsigned head = queues[ep].head;
    if (head == queues[ep].tail) { errno = EAGAIN; return -1; }
    size_t n = queues[ep].lengths[head]; assert(n <= maxlen);
    memcpy(data, queues[ep].data[head], n); ++queues[ep].head;
    return (ssize_t)n;
}
static void queue(int ep, const uint8_t *data, size_t n)
{
    unsigned tail = queues[ep].tail++; assert(tail < 8 && n <= 8);
    memcpy(queues[ep].data[tail], data, n); queues[ep].lengths[tail] = n;
}
static unsigned count(const KsUsb *u)
{
    unsigned n = 0;
    for (unsigned i = 0; i < KS_DEVICE_LIMIT; ++i) n += u->devices[i].active;
    return n;
}

int main(void)
{
    KsUsb usb; ks_usb_init(&usb);
    /* A working first interface must not prevent retrying the second one. */
    reject_mouse_protocol = true;
    ks_usb_scan(&usb);
    assert(count(&usb) == 1 && mouse_protocol_attempts == 1);
    reject_mouse_protocol = false;
    ks_usb_scan(&usb);
    assert(count(&usb) == 2 && mouse_protocol_attempts == 2);
    assert(detach_mask == ((1 << 1) | (1 << 2))); /* never vendor interface 0 */
    assert(protocol_mask == ((1 << 3) | (1 << 7))); /* actual interface numbers */
    ks_usb_scan(&usb); assert(count(&usb) == 2); /* no duplicate attachments */
    uint8_t down[] = {2,0,0x1a,0,0,0,0,0}, up[8] = {0};
    uint8_t click[] = {1,10,0}, release[] = {0,0,0};
    queue(2, down, 8); queue(2, up, 8);
    queue(4, click, 3); queue(4, release, 3);
    KsKeyboard keys; uint8_t buttons; int dx, dy;
    assert(!ks_usb_poll(&usb, &keys, &buttons, &dx, &dy));
    assert(keys.keys[0x1a] && keys.keys[0xe1] && buttons == 1 && dx == 10 && dy == 0);
    /* Press+release between ticks must produce a one-frame pulse. */
    assert(!ks_usb_poll(&usb, &keys, &buttons, &dx, &dy));
    assert(!keys.keys[0x1a] && !keys.keys[0xe1] && !buttons && !dx && !dy);
    queue(2, down, 8); queue(4, click, 3);
    ks_usb_poll(&usb, &keys, &buttons, &dx, &dy);
    assert(keys.keys[0x1a] && buttons == 1);
    fail_mouse = true;
    assert(ks_usb_poll(&usb, &keys, &buttons, &dx, &dy));
    assert(count(&usb) == 1 && !buttons && keys.keys[0x1a]);
    fail_mouse = false; ks_usb_scan(&usb); assert(count(&usb) == 2);
    present = false; assert(ks_usb_scan(&usb)); assert(count(&usb) == 0);
    ks_usb_poll(&usb, &keys, &buttons, &dx, &dy);
    assert(!keys.keys[0x1a] && !buttons && !dx && !dy);
    present = true; ks_usb_scan(&usb); assert(count(&usb) == 2);
    ks_usb_shutdown(&usb);
    assert(count(&usb) == 0 && opened == 0);
    assert(restore_mask == ((1 << 3) | (1 << 7)));
    puts("USB simulation passed: composite attach/retry, short clicks, disconnect/replug, cleanup.");
    return 0;
}
