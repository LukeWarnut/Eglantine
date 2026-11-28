// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// NVK's meta blits (Touhou 10's texture uploads) merge the texture index of one constant buffer
// word with the sampler index of another through LOP3.LUT 0xe2. Tracking kept only the sampler
// word, so the blits sampled texture 0 and wrote nothing.

#include <array>
#include <span>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/host_translate_info.h"
#include "shader_recompiler/ir_opt/passes.h"

namespace {
namespace IR = Shader::IR;

class HandleEnvironment final : public Shader::Environment {
public:
    explicit HandleEnvironment(bool native_ = false) : native{native_} {
        stage = Shader::Stage::Fragment;
    }

    bool IsNativeProgram() const override {
        return native;
    }

    u64 ReadInstruction(u32) override {
        FAIL("No instructions in this fixture");
        return 0;
    }
    u32 ReadCbufValue(u32 bank, u32 offset) override {
        return (bank << 16) | offset;
    }
    Shader::TextureType ReadTextureType(u32) override {
        return Shader::TextureType::ColorArray2D;
    }
    Shader::TexturePixelFormat ReadTexturePixelFormat(u32) override {
        return Shader::TexturePixelFormat::A8B8G8R8_UNORM;
    }
    bool IsTexturePixelFormatInteger(u32) override {
        return false;
    }
    u32 ReadViewportTransformState() override {
        return 1;
    }
    u32 TextureBoundBuffer() const override {
        return 0;
    }
    u32 LocalMemorySize() const override {
        return 0;
    }
    u32 SharedMemorySize() const override {
        return 0;
    }
    std::array<u32, 3> WorkgroupSize() const override {
        return {1, 1, 1};
    }
    bool HasHLEMacroState() const override {
        return false;
    }
    std::optional<Shader::ReplaceConstant> GetReplaceConstBuffer(u32, u32) override {
        return std::nullopt;
    }
    void Dump(u64, u64) override {}

private:
    bool native;
};

} // namespace

TEST_CASE("A bit select of two constant buffer words becomes a handle program",
          "[video_core][shader][texture_pass]") {
    HandleEnvironment env;
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;
    program.stage = Shader::Stage::Fragment;
    program.blocks = {&block};
    program.post_order_blocks = {&block};
    program.syntax_list = {{{.block = &block}, IR::AbstractSyntaxNode::Type::Block},
                           {{}, IR::AbstractSyntaxNode::Type::Return}};
    IR::IREmitter ir{block};
    // ULOP3.LUT UR0, c[0x1][0x4], 0xfffff, c[0x1][0x0], 0xe2 as the LOP3 translation emits it.
    const IR::U32 texture_word{ir.GetCbuf(ir.Imm32(1), ir.Imm32(4))};
    const IR::U32 sampler_word{ir.GetCbuf(ir.Imm32(1), ir.Imm32(0))};
    const IR::U32 mask{ir.Imm32(0xfffffU)};
    const IR::U32 handle{ir.BitwiseAnd(ir.BitwiseOr(texture_word, ir.BitwiseNot(mask)),
                                       ir.BitwiseOr(mask, sampler_word))};
    IR::TextureInstInfo flags{};
    flags.type.Assign(Shader::TextureType::ColorArray2D);
    const auto coords = ir.CompositeConstruct(ir.Imm32(0.5f), ir.Imm32(0.5f), ir.Imm32(0.0f));
    (void)ir.ImageSampleExplicitLod(handle, coords, ir.Imm32(0.0f), {}, flags);

    Shader::HostTranslateInfo host;
    host.ApplyDescriptorLimitPolicy();
    Shader::Optimization::TexturePass(env, program, host);

    REQUIRE(program.info.texture_descriptors.size() == 1);
    const auto& desc = program.info.texture_descriptors[0];
    REQUIRE(desc.handle_program != ~0U);
    REQUIRE(desc.handle_program < program.info.handle_programs.size());
    // The program reads both words, not just the one the search reached first.
    const std::span<const u32> code{program.info.handle_programs[desc.handle_program]};
    const auto reads = [&](u32 offset) {
        for (size_t i = 0; i + 2 < code.size(); ++i) {
            if (code[i] == static_cast<u32>(Shader::HandleOp::Cbuf) && code[i + 1] == 1 &&
                code[i + 2] == offset) {
                return true;
            }
        }
        return false;
    };
    CHECK(reads(0));
    CHECK(reads(4));
}

TEST_CASE("A handle read from a constant buffer array at a per-pixel offset becomes an array",
          "[video_core][shader][texture_pass]") {
    // Mesa NVK: textures[nonuniformEXT(index)] reads the combined image/sampler word at
    // c[set][index * 4], the index computed per pixel (here from an attribute).
    HandleEnvironment env{true};
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;
    program.stage = Shader::Stage::Fragment;
    program.blocks = {&block};
    program.post_order_blocks = {&block};
    program.syntax_list = {{{.block = &block}, IR::AbstractSyntaxNode::Type::Block},
                           {{}, IR::AbstractSyntaxNode::Type::Return}};
    IR::IREmitter ir{block};
    const IR::F32 attribute{ir.GetAttribute(IR::Attribute::Generic0X)};
    const IR::U32 index{ir.ConvertFToU(32, ir.FPMul(attribute, ir.Imm32(4.0f)))};
    const IR::U32 handle{ir.GetCbuf(ir.Imm32(2), ir.ShiftLeftLogical(index, ir.Imm32(2)))};
    IR::TextureInstInfo flags{};
    flags.type.Assign(Shader::TextureType::Color2D);
    const auto coords = ir.CompositeConstruct(ir.Imm32(0.5f), ir.Imm32(0.5f));
    (void)ir.ImageSampleExplicitLod(handle, coords, ir.Imm32(0.0f), {}, flags);

    Shader::HostTranslateInfo host;
    host.ApplyDescriptorLimitPolicy();
    Shader::Optimization::TexturePass(env, program, host);

    REQUIRE(program.info.texture_descriptors.size() == 1);
    const auto& desc = program.info.texture_descriptors[0];
    CHECK(desc.handle_program == ~0U);
    CHECK(!desc.bindless);
    CHECK(desc.cbuf_index == 2);
    CHECK(desc.cbuf_offset == 0);
    CHECK(desc.size_shift == 2);
    CHECK(desc.count > 1);
}
