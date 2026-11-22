// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>

#include "common/common_types.h"

namespace Core {
class System;
}

namespace Kernel {
class KProcess;
}

namespace Service::RO {

void LoopProcess(Core::System& system);

/// Adds to the process's handle table an ldr:ro session bound to it, as the one ounce-hbloader
/// passes to the NROs it runs (kaihen's JIT commands go through it). Returns 0 on failure.
u32 OpenHomebrewSession(Core::System& system, Kernel::KProcess& process);

/// Unmaps the JIT code the process mapped through ldr:ro, as ounce-hbloader does when an NRO
/// returns. Returns how many mappings there were.
size_t ReleaseHomebrewJitCode(Kernel::KProcess& process);

} // namespace Service::RO
