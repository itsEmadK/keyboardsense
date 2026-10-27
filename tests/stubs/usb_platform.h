/* SPDX-License-Identifier: GPL-3.0-only
 * Host simulation of USB operations. Tests logic, NOT Sony's ABI. */
#ifndef KS_USB_PLATFORM_H
#define KS_USB_PLATFORM_H
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
struct usb_device_request {
    uint8_t bmRequestType, bRequest;
    uint16_t wValue, wIndex, wLength;
};
struct usb_ctl_request { struct usb_device_request ucr_request; };
struct usb_gen_descriptor {
    void *ugd_data;
    uint16_t ugd_maxlen, ugd_actlen;
    uint8_t ugd_config_index;
};
#define USETW(field, value) ((field) = (value))
#define UT_WRITE_CLASS_INTERFACE 0x21
/* Fake operation numbers keep these tests independent of OS ioctl headers. */
enum { USB_DO_REQUEST = 1, USB_RELEASE_INTERFACE, USB_IFACE_DRIVER_ACTIVE,
    USB_IFACE_DRIVER_DETACH, USB_CLAIM_INTERFACE, USB_SET_RX_SHORT_XFER,
    USB_GET_CONFIG, USB_GET_FULL_DESC };
int ks_mock_open(const char *, int, ...);
int ks_mock_close(int);
ssize_t ks_mock_read(int, void *, size_t);
int ks_mock_ioctl(int, unsigned long, ...);
DIR *ks_mock_opendir(const char *);
struct dirent *ks_mock_readdir(DIR *);
int ks_mock_closedir(DIR *);
int klog_printf(const char *, ...);
#define open ks_mock_open
#define close ks_mock_close
#define read ks_mock_read
#define ioctl ks_mock_ioctl
#define opendir ks_mock_opendir
#define readdir ks_mock_readdir
#define closedir ks_mock_closedir
#endif
