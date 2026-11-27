// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Texture handle programs read banks 16 and 17, when unbound, from the root tables the buffer
// cache streams there (Mesa NVK's push constants pick a material record from root table 7).

#include <array>
#include <optional>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/shader_info.h"
#include "video_core/shader_handle_program.h"

namespace {
struct NoMemory {
    template <typename T>
    T Read(GPUVAddr) const {
        return T{};
    }
};
} // Anonymous namespace

TEST_CASE("Texture handle programs read unbound root table banks", "[video_core][textures]") {
    Shader::Info info;
    info.handle_programs.push_back({static_cast<u32>(Shader::HandleOp::Cbuf), 16, 0x44});
    NoMemory memory;
    std::array<u32, 64> table{};
    table[0x44 / 4] = 7;
    const auto unbound = [](u32) -> GPUVAddr { return 0; };
    const auto root_word = [&](u32 index, u32 offset) -> std::optional<u32> {
        if (index != 16) {
            return std::nullopt;
        }
        return table[offset / 4];
    };
    CHECK(VideoCommon::EvaluateTextureHandle(info, 0, memory, unbound, 16, root_word) == 7);
    CHECK(VideoCommon::EvaluateTextureHandle(info, 0, memory, unbound, 16) == 0);

    // The cached result is computed again once a root table word it read changes.
    VideoCommon::TextureHandleCache cache;
    CHECK(cache.Evaluate(info, 0, memory, unbound, 16, root_word) == 7);
    CHECK(cache.Evaluate(info, 0, memory, unbound, 16, root_word) == 7);
    table[0x44 / 4] = 9;
    CHECK(cache.Evaluate(info, 0, memory, unbound, 16, root_word) == 9);
}
