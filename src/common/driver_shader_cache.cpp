// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <system_error>
#include <utility>

#include <fmt/format.h>

#include "common/common_types.h"
#include "common/driver_shader_cache.h"
#include "common/fs/path_util.h"

namespace Common {
namespace {

std::string description{"driver shader cache settings unchanged"};
std::string mesa_description;

#if defined(__linux__) && !defined(__ANDROID__)
/// A driver's cache pointed at a private folder, until CreateDriverShaderCacheFolder creates it.
struct PendingCache {
    const char* name;
    const char* path_variable;
    const char* size_variable;
    std::filesystem::path folder;
    /// The size variable it replaced, restored when the folder can't be created.
    std::optional<std::string> replaced_size;
};
PendingCache nvidia{
    "NVIDIA driver", "__GL_SHADER_DISK_CACHE_PATH", "__GL_SHADER_DISK_CACHE_SIZE", {}, {}};
PendingCache mesa{"Mesa", "MESA_SHADER_CACHE_DIR", "MESA_SHADER_CACHE_MAX_SIZE", {}, {}};

bool IsFalse(const char* value) {
    return value != nullptr && (value[0] == '0' || value[0] == 'f' || value[0] == 'F' ||
                                value[0] == 'n' || value[0] == 'N');
}

bool IsTrue(const char* value) {
    return value != nullptr && (value[0] == '1' || value[0] == 't' || value[0] == 'T' ||
                                value[0] == 'y' || value[0] == 'Y');
}

// Mesa (RADV, ANV, its OpenGL drivers) keeps one shader cache folder for every application, 1 GiB
// by default. Same folder rule as NVIDIA's: the user's own path or a disabled cache wins.
void ConfigureMesaShaderCache(unsigned size_gib) {
    for (const char* variable : {"MESA_SHADER_CACHE_DIR", "MESA_GLSL_CACHE_DIR"}) {
        if (const char* path = std::getenv(variable)) {
            mesa_description =
                fmt::format("Mesa shader cache in {} (set in the environment)", path);
            return;
        }
    }
    if (IsTrue(std::getenv("MESA_SHADER_CACHE_DISABLE")) ||
        IsTrue(std::getenv("MESA_GLSL_CACHE_DISABLE"))) {
        mesa_description = "Mesa shader cache turned off by the user";
        return;
    }
    const auto path = Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir) / "mesa";
    setenv(mesa.path_variable, path.c_str(), 1);
    mesa.folder = path;
    std::string notes;
    if (const char* const environment_size = std::getenv(mesa.size_variable)) {
        mesa.replaced_size = environment_size;
        notes = fmt::format(" (instead of {}={})", mesa.size_variable, environment_size);
    }
    setenv(mesa.size_variable, fmt::format("{}G", size_gib).c_str(), 1);
    mesa_description =
        fmt::format("Mesa shader cache in {}, limit {} GiB{}", path.string(), size_gib, notes);
}

void CreateFolder(PendingCache& cache, std::string& cache_description) {
    if (cache.folder.empty()) {
        return;
    }
    const auto path = std::exchange(cache.folder, {});
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (!error) {
        return;
    }
    cache_description = fmt::format("{} shader cache left shared: can't create {}: {}", cache.name,
                                    path.string(), error.message());
    unsetenv(cache.path_variable);
    if (cache.replaced_size) {
        setenv(cache.size_variable, cache.replaced_size->c_str(), 1);
    } else {
        unsetenv(cache.size_variable);
    }
}
#endif

} // Anonymous namespace

void ConfigureDriverShaderCache([[maybe_unused]] bool enabled, [[maybe_unused]] unsigned size_gib) {
#if defined(__linux__) && !defined(__ANDROID__)
    if (!enabled) {
        description = "Driver shader caches left shared (turned off in the settings)";
        return;
    }
    if (IsFalse(std::getenv("EDEN_NX2_PRIVATE_DRIVER_CACHE"))) {
        description = "Driver shader caches left shared (EDEN_NX2_PRIVATE_DRIVER_CACHE=0)";
        return;
    }
    // Which driver loads isn't known yet; each reads only its own variables.
    ConfigureMesaShaderCache(size_gib);
    if (const char* path = std::getenv("__GL_SHADER_DISK_CACHE_PATH")) {
        description =
            fmt::format("NVIDIA driver shader cache in {} (set in the environment)", path);
        return;
    }
    if (IsFalse(std::getenv("__GL_SHADER_DISK_CACHE"))) {
        description = "NVIDIA driver shader cache turned off by the user";
        return;
    }
    // The folder is created later (CreateDriverShaderCacheFolder): creating it now would make
    // the user data migration find a cache folder in the way.
    const auto path = Common::FS::GetEdenPath(Common::FS::EdenPath::ShaderDir) / "nvidia";
    setenv("__GL_SHADER_DISK_CACHE_PATH", path.c_str(), 1);
    nvidia.folder = path;
    // A size in the environment is the user's limit for the shared cache, not for this folder.
    std::string notes;
    if (const char* const environment_size = std::getenv("__GL_SHADER_DISK_CACHE_SIZE")) {
        nvidia.replaced_size = environment_size;
        notes = fmt::format(" (instead of __GL_SHADER_DISK_CACHE_SIZE={})", environment_size);
    }
    const std::string size = std::to_string(u64{size_gib} << 30);
    setenv("__GL_SHADER_DISK_CACHE_SIZE", size.c_str(), 1);
    // Steam sets this for its own per-game caches; here it would turn the limit off.
    if (std::getenv("__GL_SHADER_DISK_CACHE_SKIP_CLEANUP") != nullptr) {
        unsetenv("__GL_SHADER_DISK_CACHE_SKIP_CLEANUP");
        notes += ", __GL_SHADER_DISK_CACHE_SKIP_CLEANUP ignored";
    }
    description = fmt::format("NVIDIA driver shader cache in {}, limit {} GiB{}", path.string(),
                              size_gib, notes);
#endif
}

void CreateDriverShaderCacheFolder() {
#if defined(__linux__) && !defined(__ANDROID__)
    CreateFolder(nvidia, description);
    CreateFolder(mesa, mesa_description);
#endif
}

std::string DriverShaderCacheDescription() {
    return mesa_description.empty() ? description
                                    : fmt::format("{}; {}", description, mesa_description);
}

} // namespace Common
