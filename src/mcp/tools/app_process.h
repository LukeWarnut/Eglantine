// SPDX-FileCopyrightText: Copyright 2026 Vortex727 (FUKKO)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <utility>

#include "core/core.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/kernel.h"

namespace Mcp {

/// Error shown when a tool needs the guest process but it doesn't exist (yet / any more).
inline constexpr const char* kNoApplicationProcess =
    "no application process (still booting or shutting down)";

/// The application process, kept alive (kernel reference) while the tool uses its memory.
/// `ToolContext::GameRunning()` is true before the process exists and while it shuts down, and
/// `System::ApplicationMemory()` dereferences the process unchecked: always go through this.
class AppProcessRef {
public:
    explicit AppProcessRef(Core::System& system) : kernel{system.Kernel()} {
        Kernel::KProcess* candidate = system.ApplicationProcess();
        if (candidate != nullptr && !candidate->IsTerminated() && candidate->Open(kernel)) {
            process = candidate;
        }
    }
    ~AppProcessRef() {
        if (process != nullptr) {
            process->Close(kernel);
        }
    }
    AppProcessRef(const AppProcessRef&) = delete;
    AppProcessRef& operator=(const AppProcessRef&) = delete;
    AppProcessRef(AppProcessRef&& other) noexcept
        : kernel{other.kernel}, process{std::exchange(other.process, nullptr)} {}
    AppProcessRef& operator=(AppProcessRef&&) = delete;

    [[nodiscard]] explicit operator bool() const {
        return process != nullptr;
    }
    [[nodiscard]] Kernel::KProcess* operator->() const {
        return process;
    }
    [[nodiscard]] Kernel::KProcess* Get() const {
        return process;
    }
    [[nodiscard]] Core::Memory::Memory& Memory() const {
        return process->GetMemory();
    }

private:
    Kernel::KernelCore& kernel;
    Kernel::KProcess* process = nullptr;
};

} // namespace Mcp
