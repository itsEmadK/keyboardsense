/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KS_EDITOR_H
#define KS_EDITOR_H
#include "input.h"
#define KS_TITLE_SIZE 16
#define KS_SIDEBAR_ROWS 14
#define KS_SIDEBAR_MAGIC 0x4b534244u
#define KS_SIDEBAR_VERSION 1u
#define KS_SIDEBAR_PORT 1406

typedef struct {
    uint32_t magic, version;
    uint64_t sequence;
    uint32_t visible, tab, row_count, selected_row, cursor_x, cursor_y;
    char title[KS_TITLE_SIZE], scope[32], heading[64], status[128];
    char rows[KS_SIDEBAR_ROWS][80];
    char footer[160];
} KsSidebarFrame;
typedef struct { uint32_t magic, version; uint64_t sequence; uint32_t visible, active; } KsSidebarAck;

#if defined(__cplusplus)
static_assert(sizeof(KsSidebarFrame)==1560 && sizeof(KsSidebarAck)==24,"sidebar IPC ABI");
#else
_Static_assert(sizeof(KsSidebarFrame)==1560 && sizeof(KsSidebarAck)==24,"sidebar IPC ABI");
#endif

typedef struct {
    KsConfig active, global, draft;
    char root[256], title[KS_TITLE_SIZE], status[128];
    uint64_t revision;
    bool game_profile, open, release_gate, input_first, capture_armed, shortcut_down;
    unsigned tab, row, capture, input; /* inputs 1..255 = HID; 256..258 = mouse */
    KsAction target;
    KsKeyboard previous;
    uint8_t previous_mouse;
    uint64_t repeat_at;
    int cursor_x, cursor_y;
} KsEditor;

int ks_editor_init(KsEditor *, const char *root);
bool ks_title_valid(const char *);
/* Select the current game's profile, discarding an editor for an old game. */
bool ks_editor_game(KsEditor *, const char *title);
int ks_editor_reload(KsEditor *);
/* revision + title are compared again by the owning input thread. */
int ks_editor_commit(KsEditor *, const KsConfig *, uint64_t revision, const char *title, unsigned save);
/* May consume raw inputs for the sidebar/release gate. Renderer readiness is
 * required before opening on the TV; a missing renderer never traps input. */
bool ks_editor_tick(KsEditor *, const KsKeyboard *, uint8_t mouse, int dx, int dy,
                    uint64_t now, bool renderer_ready);
void ks_editor_frame(const KsEditor *, uint64_t sequence, KsSidebarFrame *);
bool ks_sidebar_frame_valid(const KsSidebarFrame *, size_t);
#endif
