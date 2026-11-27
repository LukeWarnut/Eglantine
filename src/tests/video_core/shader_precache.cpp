// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include "shader_recompiler/exception.h"
#include "video_core/pipeline_cache_backups.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/shader_environment.h"
#include "video_core/shader_warm/shader_archives.h"
#include "video_core/shader_warm/shader_warm_builder.h"
#include "video_core/shader_warm/warm_records.h"

namespace {

namespace ShaderWarm = VideoCommon::ShaderWarm;

struct TempDir {
    std::filesystem::path path;
    TempDir() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("eden-shader-precache-" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void WriteBytes(const std::filesystem::path& file, std::string_view bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

} // Anonymous namespace

TEST_CASE("ShaderPrecache: the marker matches only the same file and driver", "[video_core]") {
    TempDir dir;
    const auto warm = dir.path / "warm-compute.bin";
    REQUIRE_FALSE(ShaderWarm::CurrentWarmStamp(warm, 0, 1, 2, 3).has_value());
    REQUIRE_FALSE(ShaderWarm::ReadWarmMarker(warm).has_value());

    WriteBytes(warm, "precache");
    const auto stamp = ShaderWarm::CurrentWarmStamp(warm, 12, 0x1234, 0x2206, 53);
    REQUIRE(stamp.has_value());
    REQUIRE(stamp->size == 8);
    REQUIRE(ShaderWarm::WriteWarmMarker(warm, *stamp));
    REQUIRE(ShaderWarm::WarmMarkerPath(warm) == dir.path / "warm-compute.done");

    const auto marker = ShaderWarm::ReadWarmMarker(warm);
    REQUIRE(marker.has_value());
    REQUIRE(marker->entries == 12);
    // The entry count is informative: a stamp taken before reading the file still matches.
    REQUIRE(marker->Matches(*ShaderWarm::CurrentWarmStamp(warm, 0, 0x1234, 0x2206, 53)));
    REQUIRE_FALSE(marker->Matches(*ShaderWarm::CurrentWarmStamp(warm, 12, 0x1235, 0x2206, 53)));
    REQUIRE_FALSE(marker->Matches(*ShaderWarm::CurrentWarmStamp(warm, 12, 0x1234, 0x2207, 53)));
    REQUIRE_FALSE(marker->Matches(*ShaderWarm::CurrentWarmStamp(warm, 12, 0x1234, 0x2206, 54)));

    WriteBytes(warm, "a rebuilt precache");
    REQUIRE_FALSE(marker->Matches(*ShaderWarm::CurrentWarmStamp(warm, 12, 0x1234, 0x2206, 53)));
}

TEST_CASE("ShaderPrecache: the entry count belongs to a file of one size", "[video_core]") {
    TempDir dir;
    const auto warm = dir.path / "warm-compute.bin";
    WriteBytes(warm, "0123456789");
    REQUIRE(ShaderWarm::ReadWarmCount(warm) == 0);

    WriteBytes(ShaderWarm::WarmCountPath(warm), "345 10\n");
    REQUIRE(ShaderWarm::ReadWarmCount(warm) == 345);

    WriteBytes(warm, "01234567890");
    REQUIRE(ShaderWarm::ReadWarmCount(warm) == 0);
}

TEST_CASE("ShaderPrecache: a compute entry reads back like a pipeline cache entry",
          "[video_core]") {
    ShaderWarm::LoweredCode lowered;
    lowered.code.resize(16 * sizeof(u64));
    for (size_t i = 0; i < lowered.code.size(); ++i) {
        lowered.code[i] = static_cast<u8>(i * 7 + 1);
    }
    lowered.texture_buffer = 3;
    const ShaderWarm::ComputeControl control{{64, 2, 1}, 1024, 256};
    ShaderWarm::ComputeKey key_bytes{};
    const auto entry = ShaderWarm::ComputeEntry(lowered, control, key_bytes);

    TempDir dir;
    const auto file = dir.path / "entry.bin";
    WriteBytes(file, std::string_view{reinterpret_cast<const char*>(entry.data()), entry.size()});

    std::ifstream in(file, std::ios::binary);
    in.exceptions(std::ifstream::failbit);
    u32 num_envs{};
    in.read(reinterpret_cast<char*>(&num_envs), sizeof(num_envs));
    REQUIRE(num_envs == 1);
    VideoCommon::FileEnvironment env;
    env.Deserialize(in);
    Vulkan::ComputePipelineCacheKey key{};
    static_assert(sizeof(key) == sizeof(key_bytes));
    in.read(reinterpret_cast<char*>(&key), sizeof(key));
    REQUIRE(in.tellg() == static_cast<std::streamoff>(entry.size()));

    REQUIRE(env.ShaderStage() == Shader::Stage::Compute);
    REQUIRE(env.WorkgroupSize() == control.workgroup);
    REQUIRE(env.SharedMemorySize() == control.shared_memory);
    REQUIRE(env.LocalMemorySize() == control.local_memory);
    REQUIRE(env.TextureBoundBuffer() == 3);
    REQUIRE(std::memcmp(&key, key_bytes.data(), sizeof(key)) == 0);
    REQUIRE(key.unique_hash == ShaderWarm::StageHash(lowered.code));
    REQUIRE(key.shared_memory_size == control.shared_memory);
    REQUIRE(key.workgroup_size == control.workgroup);

    // The entry has no recorded reads: without guesses they are errors, with them the guesses.
    REQUIRE_THROWS_AS(env.ReadCbufValue(0, 0x40), Shader::LogicError);
    static const VideoCommon::GuessMissing guess{};
    env.SetGuessMissing(&guess);
    REQUIRE(env.ReadCbufValue(0, 0x40) == guess.cbuf_bank0);
    REQUIRE(env.ReadCbufValue(3, 0x40) == guess.cbuf_other);
    REQUIRE(env.ReadTextureType(5) == guess.texture_type);
    REQUIRE(env.IsCbufStorageBufferPointer(0, 0x10) == guess.storage_pointer_bank0);
    REQUIRE(env.IsCbufStorageBufferPointer(1, 0x10) == guess.storage_pointer_other);
}

TEST_CASE("ShaderPrecache: a program naming no texture bank reads bank 0", "[video_core]") {
    ShaderWarm::LoweredCode lowered;
    lowered.code.resize(16 * sizeof(u64));
    ShaderWarm::ComputeKey key{};
    const auto entry = ShaderWarm::ComputeEntry(lowered, {{1, 1, 1}, 0, 0}, key);
    u32 texture_bound{};
    // num_envs, then six u64 counts and local memory before the texture bank.
    std::memcpy(&texture_bound, entry.data() + 4 + 6 * 8 + 4, sizeof(texture_bound));
    REQUIRE(texture_bound == 0);
}

TEST_CASE("ShaderPrecache: a file from an older builder or cache version is stale",
          "[video_core]") {
    TempDir dir;
    const auto warm = dir.path / "warm-compute.bin";
    REQUIRE(ShaderWarm::WarmFilesStale(warm).has_value());

    std::string header = "yuzucach";
    const u32 version = ShaderWarm::WarmCacheVersion;
    header.append(reinterpret_cast<const char*>(&version), sizeof(version));
    WriteBytes(warm, header);
    const auto count = ShaderWarm::WarmCountPath(warm);
    // Count files written before the builder version was recorded have two fields.
    WriteBytes(count, "0 12\n");
    REQUIRE(ShaderWarm::WarmFilesStale(warm).has_value());
    WriteBytes(count, fmt::format("0 12 {}\n", ShaderWarm::WarmBuilderVersion - 1));
    REQUIRE(ShaderWarm::WarmFilesStale(warm).has_value());
    WriteBytes(count, fmt::format("0 12 {}\n", ShaderWarm::WarmBuilderVersion));
    REQUIRE_FALSE(ShaderWarm::WarmFilesStale(warm).has_value());

    WriteBytes(count, fmt::format("0 13 {}\n", ShaderWarm::WarmBuilderVersion));
    REQUIRE(ShaderWarm::WarmFilesStale(warm).has_value());
    const u32 old_version = ShaderWarm::WarmCacheVersion - 1;
    std::string old_header = "yuzucach";
    old_header.append(reinterpret_cast<const char*>(&old_version), sizeof(old_version));
    WriteBytes(warm, old_header);
    WriteBytes(count, fmt::format("0 12 {}\n", ShaderWarm::WarmBuilderVersion));
    REQUIRE(ShaderWarm::WarmFilesStale(warm).has_value());
}

TEST_CASE("ShaderPrecache: every container the archive walker reads is discovered",
          "[video_core]") {
    using ShaderWarm::ClassifyGameFile;
    using ShaderWarm::GameFileKind;
    for (const char* path : {"Shader/Model.product.nin_ounce_nvn2.bfsha.zs",
                             "Shader/Model.bfsha", "Pack/Shaders.nin_ounce_nvn2.release.sarc.zs",
                             "Pack/Shaders.release.sarc", "Shader/Loose.bnsh",
                             "Shader/Loose.bnsh.zs", "Shader/Bundle.cgsb", "Shader/Bundle.cgsb.zs",
                             "Shader/Program.shn", "Shader/Program.shn.zs", "Shader/Archive.sharcb",
                             "Shader/Archive.sharcb.zs", "Shader/UPPER.BNSH", "Effect/Fx.bfsha.zs"}) {
        INFO(path);
        REQUIRE(ClassifyGameFile(path) == GameFileKind::ShaderArchive);
    }
    REQUIRE(ClassifyGameFile("Effect/Fx.nin_ounce_nvn2.vsetb.byml.zs") == GameFileKind::Effect);
    REQUIRE(ClassifyGameFile("Effect/static.Nin_Ounce_NVN2.esetb.byml.zs") == GameFileKind::Effect);
    REQUIRE(ClassifyGameFile("Effect/Particle/Magic.ptcl.zst") == GameFileKind::Effect);
    REQUIRE(ClassifyGameFile("Effect/Particle/Magic.ptcl") == GameFileKind::Effect);
    REQUIRE(ClassifyGameFile("Pack/ZsDic.pack.zs") == GameFileKind::ZstdDictionaries);
    for (const char* path :
         {"Effect/Sub/Fx.vsetb.byml.zs", "Model/Fx.vsetb.byml.zs", "Effect/Fx.vsetb.byml",
          "Effect/Sub/Fx.esetb.byml.zs",
          "Model/Tex.bntx.zs", "Model/Mesh.bfres.zs", "Asset/assets_a000.dat",
          "Bin/Nvn2-upper_left.ndsr", "Shader/notes.shn.txt", "Shader/bnsh"}) {
        INFO(path);
        REQUIRE(ClassifyGameFile(path) == GameFileKind::Other);
    }
}

TEST_CASE("ShaderPrecache: a particle effect without shader programs is not an error",
          "[video_core]") {
    std::vector<u8> effect(64, 0);
    std::memcpy(effect.data(), "VFXB", 4);
    size_t records = 0;
    size_t errors = 0;
    REQUIRE_NOTHROW(ShaderWarm::WalkArchive(
        effect, "Magic.ptcl", nullptr, [&](const ShaderWarm::ShaderRecord&) { ++records; },
        [&](const std::string&) { ++errors; }, {}));
    REQUIRE(records == 0);
    REQUIRE(errors == 0);
}

TEST_CASE("ShaderPrecache: programs that cannot be lowered are refused", "[video_core]") {
    ShaderWarm::LoweredCode lowered;
    REQUIRE(ShaderWarm::LowerProgram({}, lowered).has_value());
    const std::array<u8, 15> partial{};
    REQUIRE(ShaderWarm::LowerProgram(partial, lowered).has_value());
    // One instruction with no exit: not a complete program.
    const std::array<u8, 16> no_exit{};
    REQUIRE(ShaderWarm::LowerProgram(no_exit, lowered).has_value());
    REQUIRE(lowered.code.empty());
}

TEST_CASE("PipelineCacheFile: a damaged last record keeps the records before it",
          "[video_core]") {
    ShaderWarm::LoweredCode lowered;
    lowered.code.resize(16 * sizeof(u64));
    const ShaderWarm::ComputeControl control{{64, 1, 1}, 0, 0};
    std::string records;
    for (u8 i = 0; i < 3; ++i) {
        lowered.code[0] = i;
        ShaderWarm::ComputeKey key_bytes{};
        const auto entry = ShaderWarm::ComputeEntry(lowered, control, key_bytes);
        records.append(reinterpret_cast<const char*>(entry.data()), entry.size());
    }
    constexpr u32 version = 7;
    std::string header{"yuzucach"};
    header.append(reinterpret_cast<const char*>(&version), sizeof(version));
    const size_t record_size = records.size() / 3;

    TempDir dir;
    const auto file = dir.path / "vulkan.bin";
    // A crash in the middle of an append leaves half of the third record.
    WriteBytes(file, header + records.substr(0, record_size * 2 + record_size / 2));

    size_t loaded = 0;
    VideoCommon::LoadPipelines(
        {}, file, version,
        [&](std::ifstream& in, VideoCommon::FileEnvironment) {
            Vulkan::ComputePipelineCacheKey key{};
            in.read(reinterpret_cast<char*>(&key), sizeof(key));
            ++loaded;
        },
        [](std::ifstream&, std::vector<VideoCommon::FileEnvironment>) {});
    REQUIRE(loaded == 2);
    REQUIRE(std::filesystem::exists(file));
    REQUIRE(std::filesystem::file_size(file) == header.size() + record_size * 2);

    loaded = 0;
    VideoCommon::LoadPipelines(
        {}, file, version,
        [&](std::ifstream& in, VideoCommon::FileEnvironment) {
            Vulkan::ComputePipelineCacheKey key{};
            in.read(reinterpret_cast<char*>(&key), sizeof(key));
            ++loaded;
        },
        [](std::ifstream&, std::vector<VideoCommon::FileEnvironment>) {});
    REQUIRE(loaded == 2);
}

TEST_CASE("PipelineCacheFile: a build gets its own cache back after another version ran",
          "[video_core]") {
    ShaderWarm::LoweredCode lowered;
    lowered.code.resize(16 * sizeof(u64));
    const ShaderWarm::ComputeControl control{{64, 1, 1}, 0, 0};
    const auto cache = [&](u32 version, u8 records) {
        std::string bytes{"yuzucach"};
        bytes.append(reinterpret_cast<const char*>(&version), sizeof(version));
        for (u8 i = 0; i < records; ++i) {
            lowered.code[0] = i;
            ShaderWarm::ComputeKey key_bytes{};
            const auto entry = ShaderWarm::ComputeEntry(lowered, control, key_bytes);
            bytes.append(reinterpret_cast<const char*>(entry.data()), entry.size());
        }
        return bytes;
    };
    TempDir dir;
    const auto file = dir.path / "vulkan.bin";
    const auto backup = [&](u32 version) {
        auto path{file};
        path += fmt::format(".v{}", version);
        return path;
    };
    const auto load = [&](u32 version, VideoCommon::CacheUpgrade* upgrade = nullptr) {
        size_t loaded = 0;
        VideoCommon::LoadPipelines(
            {}, file, version,
            [&](std::ifstream& in, VideoCommon::FileEnvironment) {
                Vulkan::ComputePipelineCacheKey key{};
                in.read(reinterpret_cast<char*>(&key), sizeof(key));
                ++loaded;
            },
            [](std::ifstream&, std::vector<VideoCommon::FileEnvironment>) {}, false, 1, upgrade);
        return loaded;
    };

    // A newer build sets the older cache aside and starts empty.
    WriteBytes(file, cache(7, 2));
    REQUIRE(load(8) == 0);
    REQUIRE_FALSE(std::filesystem::exists(file));
    REQUIRE(std::filesystem::exists(backup(7)));

    // The older build takes it back.
    REQUIRE(load(7) == 2);
    REQUIRE_FALSE(std::filesystem::exists(backup(7)));

    // The newer build finds the older cache in place and its own set aside: it takes its own
    // instead of upgrading the older one.
    WriteBytes(backup(8), cache(8, 3));
    VideoCommon::CacheUpgrade upgrade;
    REQUIRE(load(8, &upgrade) == 3);
    REQUIRE(upgrade.from_version == 0);
    REQUIRE(std::filesystem::exists(backup(7)));
    REQUIRE_FALSE(std::filesystem::exists(backup(8)));

    // Only the two newest set-aside caches are kept.
    WriteBytes(backup(3), cache(3, 1));
    WriteBytes(backup(4), cache(4, 1));
    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(backup(3), now - std::chrono::hours{3});
    std::filesystem::last_write_time(backup(4), now - std::chrono::hours{2});
    std::filesystem::last_write_time(backup(7), now - std::chrono::hours{1});
    REQUIRE(load(9) == 0);
    REQUIRE(std::filesystem::exists(backup(8)));
    REQUIRE(std::filesystem::exists(backup(7)));
    REQUIRE_FALSE(std::filesystem::exists(backup(4)));
    REQUIRE_FALSE(std::filesystem::exists(backup(3)));

    VideoCommon::RemovePipelineCacheVersionBackups(file);
    REQUIRE_FALSE(std::filesystem::exists(backup(8)));
    REQUIRE_FALSE(std::filesystem::exists(backup(7)));
}
