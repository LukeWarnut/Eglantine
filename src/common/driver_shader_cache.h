// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace Common {

/// Linux: keeps the GPU driver's shader cache for this emulator in a folder of its own, with its
/// own size limit: NVIDIA's proprietary driver in <shader dir>/nvidia, Mesa (RADV, ANV, its OpenGL
/// drivers) in <shader dir>/mesa. The default caches are one folder shared by every application,
/// with one size limit (NVIDIA: 1 GB on many systems, Mesa: 1 GiB) that a game's pipelines alone
/// can exceed, and another application's entries evict ours. Leaves a driver's environment alone
/// when the user set its cache path or turned its cache off. The limit is `size_gib` (the
/// driver_shader_cache_size setting), also over a size in the environment, which is meant for the
/// shared cache. Must run before anything loads a GPU driver; other platforms are unchanged.
/// `enabled` is the private_driver_shader_cache setting; EDEN_NX2_PRIVATE_DRIVER_CACHE=0 also
/// turns it off.
/// Creates no folder: CreateDriverShaderCacheFolder does, after the user data migration.
void ConfigureDriverShaderCache(bool enabled, unsigned size_gib);

/// Creates the folders ConfigureDriverShaderCache chose, or reverts a driver's environment to the
/// shared cache when it can't. Call after the user data migration and before the renderer is
/// created.
void CreateDriverShaderCacheFolder();

/// What ConfigureDriverShaderCache did, for the log (which isn't running yet when it is called).
[[nodiscard]] std::string DriverShaderCacheDescription();

} // namespace Common
