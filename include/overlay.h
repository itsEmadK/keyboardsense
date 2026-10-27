/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KS_OVERLAY_H
#define KS_OVERLAY_H
#include "editor.h"
#include <sys/types.h>
int ks_overlay_start(void);
void ks_overlay_game(pid_t);
bool ks_overlay_ready(uint64_t now_us);
void ks_overlay_publish(const KsSidebarFrame *,uint64_t now_us);
void ks_overlay_stop(void);
#endif
