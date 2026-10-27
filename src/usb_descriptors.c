/* SPDX-License-Identifier: GPL-3.0-only */
#include "usb_descriptors.h"
#include <stdbool.h>

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

int ks_boot_interfaces(const uint8_t *data, size_t len, KsBootInterface *out, size_t cap)
{
    if (len < 9 || data[0] < 9 || data[1] != 2) return -1;
    size_t total = le16(data + 2);
    if (total > len || total < 9) return -1;
    int count = 0, iface_index = -1;
    bool eligible = false, have_endpoint = false;
    KsBootInterface current = {0};
    for (size_t off = 0; off < total;) {
        size_t n = data[off];
        if (n < 2 || n > total - off) return -1;
        const uint8_t *d = data + off;
        if (d[1] == 4) {
            if (n < 9) return -1;
            if (d[3] == 0) ++iface_index;
            if (iface_index > 255) return -1;
            current = (KsBootInterface){.index = (uint8_t)iface_index,
                .number = d[2], .protocol = d[7]};
            eligible = iface_index >= 0 && d[3] == 0 && d[5] == 3 && d[6] == 1 &&
                (d[7] == 1 || d[7] == 2);
            have_endpoint = false;
        } else if (d[1] == 5) {
            if (n < 7) return -1;
            if (eligible && !have_endpoint && (d[2] & 0x80) && (d[2] & 0xf) &&
                (d[3] & 3) == 3) {
                current.endpoint = d[2] & 0xf;
                current.packet_size = le16(d + 4) & 0x7ff;
                if (current.packet_size < (current.protocol == 1 ? 8 : 3)) return -1;
                if ((size_t)count == cap) return -1;
                out[count++] = current;
                have_endpoint = true;
            }
        }
        off += n;
    }
    return count;
}
