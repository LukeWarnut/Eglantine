// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

#include "common/common_types.h"
#include "video_core/surface.h"
#include "video_core/vulkan_common/vulkan_wrapper.h"

namespace Vulkan {

class Device;
class ImageView;
class MemoryAllocator;
class Sampler;
class Scheduler;

/// When FrameDump captures, and whether it reads the sampled textures back. Kept apart from the
/// Vulkan side so it can be tested on its own.
class FrameDumpSchedule {
public:
    enum class Step { None, Start, Finish };

    /// EDEN_NX2_FRAME_DUMP_AT: seconds after boot ("45"), a frame number ("2700f"), or "file" to
    /// capture once `trigger_file` exists. EDEN_NX2_FRAME_DUMP_SAMPLED reads back both the
    /// sampled textures and the bound buffers.
    void Configure(std::string_view at, std::filesystem::path trigger_file, bool sampled);
    /// Captures the next frame for the debug server, which is told `id`. False while a capture
    /// runs.
    bool Arm(bool sampled, bool buffers, u64 id);
    /// Once per presented frame: whether a capture starts with the next frame or has ended.
    Step Tick(double elapsed_seconds);
    /// The capture Tick started could not begin.
    void Abort();

    [[nodiscard]] bool Capturing() const noexcept {
        return capturing;
    }
    [[nodiscard]] bool Sampled() const noexcept {
        return sampled;
    }
    /// Whether the uniform and storage buffers and dispatch group counts are recorded.
    [[nodiscard]] bool Buffers() const noexcept {
        return buffers;
    }
    [[nodiscard]] u64 Frame() const noexcept {
        return frame;
    }
    /// The debug server request being captured, 0 for EDEN_NX2_FRAME_DUMP's own capture.
    [[nodiscard]] u64 CaptureId() const noexcept {
        return capture_id;
    }

private:
    void SetCapturing(bool value);

    bool enabled{};
    bool sampled{};
    bool buffers{};
    bool capturing{};
    bool done{};
    std::filesystem::path trigger_file;
    u64 trigger_frame{~0ULL};
    double trigger_seconds{std::numeric_limits<double>::infinity()};
    u64 frame{};
    u64 capture_id{};
};

/// Whether pipelines keep the textures each draw and dispatch samples: only while a capture that
/// reads them back runs, so other frames skip the copies.
[[nodiscard]] bool RecordingSampledTextures() noexcept;

/// Whether compute pipelines keep the images each dispatch writes: while any capture runs.
[[nodiscard]] bool RecordingWrittenImages() noexcept;

/// EDEN_NX2_FRAME_DUMP=<dir> reads back one whole frame: the color targets of every pass, the
/// images written by every compute dispatch and the presented image. EDEN_NX2_FRAME_DUMP_AT picks
/// the frame, in seconds after boot ("45") or as a frame number ("2700f").
/// EDEN_NX2_FRAME_DUMP_SAMPLED=1 also reads back every texture a pass samples (3D and depth
/// included), once per pass, as the first draw sampling it sees it, and the textures each compute
/// shader samples, once per shader, before its first dispatch, with each texture's sampler state,
/// and the uniform and storage buffers of every draw and dispatch, once per shader and binding,
/// with each dispatch's group counts. Readbacks are encoded and written on a worker thread so the
/// capture does not stall the game. Next to each list of shader hashes ("shaders") the manifest
/// has the native program keys of those shaders ("keys", "" where unknown).
class FrameDump {
public:
    struct Sampled {
        u32 stage;
        u32 tic;
        ImageView* view;
        const Sampler* sampler;
    };

    /// A uniform or storage buffer binding. Its bytes come from guest memory, or from the host
    /// buffer when the GPU wrote the range and guest memory may be stale.
    struct BoundBuffer {
        u32 stage;
        bool storage;
        u32 index;
        bool written;
        GPUVAddr gpu_addr;
        u64 device_addr;
        u32 bound_size;
        u32 size;
        bool gpu_modified;
        VkBuffer host;
        u32 host_offset;
        std::vector<u8> guest;
    };

    /// The current draw's arguments as the engine holds them. For an indirect draw, the first
    /// argument records as guest memory holds them (stale when the GPU wrote them).
    struct DrawArgs {
        u32 topology;
        bool indexed;
        u32 vertex_first;
        u32 vertex_count;
        u32 index_first;
        u32 index_count;
        u32 index_format;
        u32 base_vertex;
        u32 base_instance;
        u32 instance_count;
        bool indirect;
        bool include_count;
        bool from_record;
        bool indirect_gpu_modified;
        GPUVAddr indirect_addr;
        GPUVAddr count_addr;
        u32 max_draws;
        u32 stride;
        std::vector<u32> indirect_words;
        /// Raw vertex_attrib_format and vertex_streams words of the enabled ones.
        std::vector<std::pair<u32, u32>> attributes;
        std::vector<std::pair<u32, u32>> streams;
    };

    /// A bound index (slot -1), indirect argument (slot -2) or vertex buffer. Guest bytes are
    /// always kept; the cached host
    /// copy is read back too, so the two can be compared.
    struct VertexInput {
        s32 slot;
        GPUVAddr gpu_addr;
        u64 device_addr;
        u32 bound_size;
        u32 size;
        bool gpu_modified;
        VkBuffer host;
        u32 host_offset;
        std::vector<u8> guest;
    };

    explicit FrameDump(const Device& device, MemoryAllocator& memory_allocator,
                       Scheduler& scheduler);
    ~FrameDump();

    [[nodiscard]] bool Capturing() const noexcept {
        return schedule.Capturing();
    }

    [[nodiscard]] bool CapturingSampled() const noexcept {
        return schedule.Capturing() && schedule.Sampled();
    }

    [[nodiscard]] bool CapturingBuffers() const noexcept {
        return schedule.Capturing() && schedule.Buffers();
    }

    /// Reads back the textures of the current draw not yet seen in this pass. Returns true when
    /// copies were recorded, which ends the current render pass.
    bool NoteSampled(std::span<const Sampled> textures);
    /// Reads back the textures of the dispatch about to run not yet seen for this shader.
    void NoteDispatchSampled(u64 shader_hash, std::span<const Sampled> textures);
    /// Records the buffers of the current draw (shader hashes by program) or of the dispatch about
    /// to run not yet seen for their shader. Returns true when copies were recorded.
    bool NoteBuffers(bool compute, std::span<const u64> shader_hashes,
                     std::span<BoundBuffer> buffers);
    /// Records the current draw's arguments ("draw_args") and its index and vertex buffers not yet
    /// seen this frame, each as guest bytes and, when cached, host bytes. Returns true when copies
    /// were recorded.
    bool NoteDrawInputs(std::span<const u64> shader_hashes, const DrawArgs& args,
                        std::span<VertexInput> inputs);
    /// Records the group counts of the dispatch about to run.
    void NoteLaunch(u64 shader_hash, const std::array<u32, 3>& grid,
                    const std::array<u32, 3>& block, std::optional<GPUVAddr> indirect,
                    std::optional<std::array<u32, 3>> indirect_grid);
    /// Remembers the native program keys of shader hashes (0 where not native) for the manifest.
    void NoteKeys(std::span<const u64> shader_hashes, std::span<const u64> keys);

    /// Starts a pass when the framebuffer changes and captures the previous one.
    /// Returns true when copies were recorded, which ends the current render pass.
    bool BeginPass(const void* framebuffer, std::span<ImageView* const> colors);
    void NoteDraw(std::span<const u64> shader_hashes);
    /// Captures the current pass; returns true when copies were recorded.
    bool EndPass();
    void CaptureDispatch(u64 shader_hash, std::span<ImageView* const> written);
    void CapturePresent(ImageView& view);
    /// Waits for pending readbacks and writes them once they pass the staging budget.
    void DrainIfFull();
    void TickFrame();
    /// Captures the next frame into `directory` for the debug server, which is told `id` once
    /// the manifest is complete. Works without EDEN_NX2_FRAME_DUMP.
    /// Sampled textures over `sampled_max_bytes` (0: no limit) are listed without readback.
    void Arm(std::filesystem::path directory, bool sampled, bool buffers, u64 id,
             size_t sampled_max_bytes = 0, std::vector<u32> sampled_tics = {});

private:
    enum class DepthEncoding { None, Unorm16, Unorm24, Float32 };

    struct Target {
        u32 slot;
        VkImage image;
        VideoCore::Surface::PixelFormat format;
        u32 width;
        u32 height;
        u32 depth;
        s32 level;
        s32 layer;
        GPUVAddr gpu_addr;
        bool rescaled;
        bool msaa;
        DepthEncoding depth_encoding;
        std::optional<VideoCore::Surface::PixelFormat> host_blocks{};
        VkImage view_image{};
        s32 view_mip_count{1};
    };

    /// An image readback, or a buffer one (`raw`) of `bytes` bytes.
    struct Readback {
        vk::Buffer buffer;
        Target target;
        std::string name;
        std::string tags;
        bool raw{};
        size_t bytes{};
    };

    /// Work for the writer thread: an image readback (`name` set), buffer bytes (`raw`), a
    /// finished manifest line, or the summary.
    struct Job {
        Target target{};
        std::string name;
        std::string tags;
        std::vector<u8> data;
        bool raw{};
        bool summary{};
        bool truncated{};
    };

    std::optional<Target> MakeTarget(ImageView& view, u32 slot) const;
    /// "shaders":[...],"keys":[...] for these hashes.
    std::string ShaderList(std::span<const u64> hashes) const;
    /// The native program key of a shader hash as 16 hex digits, or "" when unknown.
    std::string KeyOf(u64 shader_hash) const;
    static u32 TexelBytes(const Target& target);
    static size_t ImageBytes(const Target& target);
    static int Decode(const Target& target, const u8* pixel, std::array<float, 4>& out);
    bool Record(const Target& target, std::string name, std::string tags);
    bool RecordSampled(const Sampled& texture, u32 index, std::string name, std::string tags);
    bool RecordBuffer(VkBuffer source, u32 offset, u32 size, std::string tags);
    void Drain();
    void Enqueue(Job job);
    void WriterLoop();
    void Write(const Job& job);
    void WriteBuffer(const Job& job);
    void WriteFile(const std::string& name, std::span<const u8> data);
    void StopWriter();

    const Device& device;
    MemoryAllocator& memory_allocator;
    Scheduler& scheduler;

    FrameDumpSchedule schedule;
    bool truncated{};
    std::filesystem::path directory;
    std::chrono::steady_clock::time_point start;

    std::ofstream manifest;
    std::vector<Readback> readbacks;
    size_t pending_bytes{};
    size_t recorded_bytes{};
    size_t sampled_max_bytes{};
    std::vector<u32> sampled_tics;
    size_t written_bytes{};
    u32 sequence{};
    u32 images{};

    const void* pass_framebuffer{};
    std::vector<Target> pass_targets;
    std::vector<u64> pass_hashes;
    std::vector<std::pair<GPUVAddr, u32>> pass_sampled;
    /// (compute shader hash, image address, format/level/layer) already read back this frame.
    std::vector<std::tuple<u64, GPUVAddr, u32>> dispatch_sampled;
    /// (shader hash, storage, binding index, device address, size) already recorded this frame.
    /// Shader, storage, index, address and size of a recorded buffer, with the hash of the guest
    /// bytes last recorded for it (0 for host-read buffers).
    using SeenBuffer = std::pair<std::tuple<u64, bool, u32, u64, u32>, u64>;
    std::vector<SeenBuffer> seen_buffers;
    /// (device address, size) of the index and vertex buffers already recorded this frame.
    std::vector<std::pair<u64, u32>> seen_vertex_inputs;
    /// Content hashes of the buffer files written; writer thread only.
    std::vector<u64> written_buffers;
    /// (shader hash, native program key) of the shaders seen this capture.
    std::vector<std::pair<u64, u64>> shader_keys;
    u32 pass_index{};
    u32 pass_first_draw{};
    u32 draws{};
    u32 dispatches{};

    std::thread writer;
    std::mutex queue_mutex;
    std::condition_variable queue_ready;
    std::deque<Job> queue;
    bool stop_writer{};
};

} // namespace Vulkan
