// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2023 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <boost/container/deque.hpp>

#include "common/common_types.h"
#include "common/hdr.h"
#include "common/polyfill_thread.h"
#include "video_core/vulkan_common/vulkan_memory_allocator.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Core::Frontend {
class EmuWindow;
} // namespace Core::Frontend

namespace Vulkan {

class Device;
class Scheduler;
class Swapchain;

struct Frame {
    u32 width;
    u32 height;
    u32 index;
    /// Format of `image` (RecreateFrame).
    VkFormat format{VK_FORMAT_UNDEFINED};
    vk::Image image;
    vk::ImageView image_view;
    vk::ImageView storage_view;
    vk::Framebuffer framebuffer;
    vk::CommandBuffer cmdbuf;
    vk::Semaphore render_ready;
    vk::Fence present_done;
    bool storage_capable{};
    /// Encoding of the values in `image` (VTX-HLE's screenshots read it; always sRGB here).
    Common::Hdr::Encoding source_encoding{Common::Hdr::Encoding::Srgb};
    /// Rendered through the renderer's Scheduler (Present) rather than submitted by another queue
    /// user (PresentSubmitted, VTX-HLE): only those frames are paced by the scheduler's ticks.
    bool renderer_paced{true};
    bool is_generated{};
    u32 generation_index{};
    u32 generation_count{};
};

class PresentManager {
public:
    PresentManager(const vk::Instance& instance,
                   Core::Frontend::EmuWindow& render_window,
                   const Device& device,
                   MemoryAllocator& memory_allocator,
                   Scheduler& scheduler,
                   Swapchain& swapchain,
                   vk::SurfaceKHR& surface);
    ~PresentManager();

    /// Returns the last used presentation frame
    Frame* GetRenderFrame();

    /// Starts the present worker before the first frame is queued.
    void EnablePresentThread();
    /// Returns a free presentation frame if its present fence is already signaled, or nullptr
    Frame* TryGetRenderFrame();

    /// Pushes a frame for presentation
    void Present(Frame* frame);

    /// Pushes a frame whose rendering has already been submitted to the graphics queue (signalling
    /// frame->render_ready), from a thread other than the renderer's (VTX-HLE's replay). Unlike
    /// Present() it does not record through the scheduler.
    void PresentSubmitted(Frame* frame);

    /// Recreates the present frame to match the provided parameters
    void RecreateFrame(Frame* frame, u32 width, u32 height, VkFormat image_view_format,
                       VkRenderPass rd, bool storage);

    /// The same with the image's own format. `allocator` overrides the manager's (externally
    /// synchronized) allocator for the frame's image: VTX-HLE's replay passes its own thread-safe
    /// one and must release the image before that allocator dies.
    void RecreateFrame(Frame* frame, u32 width, u32 height, VkFormat frame_image_format,
                       VkFormat image_view_format, VkRenderPass rd, bool storage,
                       MemoryAllocator* allocator);

    /// Gives back a frame taken with GetRenderFrame that was never presented (the caller failed
    /// before submitting any work that uses it).
    void ReturnFrame(Frame* frame);

    [[nodiscard]] bool NeedsStorage(const Frame* frame, bool required) const;

    /// Waits for the present thread to finish presenting all queued frames.
    void WaitPresent();

    /// How many additional frames can be queued without stalling the render thread
    [[nodiscard]] size_t MaxExtraFrames() const;

    /// True when frames are presented by the "VulkanPresent" thread instead of the render thread.
    [[nodiscard]] bool UsesPresentThread() const noexcept {
        return use_present_thread;
    }

    [[nodiscard]] std::size_t SwapchainImageCount() const { return swapchain_image_count; }
    [[nodiscard]] VkFormat SwapchainImageFormat() const { return swapchain_image_format; }
    /// Format of the frame images VTX-HLE's replay draws into: the swapchain's.
    [[nodiscard]] VkFormat FrameFormat() const { return swapchain_image_format; }

private:
    void PresentThread(std::stop_token token);

    void CopyToSwapchain(Frame* frame);

    void CopyToSwapchainImpl(Frame* frame);

    void RecreateSwapchain(Frame* frame);

    void DiscardFrame(Frame* frame);

    void SetImageCount();

private:
    const vk::Instance& instance;
    Core::Frontend::EmuWindow& render_window;
    const Device& device;
    MemoryAllocator& memory_allocator;
    Scheduler& scheduler;
    Swapchain& swapchain;
    vk::SurfaceKHR& surface;
    vk::CommandPool cmdpool;
    std::vector<Frame> frames;
    boost::container::deque<Frame*> present_queue;
    boost::container::deque<Frame*> free_queue;
    std::condition_variable_any frame_cv;
    std::condition_variable free_cv;
    std::mutex swapchain_mutex;
    std::mutex queue_mutex;
    std::mutex free_mutex;
    std::jthread present_thread;
    bool blit_supported;
    bool storage_supported;
    bool use_present_thread{};
    bool presenting_frame{};
    bool wake_present_hold{};
    std::atomic<std::size_t> image_count{};
    std::atomic<std::size_t> swapchain_image_count{};
    std::atomic<VkFormat> swapchain_image_format{};
};

} // namespace Vulkan
