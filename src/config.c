/* SPDX-License-Identifier: GPL-3.0-only */
#include "input.h"
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const actions[] = {
    "none", "left", "right", "up", "down", "walk", "cross", "circle",
    "square", "triangle", "l1", "r1", "l2", "r2", "l3", "r3", "dpad_up",
    "dpad_down", "dpad_left", "dpad_right", "options", "touchpad", "ps", "create"
};
_Static_assert(sizeof(actions) / sizeof(*actions) == KS_ACTION_COUNT, "action names");
const char *ks_action_name(KsAction a)
{
    return a >= 0 && a < KS_ACTION_COUNT ? actions[a] : "invalid";
}

void ks_config_defaults(KsConfig *c)
{
    memset(c, 0, sizeof(*c));
    c->sensitivity = 1; c->ads_multiplier = 0.6; c->deadzone = 0.08;
    c->smoothing_ms = 12; c->walk_speed = 0.45;
    c->keys[0x1a] = KS_UP; c->keys[0x04] = KS_LEFT; /* W A */
    c->keys[0x16] = KS_DOWN; c->keys[0x07] = KS_RIGHT; /* S D */
    c->keys[0x2c] = KS_CROSS; c->keys[0xe0] = KS_CIRCLE; /* Space Ctrl */
    c->keys[0x15] = KS_SQUARE; c->keys[0x08] = KS_TRIANGLE; /* R E */
    c->keys[0x14] = KS_L1; c->keys[0x09] = KS_R1; /* Q F */
    c->keys[0xe1] = KS_L3; c->keys[0xe2] = KS_WALK; /* Shift Alt */
    c->keys[0x28] = KS_OPTIONS; c->keys[0x2b] = KS_TOUCHPAD;
    c->keys[0x29] = KS_PS;
    c->keys[0x52] = KS_DPAD_UP; c->keys[0x51] = KS_DPAD_DOWN;
    c->keys[0x50] = KS_DPAD_LEFT; c->keys[0x4f] = KS_DPAD_RIGHT;
    c->mouse[0] = KS_R2; c->mouse[1] = KS_L2; c->mouse[2] = KS_R3;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) ++s;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = 0;
    return s;
}

static int key_code(const char *s)
{
    if (strlen(s) == 1 && *s >= 'a' && *s <= 'z') return 4 + *s - 'a';
    if (strlen(s) == 1 && *s >= '1' && *s <= '9') return 0x1e + *s - '1';
    if (!strcmp(s, "0")) return 0x27;
    static const struct { const char *name; int code; } names[] = {
        {"enter",0x28},{"escape",0x29},{"backspace",0x2a},{"tab",0x2b},
        {"space",0x2c},{"minus",0x2d},{"equal",0x2e},{"leftbracket",0x2f},
        {"rightbracket",0x30},{"backslash",0x31},{"semicolon",0x33},
        {"apostrophe",0x34},{"grave",0x35},{"comma",0x36},{"period",0x37},
        {"slash",0x38},{"capslock",0x39},{"insert",0x49},{"home",0x4a},
        {"pageup",0x4b},{"delete",0x4c},{"end",0x4d},{"pagedown",0x4e},
        {"right",0x4f},{"left",0x50},{"down",0x51},{"up",0x52},
        {"leftctrl",0xe0},{"leftshift",0xe1},{"leftalt",0xe2},{"leftmeta",0xe3},
        {"rightctrl",0xe4},{"rightshift",0xe5},{"rightalt",0xe6},{"rightmeta",0xe7},
        {"f1",0x3a},{"f2",0x3b},{"f3",0x3c},{"f4",0x3d},{"f5",0x3e},
        {"f6",0x3f},{"f7",0x40},{"f8",0x41},{"f11",0x44}
    };
    for (size_t i = 0; i < sizeof(names)/sizeof(*names); ++i)
        if (!strcmp(s, names[i].name)) return names[i].code;
    return -1; /* F9/F10/F12 are reserved runtime controls. */
}

static bool apply(KsConfig *c, const char *key, const char *value)
{
    if (!strncmp(key, "bind.", 5)) {
        KsAction action = KS_ACTION_COUNT;
        for (unsigned i = 0; i < KS_ACTION_COUNT; ++i)
            if (!strcmp(value, actions[i])) action = (KsAction)i;
        if (action == KS_ACTION_COUNT) return false;
        const char *name = key + 5;
        if (!strcmp(name, "mouse_left")) c->mouse[0] = action;
        else if (!strcmp(name, "mouse_right")) c->mouse[1] = action;
        else if (!strcmp(name, "mouse_middle")) c->mouse[2] = action;
        else {
            int code = key_code(name);
            if (code < 0) return false;
            c->keys[code] = action;
        }
        return true;
    }
    if (!strcmp(key, "invert_y")) {
        if (strcmp(value, "true") && strcmp(value, "false")) return false;
        c->invert_y = !strcmp(value, "true");
        return true;
    }
    char *end;
    errno = 0;
    double v = strtod(value, &end);
    if (errno || end == value || *end || !(v >= -DBL_MAX && v <= DBL_MAX)) return false;
    if (!strcmp(key, "sensitivity") && v >= 0.01 && v <= 20) c->sensitivity = v;
    else if (!strcmp(key, "ads_multiplier") && v >= 0.01 && v <= 4) c->ads_multiplier = v;
    else if (!strcmp(key, "deadzone") && v >= 0 && v <= 0.5) c->deadzone = v;
    else if (!strcmp(key, "smoothing_ms") && v >= 0 && v <= 100) c->smoothing_ms = v;
    else if (!strcmp(key, "walk_speed") && v >= 0.05 && v <= 1) c->walk_speed = v;
    else return false;
    return true;
}

int ks_config_load(const char *path, KsConfig *out, char *error, size_t capacity)
{
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(error, capacity, "%s: %s", path, strerror(errno)); return -1; }
    KsConfig next;
    ks_config_defaults(&next);
    char line[256];
    unsigned number = 0;
    bool ok = true;
    while (fgets(line, sizeof(line), f)) {
        ++number;
        if (!strchr(line, '\n') && !feof(f)) { ok = false; break; }
        char *comment = strchr(line, '#');
        if (comment) *comment = 0;
        char *key = trim(line);
        if (!*key) continue;
        char *eq = strchr(key, '=');
        if (!eq) { ok = false; break; }
        *eq = 0;
        if (!apply(&next, trim(key), trim(eq + 1))) { ok = false; break; }
    }
    if (ferror(f)) ok = false;
    fclose(f);
    if (!ok) {
        snprintf(error, capacity, "Invalid configuration at line %u", number);
        return -1;
    }
    *out = next;
    if (capacity) *error = 0;
    return 0;
}
