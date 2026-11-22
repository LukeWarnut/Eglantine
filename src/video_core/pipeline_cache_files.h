// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <filesystem>
#include <string_view>

#include "common/common_types.h"

namespace VideoCommon {

// VTX-HLE's disk caches: the NVN pipeline records and the driver's VkPipelineCache data. Bump
// NVN_PIPELINE_CACHE_VERSION when the record format changes (nvn_pipeline_disk.h).
inline constexpr u32 NVN_PIPELINE_CACHE_VERSION = 18;
inline constexpr u32 NVN_DRIVER_CACHE_VERSION = 1;

inline constexpr std::array<char, 8> NVN_PIPELINE_CACHE_MAGIC{'N', 'V', 'N', 'P',
                                                               'I', 'P', 'E', 'L'};
inline constexpr std::array<char, 8> NVN_DRIVER_CACHE_MAGIC{'N', 'V', 'N', 'V',
                                                             'K', 'C', 'H', '1'};
inline constexpr std::string_view NVN_DRIVER_CACHE_FILE_NAME = "nvn_pipelines.vkcache";

inline constexpr std::string_view MKW_EMULATED_CACHE_SUBDIR = "mkw_zero_depth_v1";
inline constexpr std::array<std::string_view, 8> EMULATED_VULKAN_CACHE_FILES{
    "vulkan.bin",        "vulkan_mesh.bin",        "vulkan_used.bin",        "vulkan_pipelines.bin",
    "vulkan.mythos.bin", "vulkan_mesh.mythos.bin", "vulkan_used.mythos.bin",
    "vulkan_pipelines.mythos.bin",
};

[[nodiscard]] constexpr bool IsNvnPipelineCacheFile(std::string_view name) {
    return name == NVN_DRIVER_CACHE_FILE_NAME ||
           (name.starts_with("nvn_pipelines.") && name.ends_with(".bin"));
}

/// Files removed by the title's Remove Vulkan Pipeline Cache action, across both graphics paths.
[[nodiscard]] constexpr bool IsVulkanPipelineCacheFile(std::string_view name) {
    for (const auto legacy : EMULATED_VULKAN_CACHE_FILES) {
        if (name == legacy) {
            return true;
        }
    }
    return IsNvnPipelineCacheFile(name);
}

enum class NvnCacheTransition { Unchanged, Cleared, Failed };

/// An emulated-path cache is evidence of a previous run. Consume it at VTX-HLE startup so
/// subsequent VTX-HLE launches can reuse the cache they built.
NvnCacheTransition ClearShaderCachesForNvn(const std::filesystem::path& title_dir,
                                          bool mario_kart_world);

} // namespace VideoCommon
