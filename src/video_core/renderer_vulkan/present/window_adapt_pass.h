// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2024 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <list>
#include <memory>

#include "common/math_util.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Layout {
struct FramebufferLayout;
}

namespace Tegra {
struct FramebufferConfig;
}

namespace Vulkan {

class Device;
struct Frame;
class Layer;
class Scheduler;
class RasterizerVulkan;

class WindowAdaptPass final {
public:
    explicit WindowAdaptPass(const Device& device, VkFormat frame_format, vk::Sampler&& sampler,
                             vk::ShaderModule&& fragment_shader, bool push_descriptor = false);
    ~WindowAdaptPass();

    void Draw(const Device& device, RasterizerVulkan& rasterizer, Scheduler& scheduler, size_t image_index,
              std::list<Layer>& layers, std::span<const Tegra::FramebufferConfig> configs,
              const Layout::FramebufferLayout& layout, Frame* dst);

    /// Records one opaque layer through the push-descriptor layout used by NVN replay.
    [[nodiscard]] std::shared_ptr<void> RecordTexture(vk::CommandBuffer cmd, const Frame& dst, VkImageView source,
                       const Common::Rectangle<f32>& crop,
                       const Common::Rectangle<u32>& rect) const;

    VkDescriptorSetLayout GetDescriptorSetLayout();
    VkRenderPass GetRenderPass();

private:
    void CreateDescriptorSetLayout(const Device& device, bool push_descriptor);
    void CreatePipelineLayout(const Device& device);
    void CreateVertexShader(const Device& device);
    void CreateRenderPass(const Device& device, VkFormat frame_format);
    void CreatePipelines(const Device& device, bool opaque_only);

    const Device& recording_device;
    bool use_push_descriptors;
    vk::DescriptorSetLayout descriptor_set_layout;
    vk::PipelineLayout pipeline_layout;
    vk::Sampler sampler;
    vk::ShaderModule vertex_shader;
    vk::ShaderModule fragment_shader;
    vk::RenderPass render_pass;
    vk::Pipeline opaque_pipeline;
    vk::Pipeline premultiplied_pipeline;
    vk::Pipeline coverage_pipeline;
};

} // namespace Vulkan
