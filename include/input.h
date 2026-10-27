/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KS_INPUT_H
#define KS_INPUT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KS_DEVICE_LIMIT 8
#define KS_KEY_COUNT 256
#define KS_MOUSE_BUTTONS 3

typedef enum {
    KS_NONE, KS_LEFT, KS_RIGHT, KS_UP, KS_DOWN, KS_WALK,
    KS_CROSS, KS_CIRCLE, KS_SQUARE, KS_TRIANGLE,
    KS_L1, KS_R1, KS_L2, KS_R2, KS_L3, KS_R3,
    KS_DPAD_UP, KS_DPAD_DOWN, KS_DPAD_LEFT, KS_DPAD_RIGHT,
    KS_OPTIONS, KS_TOUCHPAD, KS_PS, KS_CREATE, KS_ACTION_COUNT
} KsAction;

typedef struct {
    KsAction keys[KS_KEY_COUNT];
    KsAction mouse[KS_MOUSE_BUTTONS];
    double sensitivity, ads_multiplier, deadzone, smoothing_ms, walk_speed;
    bool invert_y;
} KsConfig;

typedef struct { bool keys[KS_KEY_COUNT]; } KsKeyboard;
typedef struct { uint8_t buttons; int dx, dy; } KsMouse;
typedef struct {
    uint32_t buttons;
    uint8_t lx, ly, rx, ry, l2, r2;
} KsPad;
typedef struct {
    double filtered_x, filtered_y;
    uint64_t last_motion_us, last_tick_us;
    bool has_motion;
} KsMapper;

/* Invalid reports are rejected. Keyboard rollover clears keys to avoid a
 * stuck movement key; a later valid report restores the current state. */
bool ks_keyboard_report(KsKeyboard *, const uint8_t *, size_t);
bool ks_mouse_report(KsMouse *, const uint8_t *, size_t);
void ks_pad_neutral(KsPad *);
void ks_mapper_reset(KsMapper *);
void ks_map(KsMapper *, const KsConfig *, const KsKeyboard *, uint8_t mouse_buttons,
            int dx, int dy, uint64_t now_us, KsPad *);
void ks_config_defaults(KsConfig *);
/* All-or-nothing configuration: on any error, out is left unchanged. */
int ks_config_load(const char *path, KsConfig *out, char *error, size_t error_size);
const char *ks_action_name(KsAction);
#endif
