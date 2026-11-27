// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <optional>
#include <span>
#include <vector>
#include "common/common_types.h"

namespace Shader::Ampere {
/// Lowered native programs: in [begin, end), registers low_group * 4 + 0..3 stand for
/// high_group * 4 + 0..3, in register bank 1 for the components set in bank_mask (bank 0 for the
/// others). Lowering uses it instead of copying those registers in and out of a free low quartet
/// around each instruction that uses the high ones. begin and end are lowered instruction
/// indices in LoweredProgram and code addresses once laid out (NativeRegisterRemaps).
struct NativeRegisterRemap {
    u32 begin{}, end{};
    u32 low_group{}, high_group{}, bank_mask{};
};

constexpr u64 NativeAddressMapMagic = 0x50414d4e4544454eULL;
constexpr u64 NativeRegisterRemapMagic = 0x50414d5245474552ULL;

/// The code address of lowered instruction `index`: after the 0x50 byte header, with a
/// scheduling word before every third instruction.
constexpr u32 LoweredInstructionAddress(u32 index) {
    return 0x50U + index * 8 + ((index + 1) / 3) * 8;
}

/// Appends the remap table, before the address map (which must stay last).
inline void AppendNativeRegisterRemaps(std::vector<u64>& code,
                                       std::span<const NativeRegisterRemap> remaps) {
    if (remaps.empty()) return;
    for (const NativeRegisterRemap& remap : remaps) {
        code.push_back(u64{LoweredInstructionAddress(remap.begin) / 8} |
                       (u64{LoweredInstructionAddress(remap.end) / 8} << 24) |
                       (u64{remap.low_group} << 48) | (u64{remap.high_group} << 54) |
                       (u64{remap.bank_mask} << 60));
    }
    code.push_back(remaps.size());
    code.push_back(NativeRegisterRemapMagic);
}

inline bool HasNativeAddressMap(std::span<const u64> code) {
    return code.size() >= 2 && code.back() == NativeAddressMapMagic;
}
inline void AppendNativeAddressMap(std::vector<u64>& code, std::span<const u32> offsets) {
    if (offsets.empty()) return;
    for (u32 index : offsets) code.push_back(LoweredInstructionAddress(index));
    code.push_back(offsets.size());
    code.push_back(NativeAddressMapMagic);
}
inline std::optional<u32> NativeProgramAddress(std::span<const u64> code, u32 address) {
    if (!HasNativeAddressMap(code) || address % 16) return {};
    const u64 count = code[code.size() - 2];
    if (!count || count > 65536 || count + 2 > code.size() || address / 16 >= count) return {};
    const size_t start = code.size() - static_cast<size_t>(count) - 2;
    const u64 target = code[start + address / 16];
    if (target < 0x50 || target >= start * 8 || target % 8 || target % 32 == 0) return {};
    return static_cast<u32>(target);
}
/// The remap table of laid out lowered code, in code addresses and ordered by `begin`.
inline std::vector<NativeRegisterRemap> NativeRegisterRemaps(std::span<const u64> code) {
    if (!HasNativeAddressMap(code)) return {};
    const u64 map_count = code[code.size() - 2];
    if (map_count + 4 > code.size()) return {};
    const size_t map_start = code.size() - static_cast<size_t>(map_count) - 2;
    if (code[map_start - 1] != NativeRegisterRemapMagic) return {};
    const u64 count = code[map_start - 2];
    if (count + 2 > map_start) return {};
    std::vector<NativeRegisterRemap> remaps;
    remaps.reserve(static_cast<size_t>(count));
    for (size_t index = map_start - 2 - static_cast<size_t>(count); index < map_start - 2; ++index) {
        const u64 word = code[index];
        remaps.push_back({
            .begin = static_cast<u32>(word & 0xffffff) * 8,
            .end = static_cast<u32>((word >> 24) & 0xffffff) * 8,
            .low_group = static_cast<u32>((word >> 48) & 63),
            .high_group = static_cast<u32>((word >> 54) & 63),
            .bank_mask = static_cast<u32>(word >> 60),
        });
    }
    return remaps;
}
} // namespace Shader::Ampere
