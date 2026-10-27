/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KS_GAME_H
#define KS_GAME_H
#include "editor.h"
#include <sys/types.h>
typedef struct { pid_t pid; char title[KS_TITLE_SIZE]; } KsGame;
/* 1: current running big app, 0: none, -1: discovery failed. */
int ks_game_query(KsGame *);
#endif
