/* SPDX-License-Identifier: GPL-3.0-only */
#include "input.h"
#include "sce_pad.h"
#include <math.h>
#include <string.h>

bool ks_keyboard_report(KsKeyboard *state, const uint8_t *r, size_t n)
{
    if (n != 8) return false;
    KsKeyboard next = {0};
    for (unsigned i = 2; i < 8; ++i) {
        if (r[i] > 0 && r[i] <= 3) {
            memset(state, 0, sizeof(*state));
            return false;
        }
        if (r[i]) next.keys[r[i]] = true;
    }
    for (unsigned i = 0; i < 8; ++i)
        next.keys[0xe0 + i] = (r[0] & (1u << i)) != 0;
    *state = next;
    return true;
}

bool ks_mouse_report(KsMouse *state, const uint8_t *r, size_t n)
{
    /* Boot protocol specifies three bytes. Ignore optional extensions (e.g.
     * wheel); never interpret them as report IDs or 16-bit coordinates. */
    if (n < 3) return false;
    state->buttons = r[0] & 7u;
    state->dx = (int8_t)r[1];
    state->dy = (int8_t)r[2];
    return true;
}

void ks_pad_neutral(KsPad *pad)
{
    memset(pad, 0, sizeof(*pad));
    pad->lx = pad->ly = pad->rx = pad->ry = 128;
}

void ks_mapper_reset(KsMapper *m) { memset(m, 0, sizeof(*m)); }

static uint8_t axis(double value)
{
    value = fmax(-1.0, fmin(1.0, value));
    return (uint8_t)lround(128.0 + value * (value < 0 ? 128.0 : 127.0));
}

void ks_map(KsMapper *m, const KsConfig *c, const KsKeyboard *kb,
            uint8_t mouse_buttons, int dx, int dy, uint64_t now, KsPad *pad)
{
    bool a[KS_ACTION_COUNT] = {false};
    for (unsigned i = 0; i < KS_KEY_COUNT; ++i)
        if (kb->keys[i]) a[c->keys[i]] = true;
    for (unsigned i = 0; i < KS_MOUSE_BUTTONS; ++i)
        if (mouse_buttons & (1u << i)) a[c->mouse[i]] = true;
    ks_pad_neutral(pad);
    double x = (double)a[KS_RIGHT] - a[KS_LEFT];
    double y = (double)a[KS_DOWN] - a[KS_UP];
    double length = hypot(x, y);
    if (length > 1) { x /= length; y /= length; }
    if (a[KS_WALK]) { x *= c->walk_speed; y *= c->walk_speed; }
    pad->lx = axis(x); pad->ly = axis(y);

    static const uint32_t bits[KS_ACTION_COUNT] = {
        [KS_CROSS] = SCE_PAD_BUTTON_CROSS, [KS_CIRCLE] = SCE_PAD_BUTTON_CIRCLE,
        [KS_SQUARE] = SCE_PAD_BUTTON_SQUARE, [KS_TRIANGLE] = SCE_PAD_BUTTON_TRIANGLE,
        [KS_L1] = SCE_PAD_BUTTON_L1, [KS_R1] = SCE_PAD_BUTTON_R1,
        [KS_L2] = SCE_PAD_BUTTON_L2, [KS_R2] = SCE_PAD_BUTTON_R2,
        [KS_L3] = SCE_PAD_BUTTON_L3, [KS_R3] = SCE_PAD_BUTTON_R3,
        [KS_DPAD_UP] = SCE_PAD_BUTTON_UP, [KS_DPAD_DOWN] = SCE_PAD_BUTTON_DOWN,
        [KS_DPAD_LEFT] = SCE_PAD_BUTTON_LEFT, [KS_DPAD_RIGHT] = SCE_PAD_BUTTON_RIGHT,
        [KS_OPTIONS] = SCE_PAD_BUTTON_OPTIONS, [KS_TOUCHPAD] = SCE_PAD_BUTTON_TOUCH_PAD,
        [KS_PS] = SCE_PAD_BUTTON_PS, [KS_CREATE] = SCE_PAD_BUTTON_CREATE
    };
    for (unsigned i = 1; i < KS_ACTION_COUNT; ++i)
        if (a[i]) pad->buttons |= bits[i];
    pad->l2 = a[KS_L2] ? 255 : 0;
    pad->r2 = a[KS_R2] ? 255 : 0;

    double dt = m->last_tick_us && now > m->last_tick_us ?
        (now - m->last_tick_us) / 1000000.0 : 0.004;
    dt = fmax(0.001, fmin(0.1, dt));
    m->last_tick_us = now;
    if (dx || dy) { m->last_motion_us = now; m->has_motion = true; }
    /* Full deflection at 2000 counts/s at sensitivity 1; deliberately no
     * accumulating cursor position: stopping the mouse recenters the stick. */
    double scale = c->sensitivity * (a[KS_L2] ? c->ads_multiplier : 1) / (2000 * dt);
    double target_x = dx * scale, target_y = dy * scale * (c->invert_y ? -1 : 1);
    double target_length = hypot(target_x, target_y);
    if (target_length > 1) { target_x /= target_length; target_y /= target_length; }
    double alpha = c->smoothing_ms > 0 ? 1 - exp(-dt * 1000 / c->smoothing_ms) : 1;
    m->filtered_x += alpha * (target_x - m->filtered_x);
    m->filtered_y += alpha * (target_y - m->filtered_y);
    if (!m->has_motion || now - m->last_motion_us >= 32000) {
        m->filtered_x = m->filtered_y = 0;
    }
    x = m->filtered_x; y = m->filtered_y;
    length = hypot(x, y);
    if (length > 0.0001) {
        double magnitude = c->deadzone + fmin(1, length) * (1 - c->deadzone);
        x = x / length * magnitude; y = y / length * magnitude;
    } else { x = y = 0; }
    x += (double)a[KS_RS_RIGHT] - a[KS_RS_LEFT];
    y += (double)a[KS_RS_DOWN] - a[KS_RS_UP];
    length = hypot(x,y);
    if(length>1){x/=length;y/=length;}
    pad->rx = axis(x); pad->ry = axis(y);
}
