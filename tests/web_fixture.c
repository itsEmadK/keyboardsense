/* SPDX-License-Identifier: GPL-3.0-only */
#include "web.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    KsWebSnapshot s = {0}; KsMapper m = {0};
    uint32_t seed = 42;
    uint64_t now = 1000000;
    const unsigned usages[] = {4,7,8,9,20,21,22,26,40,41,43,44,79,80,81,82,224,225,226,18,19,13,14};
    for (unsigned frame = 0; frame < 1000; ++frame) {
        bool reset = frame % 100 == 0;
        if (reset) {
            ks_config_defaults(&s.config); ks_mapper_reset(&m);
            s.config.smoothing_ms = (frame / 100) % 2 ? 0 : 12;
            s.config.invert_y = (frame / 100) % 3 == 1;
            s.config.sensitivity = 0.25 + (frame / 100) * 0.5;
            s.config.keys[18]=KS_RS_UP;s.config.keys[19]=KS_RS_DOWN;
            s.config.keys[13]=KS_RS_LEFT;s.config.keys[14]=KS_RS_RIGHT;
        }
        memset(&s.keys, 0, sizeof(s.keys));
        for (unsigned i = 0; i < sizeof(usages)/sizeof(*usages); ++i) {
            seed = seed * 1664525u + 1013904223u;
            s.keys.keys[usages[i]] = (seed >> 29) == 0;
        }
        seed = seed * 1664525u + 1013904223u;
        s.mouse_buttons = (seed >> 20) & 7;
        s.mouse_dx = frame % 4 ? (int)((seed >> 16) % 81) - 40 : 0;
        s.mouse_dy = frame % 4 ? (int)((seed >> 8) % 81) - 40 : 0;
        now += 4000 + (seed % 3) * 4000;
        if (frame % 19 == 0) now += 40000;
        s.uptime_us = now; s.sequence = frame; s.enabled = s.submit_ok = true;
        ks_map(&m, &s.config, &s.keys, s.mouse_buttons, s.mouse_dx, s.mouse_dy, now, &s.pad);
        char json[8192];
        int size = ks_web_json(&s, true, json, sizeof(json)); assert(size > 0);
        printf("{\"reset\":%s,\"now\":%llu,\"state\":%s}\n", reset ? "true" : "false", (unsigned long long)now, json);
    }
    /* Exercise every key and maximum device count, including JSON escaping. */
    for (unsigned i = 0; i < KS_KEY_COUNT; ++i) s.keys.keys[i] = s.pulse_keys.keys[i] = true;
    s.device_count = KS_DEVICE_LIMIT;
    for (unsigned i = 0; i < s.device_count; ++i) {
        strcpy(s.devices[i].name, "a\"\\\nb"); s.devices[i].protocol = 1;
    }
    char json[8192]; assert(ks_web_json(&s, false, json, sizeof(json)) > 0);
    assert(ks_web_json(&s, false, json, 1) == -1);
    assert(ks_web_json(&s, false, json, 0) == -1);
    assert(ks_web_json(&s, false, json, sizeof(json)) > 0);
    printf("{\"serialization\":%s}\n", json);
    return 0;
}
