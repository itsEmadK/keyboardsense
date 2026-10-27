/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "input.h"
#include "sce_pad.h"
#include "usb_descriptors.h"
#include "vpad.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_reports(void)
{
    KsKeyboard keyboard = {0};
    uint8_t report[8] = {2,0,0x1a,0x07,0,0,0,0};
    assert(ks_keyboard_report(&keyboard, report, 8));
    assert(keyboard.keys[0x1a] && keyboard.keys[0x07] && keyboard.keys[0xe1]);
    assert(!ks_keyboard_report(&keyboard, report, 7));
    assert(keyboard.keys[0x1a]);
    report[2] = 1; /* Rollover must not retain W. */
    assert(!ks_keyboard_report(&keyboard, report, 8));
    for (unsigned i = 0; i < 256; ++i) assert(!keyboard.keys[i]);
    memset(report, 0, sizeof(report));
    assert(ks_keyboard_report(&keyboard, report, 8));
    KsMouse mouse;
    uint8_t movement[] = {0xff,0x80,0x7f,1};
    assert(ks_mouse_report(&mouse, movement, sizeof(movement)));
    assert(mouse.buttons == 7 && mouse.dx == -128 && mouse.dy == 127);
    assert(!ks_mouse_report(&mouse, movement, 2));
}

static void test_mapping(void)
{
    KsConfig c;
    ks_config_defaults(&c);
    KsKeyboard keys = {0};
    KsMapper m = {0};
    KsPad p;
    ks_map(&m, &c, &keys, 0, 0, 0, 1000000, &p);
    assert(p.lx == 128 && p.ly == 128 && p.rx == 128 && p.ry == 128 && !p.buttons);
    keys.keys[0x1a] = keys.keys[0x07] = true;
    ks_map(&m, &c, &keys, 3, 0, 0, 1004000, &p);
    assert(p.lx == 218 && p.ly == 37); /* normalized diagonal */
    assert(p.l2 == 255 && p.r2 == 255);
    assert((p.buttons & (SCE_PAD_BUTTON_L2 | SCE_PAD_BUTTON_R2)) ==
           (SCE_PAD_BUTTON_L2 | SCE_PAD_BUTTON_R2));
    keys.keys[0x04] = keys.keys[0x16] = true;
    ks_map(&m, &c, &keys, 0, 0, 0, 1008000, &p);
    assert(p.lx == 128 && p.ly == 128); /* opposing keys cancel */
    memset(&keys, 0, sizeof(keys));
    keys.keys[0x2c] = keys.keys[0xe1] = keys.keys[0x29] = true;
    ks_map(&m, &c, &keys, 4, 0, 0, 1012000, &p);
    assert(p.buttons == (SCE_PAD_BUTTON_CROSS | SCE_PAD_BUTTON_L3 | SCE_PAD_BUTTON_PS | SCE_PAD_BUTTON_R3));
    memset(&keys, 0, sizeof(keys));
    c.smoothing_ms = 0; c.deadzone = 0;
    ks_mapper_reset(&m);
    ks_map(&m, &c, &keys, 0, 4, -4, 2000000, &p);
    assert(p.rx > 128 && p.ry < 128);
    int hip_x = p.rx;
    ks_mapper_reset(&m);
    ks_map(&m, &c, &keys, 2, 4, 0, 2000000, &p);
    assert(p.rx < hip_x && p.rx > 128); /* ADS sensitivity */
    c.invert_y = true;
    ks_map(&m, &c, &keys, 0, 0, -4, 2004000, &p);
    assert(p.ry > 128);
    ks_map(&m, &c, &keys, 0, 0, 0, 2040000, &p);
    assert(p.rx == 128 && p.ry == 128); /* no drift after stop */
    ks_map(&m, &c, &keys, 0, 100000, 0, 2044000, &p);
    assert(p.rx == 255 && p.ry == 128);
    c.smoothing_ms = 12;
    ks_mapper_reset(&m);
    ks_map(&m, &c, &keys, 0, -2, 1, 3000000, &p);
    assert(p.rx < 128);
    ks_map(&m, &c, &keys, 0, 0, 0, 3032000, &p);
    assert(p.rx == 128 && p.ry == 128);
    keys.keys[0x1a] = keys.keys[0xe2] = true;
    ks_map(&m, &c, &keys, 0, 0, 0, 3036000, &p);
    assert(p.ly > 0 && p.ly < 128); /* walk */
}

static void test_config(void)
{
    char path[] = "/tmp/keyboardsense-test-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *f = fdopen(fd, "w"); assert(f);
    fputs("# example\nsensitivity = 2.5\nbind.w = cross\nbind.leftshift = walk\nbind.mouse_right = none\ninvert_y = true\n", f);
    fclose(f);
    KsConfig c; ks_config_defaults(&c);
    char error[128];
    assert(ks_config_load(path, &c, error, sizeof(error)) == 0);
    assert(c.sensitivity == 2.5 && c.keys[0x1a] == KS_CROSS && c.invert_y);
    assert(c.keys[0xe1] == KS_WALK && c.mouse[1] == KS_NONE);
    KsConfig before;
    memcpy(&before, &c, sizeof(c));
    const char *invalid[] = {"sensitivity=nan", "deadzone=0.9", "bind.f12=cross", "foo=1", "bind.a=bogus", "invert_y=1", "sensitivity=1oops", "sensitivity=", "[profile]"};
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(*invalid); ++i) {
        f = fopen(path, "w"); assert(f);
        fprintf(f, "sensitivity=1\n%s\n", invalid[i]); fclose(f);
        assert(ks_config_load(path, &c, error, sizeof(error)) == -1);
        assert(!memcmp(&before, &c, sizeof(c))); /* atomic reload */
    }
    unlink(path);
    assert(ks_config_load(path, &c, error, sizeof(error)) == -1);
    assert(!memcmp(&before, &c, sizeof(c)));
}

static void test_descriptors(void)
{
    /* Composite receiver: vendor interface 0, keyboard number 3, mouse 7.
     * Interface index is deliberately different from bInterfaceNumber. */
    uint8_t config[] = {
        9,2,57,0,3,1,0,0x80,50,
        9,4,0,0,0,0xff,0,0,0,
        9,4,3,0,1,3,1,1,0, 7,5,0x82,3,8,0,1,
        9,4,7,0,2,3,1,2,0, 7,5,0x03,3,8,0,1, 7,5,0x84,3,8,0,1
    };
    assert(sizeof(config) == 57);
    KsBootInterface interfaces[8];
    int n = ks_boot_interfaces(config, sizeof(config), interfaces, 8);
    assert(n == 2);
    assert(interfaces[0].index == 1 && interfaces[0].number == 3 && interfaces[0].endpoint == 2);
    assert(interfaces[1].index == 2 && interfaces[1].number == 7 && interfaces[1].endpoint == 4);
    assert(interfaces[0].protocol == 1 && interfaces[1].protocol == 2);
    for (size_t len = 0; len < sizeof(config); ++len)
        assert(ks_boot_interfaces(config, len, interfaces, 8) == -1);
    assert(ks_boot_interfaces(config, sizeof(config), interfaces, 1) == -1);
    config[18] = 0;
    assert(ks_boot_interfaces(config, sizeof(config), interfaces, 8) == -1);
    config[18] = 9; config[18 + 6] = 0; /* non-boot keyboard ignored */
    assert(ks_boot_interfaces(config, sizeof(config), interfaces, 8) == 1);
    config[18 + 6] = 1; config[18 + 3] = 1; /* alternate setting ignored */
    assert(ks_boot_interfaces(config, sizeof(config), interfaces, 8) == 1);
    /* Exercise arbitrary hostile/truncated descriptors under sanitizers. */
    uint32_t seed = 1234;
    uint8_t fuzz[128];
    for (unsigned run = 0; run < 20000; ++run) {
        for (unsigned j = 0; j < sizeof(fuzz); ++j) {
            seed = seed * 1664525u + 1013904223u; fuzz[j] = (uint8_t)(seed >> 24);
        }
        fuzz[0] = 9; fuzz[1] = 2; fuzz[2] = sizeof(fuzz); fuzz[3] = 0;
        (void)ks_boot_interfaces(fuzz, sizeof(fuzz), interfaces, 8);
    }
}

int main(void)
{
    test_reports(); test_mapping(); test_config(); test_descriptors();
    puts("All KeyboardSense core tests passed (including 20,000 descriptor fuzz cases).");
    return 0;
}
