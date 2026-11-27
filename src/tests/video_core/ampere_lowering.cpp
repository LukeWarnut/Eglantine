// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstdlib>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/ampere/program_capture.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
using Shader::Ampere::ProgramCaptureState;

constexpr Instruction EXIT{{0x0000794d, 0, 0x03800000, 0}};

// Lowered Maxwell words whose top bits (above `shift`) are `opcode`.
template <typename Program>
size_t CountMaxwell(const Program& lowered, u64 opcode, u32 shift) {
    return static_cast<size_t>(std::count_if(lowered.instructions.begin(),
                                             lowered.instructions.end(),
                                             [&](u64 word) { return (word >> shift) == opcode; }));
}

template <typename Program>
u64 FindMaxwell(const Program& lowered, u64 opcode, u32 shift) {
    const auto it = std::find_if(lowered.instructions.begin(), lowered.instructions.end(),
                                 [&](u64 word) { return (word >> shift) == opcode; });
    return it != lowered.instructions.end() ? *it : 0;
}

TEST_CASE("Ampere BRA.CONV always branches and BRA.DIV never does", "[video_core][ampere]") {
    // BRA.CONV UR6 / BRA.DIV ~URZ to the second EXIT: the mode is in the offset's low two bits.
    const std::array<Instruction, 3> convergent{{
        {{0x06007947, 0x10 | 3, 0x0b800000, 0}},
        EXIT,
        EXIT,
    }};
    ProgramCaptureState capture;
    REQUIRE(capture.Observe(convergent[0], 0) == ProgramCaptureState::Result::Continue);
    const auto taken = LowerToMaxwell(convergent);
    REQUIRE(static_cast<bool>(taken));
    REQUIRE(CountMaxwell(taken, 0xe24, 52) == 1);

    const std::array<Instruction, 3> divergent{{
        {{0xff007947, 0x10 | 2, 0x0b800000, 0}},
        EXIT,
        EXIT,
    }};
    const auto dropped = LowerToMaxwell(divergent);
    REQUIRE(static_cast<bool>(dropped));
    REQUIRE(CountMaxwell(dropped, 0xe24, 52) == 0);
}

TEST_CASE("Ampere TLD.SCR.LZ takes its handle from a uniform register", "[video_core][ampere]") {
    // TLD.SCR.LZ R5, R8, R4, R5, UR4, 0x0, 2D, 0x7 -> Maxwell TLD.B.
    const std::array<Instruction, 2> code{{
        {{0x04087f66, 0x30000405, 0x089e0705, 0x001f4200}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    const u64 tld = FindMaxwell(lowered, 0xdd, 56);
    REQUIRE(tld != 0);
    REQUIRE(((tld >> 28) & 7) == 2); // 2D
    REQUIRE(((tld >> 31) & 0xf) == 7);
}

TEST_CASE("Ampere TLD.LL without the scalar bit lowers like the scalar form", "[video_core][ampere]") {
    // NAK (Touhou 10's pause capture blit): TLD.LL R6, R2, R4, R0, 0x0, 1, 2D, 0xf. Bit 60 is clear
    // because the level makes a second source list: x, y in R4, R5, the level in R0.
    const std::array<Instruction, 2> code{{
        {{0x04067b66, 0x20400000, 0x059e0f02, 0x00be0400}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    const u64 tlds = FindMaxwell(lowered, 0xda, 56);
    REQUIRE(tlds != 0);
    REQUIRE(((tlds >> 8) & 0xff) == 4);  // x, y
    REQUIRE(((tlds >> 20) & 0xff) == 0); // level
    REQUIRE(((tlds >> 53) & 0xf) == 5);  // 2D.LL

    // 2D at level zero has nothing after the coordinates: without the bit, x and y sit together
    // in Ra rather than split over Ra and Rb, which this lowering doesn't read.
    const std::array<Instruction, 2> unsplit{{
        {{0x04067b66, 0x20400000, 0x009e0f02, 0x00be0400}},
        EXIT,
    }};
    REQUIRE_FALSE(static_cast<bool>(LowerToMaxwell(unsplit)));
}

TEST_CASE("Ampere I2F converts signed 16-bit sources", "[video_core][ampere]") {
    // I2F.S16 R8, R8
    const std::array<Instruction, 2> code{{
        {{0x00087306, 0x00000008, 0x00101400, 0x000fe200}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    const u64 i2f = FindMaxwell(lowered, 0x5cb, 52);
    REQUIRE(((i2f >> 10) & 3) == 1); // 16-bit source
    REQUIRE(((i2f >> 13) & 1) == 1); // signed
}

TEST_CASE("Ampere integer-to-float conversions take immediates", "[video_core][ampere]") {
    // I2F.U32.RP R7, 0x20; I2FP.F32.U32.RZ R4, 0x100; WARPSYNC R7
    const std::array<Instruction, 4> code{{
        {{0x00077906, 0x00000020, 0x00209000, 0x000e2200}},
        {{0x00047845, 0x00000100, 0x0020d000, 0x000fe200}},
        {{0x00007348, 0x00000007, 0x03800000, 0x000fe800}},
        EXIT,
    }};
    REQUIRE(static_cast<bool>(LowerToMaxwell(code)));
}

TEST_CASE("Ampere SUST.P keeps its channel mask", "[video_core][ampere]") {
    // SUST.P.2D.STRONG.SM.R.IGN [R6], R4, 0x0, 0x59
    const std::array<Instruction, 2> code{{
        {{0x06007b9b, 0x60005904, 0x0010a100, 0x000fe200}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(((FindMaxwell(lowered, 0xeb2, 52) >> 20) & 0xf) == 1);
}

TEST_CASE("Ampere SHF.R.U64 immediate shifts keep their amount", "[video_core][ampere]") {
    // SHF.R.U64 R14, R14, n, R17 (Ocarina vertex shader 1a55fa57a335b963). The Maxwell
    // translator reads the immediate from bits 20-38, so no max-shift bits may be set there.
    for (const u32 shift : {0U, 1U, 21U, 31U, 32U}) {
        const std::array<Instruction, 2> code{{
            {{0x0e0e7819, shift, 0x00001211, 0x020fe200}},
            EXIT,
        }};
        const auto lowered = LowerToMaxwell(code);
        REQUIRE(static_cast<bool>(lowered));
        const u64 shf = FindMaxwell(lowered, 0x38f8, 48);
        REQUIRE(shf != 0);
        CHECK(((shf >> 20) & 0x7ffff) == shift);
        CHECK(((shf >> 39) & 0xff) == 17);
    }
}

TEST_CASE("Ampere SHFL with a register lane keeps its mode", "[video_core][ampere]") {
    // SHFL.DOWN PT, R7, R0, R5, 0x1f
    const std::array<Instruction, 2> code{{
        {{0x00077589, 0x08001f05, 0x000e0000, 0x000fe200}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(((FindMaxwell(lowered, 0xef1, 52) >> 30) & 3) == 2);
}

TEST_CASE("Ampere calls find a return address set ahead of them", "[video_core][ampere]") {
    // MOV R9, 0x30; MOV R4, 0x5; CALL.REL.NOINC; EXIT; RET.REL.NODEC R9
    std::array<Instruction, 5> code{{
        {{0x00097802, 0x30, 0x00000f00, 0}},
        {{0x00047802, 0x5, 0x00000f00, 0}},
        {{0x00007944, 0x10, 0x03c00000, 0}},
        EXIT,
        {{0x09007950, 0xffffffb0, 0x03c3ffff, 0}},
    }};
    REQUIRE(static_cast<bool>(LowerToMaxwell(code)));

    // The link register is written again between its MOV and the call.
    code[1] = {{0x00097802, 0x5, 0x00000f00, 0}};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(!static_cast<bool>(lowered));
    REQUIRE(lowered.error == "native call linkage mode");
}

TEST_CASE("Ampere programs go on past an EXIT that a branch jumps over", "[video_core][ampere]") {
    // A call, then a branch over an EXIT to more of the program, with another call there.
    const std::array<Instruction, 8> code{{
        {{0x00047802, 0x20, 0x00000f00, 0}},
        {{0x00007944, 0x50, 0x03c00000, 0}},
        {{0x00007947, 0x10, 0x03800000, 0}},
        EXIT,
        {{0x00047802, 0x60, 0x00000f00, 0}},
        {{0x00007944, 0x10, 0x03c00000, 0}},
        EXIT,
        {{0x04007950, 0xffffff80, 0x03c3ffff, 0}},
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.source_size == sizeof(code));
}

TEST_CASE("Ampere capture ends at the return of a helper called past the EXIT",
          "[video_core][ampere]") {
    // Ocarina 69a40579971fb867: MOV R0, 0x20; CALL.REL.NOINC; EXIT; RET.REL.NODEC R0, then the
    // next program in memory.
    const std::array<Instruction, 4> code{{
        {{0x00007802, 0x20, 0x00000f00, 0}},
        {{0x00007944, 0x10, 0x03c00000, 0}},
        EXIT,
        {{0x00007950, 0xffffffc0, 0x03c3ffff, 0}},
    }};
    ProgramCaptureState capture;
    for (u32 index = 0; index + 1 < code.size(); ++index)
        CHECK(capture.Observe(code[index], index) == ProgramCaptureState::Result::Continue);
    CHECK(capture.Observe(code.back(), 3) == ProgramCaptureState::Result::Complete);
    REQUIRE(static_cast<bool>(LowerToMaxwell(code)));

    ProgramCaptureState jump;
    CHECK(jump.Observe({{0x00007944, 0x10, 0x03c00000, 0}}, 0) == ProgramCaptureState::Result::Continue);
    CHECK(jump.Observe(EXIT, 1) == ProgramCaptureState::Result::Continue);
    CHECK(jump.Observe(EXIT, 2) == ProgramCaptureState::Result::Complete);
}

TEST_CASE("Ampere uniform select and bit operations reach every uniform register",
          "[video_core][ampere]") {
    // Ocarina e8decc3e5c0c184e: @UP0 USEL UR4, UR4, 0x20, UP1. UP0 and UP1 come from R239 into
    // P6 and P5; the SEL runs under P6 and the guest predicates are restored.
    const std::array<Instruction, 2> select{{
        {{0x04040887, 0x20, 0x08800000, 0x000fe200}},
        EXIT,
    }};
    const auto selected = LowerToMaxwell(select);
    INFO(selected.error);
    REQUIRE(static_cast<bool>(selected));
    const std::array<u64, 5> expected_select{
        0x010000000207f0fcULL, 0x38e800000607fffdULL, 0x38f000000607ef00ULL,
        0x5ca002800fc6dcdcULL, 0x38f000000607fd00ULL};
    CHECK(std::search(selected.instructions.begin(), selected.instructions.end(),
                      expected_select.begin(), expected_select.end()) != selected.instructions.end());

    // Ocarina 2079791ecb288b83: UBREV UR4, UR17.
    const std::array<Instruction, 2> reverse{{
        {{0x000472be, 0x11, 0x08000000, 0x000fc800}},
        EXIT,
    }};
    const auto reversed = LowerToMaxwell(reverse);
    INFO(reversed.error);
    REQUIRE(static_cast<bool>(reversed));
    CHECK(reversed.instructions.front() == 0x382801800007e9dcULL);

    // Ocarina 2079791ecb288b83: UFLO.U32.SH UR21, UR21.
    const std::array<Instruction, 2> leading{{
        {{0x001572bd, 0x15, 0x080e0400, 0x000fe400}},
        EXIT,
    }};
    const auto found = LowerToMaxwell(leading);
    INFO(found.error);
    REQUIRE(static_cast<bool>(found));
    CHECK(found.instructions.front() == 0x5c3002000ed700edULL);

    // Ocarina 2079791ecb288b83: UPOPC UR10, UR17.
    const std::array<Instruction, 2> count{{
        {{0x000a72bf, 0x11, 0x08000000, 0x000fe200}},
        EXIT,
    }};
    const auto counted = LowerToMaxwell(count);
    INFO(counted.error);
    REQUIRE(static_cast<bool>(counted));
    CHECK(counted.instructions.front() == 0x5c0800000e9700e2ULL);
}

TEST_CASE("Ampere guarded uniform comparisons keep the output when the guard is false",
          "[video_core][ampere]") {
    // Ocarina 2079791ecb288b83: @UP4 UISETP.GT.U32.AND UP0, UPT, UR21, 0x1f, UPT. UP4 and the old
    // UP0 load into P2 and P6, so a false guard merges UP0 back unchanged.
    const std::array<Instruction, 2> code{{
        {{0x1500488c, 0x1f, 0x0bf04070, 0x000fe200}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    INFO(lowered.error);
    REQUIRE(static_cast<bool>(lowered));
    const std::array<u64, 6> expected{
        0x0100000001f7f0fcULL, 0x38e800000447fffdULL, 0x38f000000447ef00ULL,
        0x5b6803800fc2ed37ULL, 0x38e800000407efefULL, 0x38f000000447fd00ULL};
    CHECK(std::equal(expected.begin(), expected.end(), lowered.instructions.begin()));

    // SEL R3, R4, UR7, P0
    const std::array<Instruction, 2> select{{
        {{0x04037c07, 0x7, 0x08000000, 0x000fe400}},
        EXIT,
    }};
    const auto selected = LowerToMaxwell(select);
    INFO(selected.error);
    REQUIRE(static_cast<bool>(selected));
    CHECK(selected.instructions.front() == 0x5ca000000df70403ULL);
}

TEST_CASE("Ampere gathers take arrays, per-pixel offsets and depth references",
          "[video_core][ampere]") {
    const auto lower = [](const Instruction& instruction) {
        const std::array<Instruction, 2> code{{instruction, EXIT}};
        return LowerToMaxwell(code);
    };
    const auto field = [](u64 word, u32 offset, u32 count) {
        return (word >> offset) & ((u64{1} << count) - 1);
    };
    // Ocarina: TLD4.R.2D.ARRAY R44, R42, R8, R4. Layer and x in R8, R9, y in R4.
    const auto array = lower({{0x082c7b63, 0xb0801004, 0x001e0f2a, 0x00014200}});
    INFO(array.error);
    REQUIRE(static_cast<bool>(array));
    REQUIRE(array.instructions.size() >= 4);
    CHECK(array.instructions[0] == 0x5c980780008700faULL);
    CHECK(array.instructions[1] == 0x5c980780009700fbULL);
    CHECK(array.instructions[2] == 0x5c980780004700fcULL);
    CHECK(field(array.instructions[3], 20, 8) == 255);
    CHECK(field(array.instructions[3], 28, 3) == 3);
    CHECK(field(array.instructions[3], 50, 1) == 0);
    CHECK(field(array.instructions[3], 54, 2) == 0);

    // Ocarina: @!P1 TLD4.R.PTP R24, R14, R6, R16: x, y in R6, R7, the offsets in R16, R17.
    const auto per_pixel = lower({{0x06189b63, 0x30007c10, 0x001e2f0e, 0x000f4600}});
    INFO(per_pixel.error);
    REQUIRE(static_cast<bool>(per_pixel));
    REQUIRE(per_pixel.instructions.size() >= 5);
    CHECK(field(per_pixel.instructions[2], 20, 8) == 16);
    CHECK(field(per_pixel.instructions[3], 20, 8) == 17);
    CHECK(field(per_pixel.instructions[4], 20, 8) == 246);
    CHECK(field(per_pixel.instructions[4], 28, 3) == 2);
    CHECK(field(per_pixel.instructions[4], 54, 2) == 2);

    // Ocarina: TLD4.R.DC R12, R14, R24, R28: x, y in R24, R25, the reference in R28.
    const auto depth = lower({{0x180c7b63, 0x30800e1c, 0x001e4f0e, 0x000f4200}});
    INFO(depth.error);
    REQUIRE(static_cast<bool>(depth));
    REQUIRE(depth.instructions.size() >= 4);
    CHECK(field(depth.instructions[2], 20, 8) == 28);
    CHECK(field(depth.instructions[3], 50, 1) == 1);
    CHECK(field(depth.instructions[3], 54, 2) == 0);

    // Offset mode 3 is not an offset form.
    CHECK_FALSE(lower({{0x082c7b63, 0xb0801004, 0x001e3f2a, 0x00014200}}));

    // A gather from a second descriptor buffer loads the handle from c[binding][offset] into R246
    // and emits Maxwell TLD4.B (0xdec0...) with meta_reg = 246 and any offsets/dref in R247+.
    const Instruction depth_gather{{0x180c7b63, 0x30800e1c, 0x001e4f0e, 0x000f4200}}; // binding 2
    const Instruction plain_b14{{0x040a7b63, 0x33800408, 0x041e0f08, 0x100fc200}};    // binding 14
    auto aoffi_b14 = plain_b14;
    aoffi_b14.words[2] |= 1U << 12;
    const auto mixed_plain = LowerToMaxwell(std::array{depth_gather, plain_b14, EXIT});
    INFO(mixed_plain.error);
    REQUIRE(static_cast<bool>(mixed_plain));
    CHECK(mixed_plain.required_texture_buffer == 2);
    const u64 tld4b_plain = FindMaxwell(mixed_plain, 0xde, 56);
    REQUIRE(tld4b_plain != 0);
    CHECK(field(tld4b_plain, 20, 8) == 246);
    CHECK(field(tld4b_plain, 36, 2) == 0);

    const auto mixed_aoffi = LowerToMaxwell(std::array{depth_gather, aoffi_b14, EXIT});
    INFO(mixed_aoffi.error);
    REQUIRE(static_cast<bool>(mixed_aoffi));
    CHECK(mixed_aoffi.required_texture_buffer == 2);
    const u64 tld4b_aoffi = FindMaxwell(mixed_aoffi, 0xde, 56);
    REQUIRE(tld4b_aoffi != 0);
    CHECK(field(tld4b_aoffi, 20, 8) == 246);
    CHECK(field(tld4b_aoffi, 36, 2) == 1);
}

TEST_CASE("Ampere Ocarina sign extends, uniform shifts and carries, LEA.HI and SURED forms",
          "[video_core][ampere]") {
    const auto lower = [](const Instruction& instruction) {
        const std::array<Instruction, 2> code{{instruction, EXIT}};
        return LowerToMaxwell(code);
    };
    const auto field = [](u64 word, u32 offset, u32 count) {
        return (word >> offset) & ((u64{1} << count) - 1);
    };
    // SGXT R32, R32, 0x1: signed, so BFE.S32 (bit 48).
    const auto extend = lower({{0x2020781a, 1, 0x00000200, 0x000fe400}});
    INFO(extend.error);
    REQUIRE(static_cast<bool>(extend));
    CHECK(extend.instructions.front() == 0x3801000010072020ULL);

    // USHF.L.U32.HI UR5, URZ, UR4, 0x1: 1 << UR4 into UR5 (R221).
    const auto shift = lower({{0x3f057499, 1, 0x08010e04, 0x000fe400}});
    INFO(shift.error);
    REQUIRE(static_cast<bool>(shift));
    REQUIRE(shift.instructions.size() >= 2);
    CHECK(shift.instructions[1] == 0x5c4800800dc7faddULL);

    // UIADD3 UR6, UP2, UR15, UR6, URZ: the carry goes through P4 into R239 bit 4.
    const auto carry = lower({{0x0f067290, 6, 0x0ff5e03f, 0x000fe200}});
    INFO(carry.error);
    REQUIRE(static_cast<bool>(carry));
    const std::array<u64, 6> expected_carry{
        0x5c1000000de7e7faULL, 0x38e800000107fffdULL, 0x5b6203800e77fa27ULL,
        0x38e800000107efefULL, 0x38f000000107fd00ULL, 0x5c9807800fa700deULL};
    CHECK(std::equal(expected_carry.begin(), expected_carry.end(), carry.instructions.begin()));

    // LEA.HI R1, R1, -R0, RZ, 0x1e: the final add negates R0.
    const auto lea = lower({{0x01017211, 0x80000000, 0x078ff0ff, 0x000fca00}});
    INFO(lea.error);
    REQUIRE(static_cast<bool>(lea));
    REQUIRE(lea.instructions.size() >= 2);
    CHECK(lea.instructions[1] == 0x5c1100000007fa01ULL);

    // SURED.OR: lowered to SUATOM (result in RZ) with op 6 at Maxwell bits 29-32.
    const auto reduce = lower({{0x0400159f, 0x60008400, 0x0310e000, 0x0001e200}});
    INFO(reduce.error);
    REQUIRE(static_cast<bool>(reduce));
    CHECK(field(reduce.instructions.front(), 52, 12) == 0xea0);
    CHECK(field(reduce.instructions.front(), 29, 4) == 6);

    // LDG.E.128 R216, [R216.64] in a program using uniform registers: R216-R219 are reserved,
    // so the load goes through a borrowed low quartet.
    const std::array<Instruction, 3> load{{
        {{0x00007c02, 0, 0x08000f00, 0}},
        {{0xd8d87381, 0, 0x001ead00, 0x000f2800}},
        EXIT,
    }};
    const auto loaded = LowerToMaxwell(load);
    INFO(loaded.error);
    REQUIRE(static_cast<bool>(loaded));
    CHECK(std::any_of(loaded.instructions.begin(), loaded.instructions.end(), [&](u64 word) {
        return (word >> 52) == 0xeed && field(word, 0, 8) < 216 && field(word, 8, 8) < 216;
    }));
    // A reserved operand spanning two quartets (R218-R221) cannot be borrowed for.
    const std::array<Instruction, 3> straddle{{
        {{0x00007c02, 0, 0x08000f00, 0}},
        {{0xd8da7381, 0, 0x001ead00, 0x000f2800}},
        EXIT,
    }};
    CHECK_FALSE(LowerToMaxwell(straddle));
}

TEST_CASE("Ampere Ocarina byte constant loads, ULEA carries to UP3 and two-source F2IP",
          "[video_core][ampere]") {
    const auto lower = [](const Instruction& instruction) {
        const std::array<Instruction, 2> code{{instruction, EXIT}};
        return LowerToMaxwell(code);
    };
    const auto field = [](u64 word, u32 offset, u32 count) {
        return (word >> offset) & ((u64{1} << count) - 1);
    };
    // These forms are lowered by default; EDEN_NX2_LOWER_EXTENDED=0 rejects them again.
    const auto set_extended = [](const char* value) {
#ifdef _WIN32
        _putenv_s("EDEN_NX2_LOWER_EXTENDED", value ? value : "");
#else
        if (value) {
            setenv("EDEN_NX2_LOWER_EXTENDED", value, 1);
        } else {
            unsetenv("EDEN_NX2_LOWER_EXTENDED");
        }
#endif
    };
    constexpr Instruction ByteLoad{0x06087abb, 0x01002080, 0x08000000, 0x000fe400};
    set_extended("0");
    CHECK_FALSE(lower({ByteLoad}));
    set_extended(nullptr);
    // ULDC.U8 UR8, c[0x4][UR6+0x82]: LDC.U8 (size 0) indexed by UR6's register, odd offset kept.
    const auto byte_load = lower({{0x06087abb, 0x01002080, 0x08000000, 0x000fe400}});
    INFO(byte_load.error);
    REQUIRE(static_cast<bool>(byte_load));
    const u64 ldc = FindMaxwell(byte_load, 0xef9, 52);
    REQUIRE(ldc != 0);
    CHECK(field(ldc, 48, 3) == 0);
    CHECK(field(ldc, 20, 16) == 0x82);
    CHECK(field(ldc, 36, 5) == 4);

    // ULEA UR10, UP3, UR7, UR10, 0x2: the carry is computed in P3 (6 - 3) and merged into R239.
    const auto lea = lower({{0x070a7291, 0x0000000a, 0x0f86103f, 0x000fe200}});
    INFO(lea.error);
    REQUIRE(static_cast<bool>(lea));
    const u64 compare = FindMaxwell(lea, 0x5b6, 52);
    REQUIRE(compare != 0);
    CHECK(field(compare, 3, 3) == 3);
    CHECK(std::any_of(lea.instructions.begin(), lea.instructions.end(), [&](u64 word) {
        return (word >> 52) == 0x38e && field(word, 0, 8) == 239 && field(word, 20, 8) == 8;
    }));

    // F2IP.U8.F32.NTZ R45, R44, R21, RZ: two byte conversions, R44's shifted into byte 1.
    const auto pack = lower({{0x2c2d7243, 0x00000015, 0x000004ff, 0x000fc400}});
    INFO(pack.error);
    REQUIRE(static_cast<bool>(pack));
    CHECK(CountMaxwell(pack, 0x5cb, 52) == 2);
    CHECK(FindMaxwell(pack, 0x384, 52) != 0);

    // ULDC UR7, c[0x4][UR15+0x29]: only the indexed sum has to be aligned.
    const auto indexed = lower({{0x0f077abb, 0x01000a40, 0x08000800, 0x000fc600}});
    INFO(indexed.error);
    REQUIRE(static_cast<bool>(indexed));
    CHECK(field(FindMaxwell(indexed, 0xef9, 52), 20, 16) == 0x29);

    // I2F.U8 R0, UR17.B3: Maxwell I2F with byte selector 3 (bits 41-42).
    const auto convert = lower({{0x00097d06, 0x30000011, 0x08001000, 0x000fe200}});
    INFO(convert.error);
    REQUIRE(static_cast<bool>(convert));
    CHECK(field(FindMaxwell(convert, 0x5cb, 52), 41, 2) == 3);

    // UISETP.LT.U32.AND UPc, UPT, UR4, 0x2, UPT + USEL UR4, UR4, 0x2, UPc:
    // Matching UP0/UP1/UP2 clamps fold USEL to IMNMX.U32 while preserving UISETP R239 write.
    for (const u32 cond : {0U, 1U, 2U}) {
        const auto clamped = LowerToMaxwell(std::array<Instruction, 3>{{
            {{0x0400788c, 0x00000002, 0x0bf01070U | (cond << 17), 0x000fc800}},
            {{0x04047887, 0x00000002, 0x08000000U | (cond << 23), 0x000fc800}},
            EXIT,
        }});
        REQUIRE(static_cast<bool>(clamped));
        CHECK(CountMaxwell(clamped, 0x382003800027dcdcULL, 0) == 1);
        CHECK(CountMaxwell(
            clamped, 0x38e800000007efefULL | (u64{1U << (6U - cond)} << 20), 0) == 1);
    }
    // Mismatched predicate (UP1 vs UP2), negated USEL (!UP1), or guarded UISETP/USEL must not fold.
    for (const auto& non_clamp : {
             std::array<Instruction, 3>{{
                 {{0x0400788c, 0x00000002, 0x0bf21070, 0x000fc800}},
                 {{0x04047887, 0x00000002, 0x09000000, 0x000fc800}}, EXIT}},
             std::array<Instruction, 3>{{
                 {{0x0400788c, 0x00000002, 0x0bf21070, 0x000fc800}},
                 {{0x04047887, 0x00000002, 0x0c800000, 0x000fc800}}, EXIT}},
             std::array<Instruction, 3>{{
                 {{0x0400088c, 0x00000002, 0x0bf21070, 0x000fc800}},
                 {{0x04047887, 0x00000002, 0x08800000, 0x000fc800}}, EXIT}},
             std::array<Instruction, 3>{{
                 {{0x0400788c, 0x00000002, 0x0bf21070, 0x000fc800}},
                 {{0x04040887, 0x00000002, 0x08800000, 0x000fc800}}, EXIT}},
         }) {
        const auto res = LowerToMaxwell(non_clamp);
        REQUIRE(static_cast<bool>(res));
        CHECK(CountMaxwell(res, 0x382003800027dcdcULL, 0) == 0);
    }
}
TEST_CASE("Ampere three-input IADD3 preserves negative immediates", "[video_core][ampere]") {
    // GrassCullGrid's R4 + (-1) + R7 selects the grid level. Bit 63 is the literal's sign bit.
    for (const u32 literal : {0xffffffffU, 0xfffffffeU, 0x80000000U, 0x7fffffffU, 17U}) {
        const std::array<Instruction, 2> code{{
            {{0x04047810, literal, 0x07ffe007, 0x000fc400}},
            EXIT,
        }};
        const auto lowered = LowerToMaxwell(code);
        REQUIRE(static_cast<bool>(lowered));
        const u64 add = FindMaxwell(lowered, 0x5c1, 52);
        REQUIRE(add != 0);
        CHECK(((add >> 48) & 1) == 0);
        CHECK(((add >> 49) & 1) == 0);
        const u64 move = FindMaxwell(lowered, 0x010, 52);
        CHECK(static_cast<u32>(move >> 20) == literal);
    }
}

TEST_CASE("Ampere three-input IADD3 retains register source negation", "[video_core][ampere]") {
    const std::array<Instruction, 2> code{{
        {{0x04047210, 0x80000005, 0x07ffe907, 0x000fc400}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    std::vector<u64> adds;
    for (const u64 word : lowered.instructions) {
        if ((word >> 52) == 0x5c1) adds.push_back(word);
    }
    REQUIRE(adds.size() == 2);
    CHECK(((adds[0] >> 48) & 1) == 1);
    CHECK(((adds[0] >> 49) & 1) == 1);
    CHECK(((adds[1] >> 48) & 1) == 1);
}

TEST_CASE("Ampere F2F widens a half to a float", "[video_core][ampere]") {
    // Logged from a homebrew (NVK) fragment shader: F2F.F32.F16 R4, R2.
    const std::array<Instruction, 2> code{{
        {{0x00027304, 0x00000002, 0x00101000, 0x000e0200}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    const u64 convert = FindMaxwell(lowered, 0x5ca, 52);
    REQUIRE(convert != 0);
    CHECK(((convert >> 8) & 3) == 2);  // F32 destination
    CHECK(((convert >> 10) & 3) == 1); // F16 source
    CHECK(((convert >> 20) & 0xff) == 2);
    CHECK(((convert >> 41) & 1) == 0); // low half

    // F2F.F32.F16 R4, R1.H1: the high half of the register.
    const std::array<Instruction, 2> high_code{{
        {{0x00017304, 0x10000001, 0x00101000, 0x001e0200}},
        EXIT,
    }};
    const auto high = LowerToMaxwell(high_code);
    REQUIRE(static_cast<bool>(high));
    const u64 high_convert = FindMaxwell(high, 0x5ca, 52);
    CHECK(((high_convert >> 20) & 0xff) == 1);
    CHECK(((high_convert >> 41) & 1) == 1);
}

TEST_CASE("Ampere SHF.R.W.U32.HI lowers to a right shift", "[video_core][ampere]") {
    // Logged from a homebrew (NVK) vertex shader: SHF.R.W.U32.HI R3, RZ, 8, R1.
    const std::array<Instruction, 2> code{{
        {{0xff037819, 0x00000008, 0x00011e01, 0x000fc200}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    const u64 shift = FindMaxwell(lowered, 0x382, 52);
    REQUIRE(shift != 0);
    CHECK((shift & 0xff) == 3);
    CHECK(((shift >> 8) & 0xff) == 1);
    CHECK(((shift >> 20) & 0x7ffff) == 8);
    CHECK(((shift >> 48) & 1) == 0); // logical

    // SHF.L.W.S32 R3, R1, 8, RZ: a left shift of the low word.
    const std::array<Instruction, 2> left_code{{
        {{0x01037819, 0x00000008, 0x00000cff, 0x000fc200}},
        EXIT,
    }};
    const auto left = LowerToMaxwell(left_code);
    REQUIRE(static_cast<bool>(left));
    const u64 left_shift = FindMaxwell(left, 0x384, 52);
    REQUIRE(left_shift != 0);
    CHECK((left_shift & 0xff) == 3);
    CHECK(((left_shift >> 8) & 0xff) == 1);
    CHECK(((left_shift >> 20) & 0x7ffff) == 8);
}

TEST_CASE("Ampere BRA on a uniform predicate branches on the condition code", "[video_core][ampere]") {
    // Logged from a homebrew (NVK) fragment shader: @!UP0 BRA.U to the second EXIT.
    const std::array<Instruction, 3> code{{
        {{0x08007547, 0x00000011, 0x0b800000, 0x001fc200}},
        EXIT,
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    const u64 test = FindMaxwell(lowered, 0x041, 52); // LOP32I.AND.CC RZ, R239, 1 << 6
    REQUIRE(test != 0);
    CHECK((test & 0xff) == 255);
    CHECK(((test >> 8) & 0xff) == 239);
    CHECK(((test >> 20) & 0xffffffff) == (1U << 6));
    const u64 branch = FindMaxwell(lowered, 0xe24, 52);
    REQUIRE(branch != 0);
    CHECK((branch & 0x1f) == 2); // EQ: taken while UP0 is clear
    CHECK(((branch >> 16) & 0xf) == 7);
}

TEST_CASE("Ampere Mesa NAK conversion and multiply forms lower", "[video_core][ampere]") {
    // Logged from homebrew (NVK) shaders. F2I.FTZ.TRUNC.S32 R1, R1 without .NTZ (bit 77).
    const std::array<Instruction, 2> f2i{{
        {{0x00017305, 0x00000001, 0x0021d100, 0x000e0200}},
        EXIT,
    }};
    CHECK(static_cast<bool>(LowerToMaxwell(f2i)));

    // I2F.F32.U32 R4, R4: Maxwell I2F with a 32-bit unsigned source.
    const std::array<Instruction, 2> i2f{{
        {{0x00047306, 0x00000004, 0x00201000, 0x002e0200}},
        EXIT,
    }};
    const auto lowered_i2f = LowerToMaxwell(i2f);
    REQUIRE(static_cast<bool>(lowered_i2f));
    const u64 convert = FindMaxwell(lowered_i2f, 0x5cb, 52);
    REQUIRE(convert != 0);
    CHECK(((convert >> 10) & 3) == 2); // 32-bit source
    CHECK(((convert >> 13) & 1) == 0); // unsigned
    CHECK(((convert >> 20) & 0xff) == 4);

    // FMUL.DNZ R1, R1, R3: Maxwell's FMUL.FMZ.
    const std::array<Instruction, 2> fmul{{
        {{0x01017220, 0x00000003, 0x004010ff, 0x000fc200}},
        EXIT,
    }};
    const auto lowered_fmul = LowerToMaxwell(fmul);
    REQUIRE(static_cast<bool>(lowered_fmul));
    const u64 multiply = FindMaxwell(lowered_fmul, 0x5c6, 52);
    REQUIRE(multiply != 0);
    CHECK(((multiply >> 44) & 3) == 2);

    // USHF.R.W.S32.HI UR1, URZ, 8, UR0: an arithmetic right shift.
    const std::array<Instruction, 2> ushf{{
        {{0x3f017899, 0x00000008, 0x08011c00, 0x000fc200}},
        EXIT,
    }};
    CHECK(static_cast<bool>(LowerToMaxwell(ushf)));
}

TEST_CASE("Ampere BMOV.CLEAR of a convergence barrier is dropped", "[video_core][ampere]") {
    // Logged from a homebrew (NVK) vertex shader: BMOV.32.CLEAR RZ, B0.
    const std::array<Instruction, 2> code{{
        {{0x00ff7355, 0x00000000, 0x00100000, 0x000fc200}},
        EXIT,
    }};
    CHECK(static_cast<bool>(LowerToMaxwell(code)));

    // Reading the barrier into a register stays rejected.
    const std::array<Instruction, 2> read{{
        {{0x00037355, 0x00000000, 0x00000000, 0x000fc200}},
        EXIT,
    }};
    CHECK_FALSE(static_cast<bool>(LowerToMaxwell(read)));
}

// Words from Hyrule Warriors: Age of Imprisonment's gloom decal pixel shader (fed969f5c41bb131).
TEST_CASE("Ampere convergence-state markers lower to nothing", "[video_core][ampere]") {
    const std::array<Instruction, 4> code{{
        {{0x1a0f7f55, 0x00000000, 0x00000000, 0x000fe200}},
        {{0x1a0f7f56, 0x00000000, 0x00100000, 0x000fe400}},
        {{0x1a0f7f56, 0x00000000, 0x00000000, 0x000fe400}},
        EXIT,
    }};
    const std::array<Instruction, 1> exit_only{{EXIT}};
    const auto lowered = LowerToMaxwell(code);
    const auto reference = LowerToMaxwell(exit_only);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(static_cast<bool>(reference));
    CHECK(lowered.instructions.size() == reference.instructions.size());
}

TEST_CASE("Ampere FSWZADD.NDV keeps its swizzle", "[video_core][ampere]") {
    // FSWZADD.NDV R12, R12, R73, swizzle 0xcc
    const std::array<Instruction, 2> code{{
        {{0x0c0c7822, 0x000000cc, 0x00002049, 0x001fc600}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    const u64 swizzled = FindMaxwell(lowered, 0x50f8, 48);
    REQUIRE(swizzled != 0);
    CHECK((swizzled & 0xff) == 12);
    CHECK(((swizzled >> 8) & 0xff) == 12);
    CHECK(((swizzled >> 20) & 0xff) == 0x49);
    CHECK(((swizzled >> 28) & 0xff) == 0xcc);
}

TEST_CASE("Ampere TEX.NDV becomes Maxwell TEX.NDV", "[video_core][ampere]") {
    // TEX.NDV R12, R74, R12, binding 2, 3D, R
    const std::array<Instruction, 2> code{{
        {{0x4a0c7b60, 0x5080120c, 0x001e21ff, 0x00014200}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.instructions.size() >= 4);
    CHECK((lowered.instructions[0] >> 48) == 0x5c98);
    CHECK((lowered.instructions[0] & 0xff) == 250);
    CHECK(((lowered.instructions[0] >> 20) & 0xff) == 74);
    CHECK((lowered.instructions[1] >> 48) == 0x5c98);
    CHECK((lowered.instructions[1] & 0xff) == 251);
    CHECK(((lowered.instructions[1] >> 20) & 0xff) == 75);
    CHECK((lowered.instructions[2] >> 48) == 0x5c98);
    CHECK((lowered.instructions[2] & 0xff) == 252);
    CHECK(((lowered.instructions[2] >> 20) & 0xff) == 12);
    const u64 tex = FindMaxwell(lowered, 0x30, 58);
    REQUIRE(tex != 0);
    CHECK(((tex >> 28) & 7) == 4); // 3D
    CHECK(((tex >> 35) & 1) == 1); // NDV
    CHECK(((tex >> 50) & 1) == 0); // DC 0
    CHECK(((tex >> 54) & 1) == 0); // offset mode 0
}

TEST_CASE("Ampere MOV with a one-lane quad mask writes only that lane", "[video_core][ampere]") {
    // MOV R16, R12 with quad-lane mask 2
    const std::array<Instruction, 2> code{{
        {{0x00107202, 0x0000000c, 0x00000200, 0x000fc800}},
        EXIT,
    }};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    CHECK(CountMaxwell(lowered, 0xf0c8, 48) == 1); // S2R SR_LANEID
    const u64 lane = FindMaxwell(lowered, 0x010, 52);
    CHECK(static_cast<u32>(lane >> 20) == 1);
    const u64 compare = FindMaxwell(lowered, 0x5b6, 52);
    REQUIRE(compare != 0);
    CHECK(((compare >> 3) & 7) == 6);  // P6
    CHECK(((compare >> 49) & 7) == 2); // EQ
    const u64 move = FindMaxwell(lowered, 0x5c98, 48);
    REQUIRE(move != 0);
    CHECK(((move >> 16) & 15) == 6); // @P6
    CHECK((move & 0xff) == 16);
    CHECK(((move >> 20) & 0xff) == 12);

    const std::array<Instruction, 2> two_lanes{{
        {{0x00107202, 0x0000000c, 0x00000300, 0x000fc800}},
        EXIT,
    }};
    REQUIRE(!static_cast<bool>(LowerToMaxwell(two_lanes)));
}
} // namespace
