/*
 * Common FPS for PS5
 * Copyright (C) 2026 porhe911
 *
 * KeyboardSense modifications (2026-10-27): sidebar integration and runtime paths.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

extern "C" {
#include "editor.h"
}
#include <cstdarg>

namespace common_fps::ps5::shellui {

/* Persist the last reached startup stage even when /data logging fails. */
void record_stage(const char* stage) noexcept;

/* Resolve the already-loaded ShellUI Mono runtime and PUI assemblies. */
bool initialize_runtime();

/* Bind the loopback UDP receiver used by the controller. */
bool initialize_receiver();

/* Remain blocked on IPC for the complete SceShellUI process lifetime. */
[[noreturn]] void run_receiver_loop();

} // namespace common_fps::ps5::shellui
