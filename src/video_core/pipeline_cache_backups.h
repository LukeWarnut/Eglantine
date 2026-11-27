// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

namespace VideoCommon {

/// Deletes the caches of other versions LoadPipelines set aside for `file` (<file>.v<version>).
void RemovePipelineCacheVersionBackups(const std::filesystem::path& file);

} // namespace VideoCommon
