/* SPDX-License-Identifier: GPL-3.0-only */
#include "usb_input.h"
#include <usb_platform.h>

static int protocol(int fd, uint8_t number, uint16_t value)
{
    struct usb_ctl_request request;
    memset(&request, 0, sizeof(request));
    request.ucr_request.bmRequestType = UT_WRITE_CLASS_INTERFACE;
    request.ucr_request.bRequest = 0x0b; /* HID SET_PROTOCOL */
    USETW(request.ucr_request.wValue, value);
    USETW(request.ucr_request.wIndex, number);
    return ioctl(fd, USB_DO_REQUEST, &request);
}

static void disconnect(KsUsbDevice *d)
{
    if (!d->active) return;
    klog_printf("[KeyboardSense] USB %s interface %u disconnected\n", d->name, d->iface.number);
    close(d->input_fd);
    /* Return the interface to report protocol. FreeBSD has no matching
     * DRIVER_ATTACH ioctl: restoring the PS5's native driver needs a replug. */
    protocol(d->control_fd, d->iface.number, 1);
    int index = d->iface.index;
    ioctl(d->control_fd, USB_RELEASE_INTERFACE, &index);
    close(d->control_fd);
    memset(d, 0, sizeof(*d));
    d->control_fd = d->input_fd = -1;
}

void ks_usb_init(KsUsb *usb)
{
    memset(usb, 0, sizeof(*usb));
    for (unsigned i = 0; i < KS_DEVICE_LIMIT; ++i)
        usb->devices[i].control_fd = usb->devices[i].input_fd = -1;
}

static KsUsbDevice *find(KsUsb *u, const char *name, uint8_t index)
{
    for (unsigned i = 0; i < KS_DEVICE_LIMIT; ++i) {
        KsUsbDevice *d = &u->devices[i];
        if (d->active && d->iface.index == index && !strcmp(d->name, name)) return d;
    }
    return NULL;
}

static void attach(KsUsb *u, const char *name, KsBootInterface iface)
{
    KsUsbDevice *d = NULL;
    for (unsigned i = 0; i < KS_DEVICE_LIMIT; ++i)
        if (!u->devices[i].active) { d = &u->devices[i]; break; }
    if (!d) return;
    char path[80];
    snprintf(path, sizeof(path), "/dev/usb/%s.0", name);
    int cfd = open(path, O_RDWR);
    if (cfd < 0) return;
    int index = iface.index;
    /* Never detach an unrelated interface on composite devices. */
    if (ioctl(cfd, USB_IFACE_DRIVER_ACTIVE, &index) == 0 &&
        ioctl(cfd, USB_IFACE_DRIVER_DETACH, &index) != 0) {
        klog_printf("[KeyboardSense] %s iface %u: detach failed errno=%d\n", name, iface.number, errno);
        close(cfd); return;
    }
    if (ioctl(cfd, USB_CLAIM_INTERFACE, &index) != 0) {
        klog_printf("[KeyboardSense] %s iface %u: claim failed errno=%d\n", name, iface.number, errno);
        close(cfd); return;
    }
    if (protocol(cfd, iface.number, 0) != 0) {
        klog_printf("[KeyboardSense] %s iface %u: SET_PROTOCOL(boot) failed errno=%d\n", name, iface.number, errno);
        ioctl(cfd, USB_RELEASE_INTERFACE, &index);
        close(cfd); return;
    }
    snprintf(path, sizeof(path), "/dev/usb/%s.%u", name, iface.endpoint);
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        klog_printf("[KeyboardSense] %s ep%u: open failed errno=%d\n", name, iface.endpoint, errno);
        protocol(cfd, iface.number, 1);
        ioctl(cfd, USB_RELEASE_INTERFACE, &index);
        close(cfd); return;
    }
    int one = 1;
    if (ioctl(fd, USB_SET_RX_SHORT_XFER, &one) != 0) {
        close(fd); protocol(cfd, iface.number, 1);
        ioctl(cfd, USB_RELEASE_INTERFACE, &index); close(cfd); return;
    }
    memset(d, 0, sizeof(*d));
    d->active = d->seen = true;
    d->control_fd = cfd; d->input_fd = fd; d->iface = iface;
    snprintf(d->name, sizeof(d->name), "%s", name);
    klog_printf("[KeyboardSense] USB %s iface %u: boot %s on ep%u\n",
        name, iface.number, iface.protocol == 1 ? "keyboard" : "mouse", iface.endpoint);
}

bool ks_usb_scan(KsUsb *u)
{
    DIR *dir = opendir("/dev");
    if (!dir) return false;
    for (unsigned i = 0; i < KS_DEVICE_LIMIT; ++i) u->devices[i].seen = false;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "ugen", 4)) continue;
        const char *name = entry->d_name + 4;
        unsigned bus, address; char extra;
        if (sscanf(name, "%u.%u%c", &bus, &address, &extra) != 2 ||
            strlen(name) >= sizeof(u->devices[0].name)) continue;
        /* Reuse an existing control fd, but still enumerate every interface
         * so a previously failed second interface can be retried. */
        int fd = -1;
        for (unsigned i = 0; i < KS_DEVICE_LIMIT; ++i) {
            KsUsbDevice *d = &u->devices[i];
            if (d->active && !strcmp(d->name, name)) { fd = d->control_fd; break; }
        }
        bool borrowed = fd >= 0;
        char path[80];
        snprintf(path, sizeof(path), "/dev/usb/%s.0", name);
        if (!borrowed) fd = open(path, O_RDWR);
        if (fd < 0) continue;
        int config_index;
        uint8_t data[4096];
        struct usb_gen_descriptor desc;
        memset(&desc, 0, sizeof(desc));
        desc.ugd_data = data; desc.ugd_maxlen = sizeof(data);
        if (ioctl(fd, USB_GET_CONFIG, &config_index) != 0 || config_index < 0 || config_index > 255) {
            if (!borrowed) close(fd);
            continue;
        }
        desc.ugd_config_index = (uint8_t)config_index;
        if (ioctl(fd, USB_GET_FULL_DESC, &desc) != 0) {
            if (!borrowed) close(fd);
            continue;
        }
        if (!borrowed) close(fd);
        KsBootInterface interfaces[32];
        int count = ks_boot_interfaces(data, desc.ugd_actlen, interfaces, 32);
        for (int i = 0; i < count; ++i) {
            if (interfaces[i].packet_size > 1024) continue;
            KsUsbDevice *existing = find(u, name, interfaces[i].index);
            if (existing) existing->seen = true;
            else attach(u, name, interfaces[i]);
        }
    }
    closedir(dir);
    bool mouse_lost = false;
    for (unsigned i = 0; i < KS_DEVICE_LIMIT; ++i) {
        if (u->devices[i].active && !u->devices[i].seen) {
            mouse_lost |= u->devices[i].iface.protocol == 2;
            disconnect(&u->devices[i]);
        }
    }
    return mouse_lost;
}

bool ks_usb_poll(KsUsb *u, KsKeyboard *keys, uint8_t *buttons, int *dx, int *dy)
{
    memset(keys, 0, sizeof(*keys));
    *buttons = 0; *dx = *dy = 0;
    bool mouse_lost = false;
    for (unsigned i = 0; i < KS_DEVICE_LIMIT; ++i) {
        KsUsbDevice *d = &u->devices[i];
        if (!d->active) continue;
        KsKeyboard pressed = {0};
        uint8_t mouse_pressed = 0;
        int device_dx = 0, device_dy = 0;
        /* Bound the drain so a 1000-Hz mouse cannot starve other devices,
         * signal handling, or the virtual pad's 250-Hz submission loop. */
        for (unsigned j = 0; j < 32; ++j) {
            uint8_t report[1024];
            ssize_t n = read(d->input_fd, report, d->iface.packet_size);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) break;
            if (n <= 0) {
                mouse_lost |= d->iface.protocol == 2;
                disconnect(d); break;
            }
            if (d->iface.protocol == 1) {
                KsKeyboard old = d->keyboard;
                if (ks_keyboard_report(&d->keyboard, report, (size_t)n)) {
                    for (unsigned k = 0; k < KS_KEY_COUNT; ++k)
                        pressed.keys[k] |= d->keyboard.keys[k] && !old.keys[k];
                } else memset(&pressed, 0, sizeof(pressed));
            }
            else {
                KsMouse mouse;
                if (ks_mouse_report(&mouse, report, (size_t)n)) {
                    mouse_pressed |= mouse.buttons & ~d->mouse_buttons;
                    d->mouse_buttons = mouse.buttons;
                    device_dx += mouse.dx; device_dy += mouse.dy;
                }
            }
        }
        if (!d->active) continue;
        *dx += device_dx; *dy += device_dy;
        for (unsigned k = 0; k < KS_KEY_COUNT; ++k)
            keys->keys[k] |= d->keyboard.keys[k] || pressed.keys[k];
        *buttons |= d->mouse_buttons | mouse_pressed;
    }
    return mouse_lost;
}

void ks_usb_shutdown(KsUsb *u)
{
    for (unsigned i = 0; i < KS_DEVICE_LIMIT; ++i) disconnect(&u->devices[i]);
}
