// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <future>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/alignment.h"
#include "common/common_funcs.h"
#include "common/common_types.h"
#include "common/fs/fs_util.h"
#include "common/fs/path_util.h"
#include "common/literals.h"
#include "common/logging.h"
#include "common/settings.h"
#include "common/random.h"
#include "common/swap.h"
#include "core/arm/arm_interface.h"
#include "core/core.h"
#include "core/file_sys/control_metadata.h"
#include "core/file_sys/romfs_factory.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_offset.h"
#include "core/hle/kernel/code_set.h"
#include "core/hle/kernel/k_page_table.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/kernel/svc_types.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/hle/service/ro/ro.h"
#include "core/loader/nro.h"
#include "core/memory.h"
#include "video_core/gpu.h"

#ifdef HAS_NCE
#include "core/arm/nce/patcher.h"
#endif

namespace Loader {

struct NroSegmentHeader {
    u32_le offset;
    u32_le size;
};
static_assert(sizeof(NroSegmentHeader) == 0x8, "NroSegmentHeader has incorrect size.");

struct NroHeader {
    INSERT_PADDING_BYTES(0x4);
    u32_le module_header_offset;
    u32 magic_ext1;
    u32 magic_ext2;
    u32_le magic;
    INSERT_PADDING_BYTES(0x4);
    u32_le file_size;
    INSERT_PADDING_BYTES(0x4);
    std::array<NroSegmentHeader, 3> segments; // Text, RoData, Data (in that order)
    u32_le bss_size;
    INSERT_PADDING_BYTES(0x44);
};
static_assert(sizeof(NroHeader) == 0x80, "NroHeader has incorrect size.");

struct ModHeader {
    u32_le magic;
    u32_le dynamic_offset;
    u32_le bss_start_offset;
    u32_le bss_end_offset;
    u32_le unwind_start_offset;
    u32_le unwind_end_offset;
    u32_le module_offset; // Offset to runtime-generated module object. typically equal to .bss base
};
static_assert(sizeof(ModHeader) == 0x1c, "ModHeader has incorrect size.");

struct AssetSection {
    u64_le offset;
    u64_le size;
};
static_assert(sizeof(AssetSection) == 0x10, "AssetSection has incorrect size.");

struct AssetHeader {
    u32_le magic;
    u32_le format_version;
    AssetSection icon;
    AssetSection nacp;
    AssetSection romfs;
};
static_assert(sizeof(AssetHeader) == 0x38, "AssetHeader has incorrect size.");

AppLoader_NRO::AppLoader_NRO(FileSys::VirtualFile file_) : AppLoader(std::move(file_)) {
    NroHeader nro_header{};
    if (file->ReadObject(&nro_header) != sizeof(NroHeader)) {
        return;
    }

    if (file->GetSize() >= nro_header.file_size + sizeof(AssetHeader)) {
        const u64 offset = nro_header.file_size;
        AssetHeader asset_header{};
        if (file->ReadObject(&asset_header, offset) != sizeof(AssetHeader)) {
            return;
        }

        if (asset_header.format_version != 0) {
            LOG_WARNING(Loader,
                        "NRO Asset Header has format {}, currently supported format is 0. If "
                        "strange glitches occur with metadata, check NRO assets.",
                        asset_header.format_version);
        }

        if (asset_header.magic != Common::MakeMagic('A', 'S', 'E', 'T')) {
            return;
        }

        if (asset_header.nacp.size > 0) {
            nacp = std::make_unique<FileSys::NACP>(std::make_shared<FileSys::OffsetVfsFile>(
                file, asset_header.nacp.size, offset + asset_header.nacp.offset, "Control.nacp"));
        }

        if (asset_header.romfs.size > 0) {
            romfs = std::make_shared<FileSys::OffsetVfsFile>(
                file, asset_header.romfs.size, offset + asset_header.romfs.offset, "game.romfs");
        }

        if (asset_header.icon.size > 0) {
            icon_data = file->ReadBytes(asset_header.icon.size, offset + asset_header.icon.offset);
        }
    }
}

AppLoader_NRO::~AppLoader_NRO() = default;

FileType AppLoader_NRO::IdentifyType(const FileSys::VirtualFile& nro_file) {
    // Read NSO header
    NroHeader nro_header{};
    if (sizeof(NroHeader) != nro_file->ReadObject(&nro_header)) {
        return FileType::Error;
    }
    if (nro_header.magic == Common::MakeMagic('N', 'R', 'O', '0')) {
        return FileType::NRO;
    }
    return FileType::Error;
}

bool AppLoader_NRO::IsHomebrew() {
    // Read NSO header
    NroHeader nro_header{};
    if (sizeof(NroHeader) != file->ReadObject(&nro_header)) {
        return false;
    }
    return nro_header.magic_ext1 == Common::MakeMagic('H', 'O', 'M', 'E') &&
           nro_header.magic_ext2 == Common::MakeMagic('B', 'R', 'E', 'W');
}

static constexpr u32 PageAlignSize(u32 size) {
    return static_cast<u32>((size + Core::Memory::YUZU_PAGEMASK) & ~Core::Memory::YUZU_PAGEMASK);
}

// The NRO's path on the emulated SD card ("sdmc:/switch/app.nro") when it is stored there. Homebrew
// loaders pass it as argv[0], and libnx mounts the NRO's RomFS from that file.
static std::optional<std::string> SdmcPathOf(const std::string& host_path) {
    if (host_path.empty()) {
        return std::nullopt;
    }
    std::error_code ec;
    const auto sdmc = std::filesystem::weakly_canonical(
        Common::FS::GetEdenPath(Common::FS::EdenPath::SDMCDir), ec);
    if (ec) {
        return std::nullopt;
    }
    const auto file = std::filesystem::weakly_canonical(Common::FS::ToU8String(host_path), ec);
    if (ec) {
        return std::nullopt;
    }
    const auto relative = file.lexically_relative(sdmc);
    if (relative.empty() || *relative.begin() == "..") {
        return std::nullopt;
    }
    return "sdmc:/" + Common::FS::PathToUTF8String(relative.generic_u8string());
}

namespace {

struct ConfigEntry {
    u32_le key;
    u32_le flags;
    u64_le value[2];
};
static_assert(sizeof(ConfigEntry) == 0x18);

constexpr u32 kEntryEndOfList = 0;
constexpr u32 kEntryMainThreadHandle = 1;
constexpr u32 kEntryNextLoadPath = 2;
constexpr u32 kEntryOverrideHeap = 3;
constexpr u32 kEntryOverrideService = 4;
constexpr u32 kEntryArgv = 5;
constexpr u32 kEntryAppletType = 7;
constexpr u32 kAppletTypeApplication = 0;
constexpr u32 kEntryFlagMandatory = 1;
// AArch64 encoding for svc #0x7 (ExitProcess).
constexpr u32 kSvcExitProcessInstruction = 0xD40000E1;
// "ldr:ro" as sm encodes service names.
constexpr u64 kLdrRoServiceName = 0x00006f723a72646cULL;
// MainThreadHandle, AppletType, NextLoadPath, Argv, OverrideService (ldr:ro), OverrideHeap (NROs
// after the first), EndOfList
constexpr size_t kNumEntries = 7;
constexpr size_t kLdrRoEntry = 4;
constexpr size_t kConfigTableSize = kNumEntries * sizeof(ConfigEntry);
// NextLoadPath buffers, sized like libnx's (hbmenu needs the entry to launch anything).
constexpr size_t kNextLoadPathSize = 0x200;
constexpr size_t kNextLoadArgvSize = 0x800;

struct HomebrewImage {
    Kernel::CodeSet codeset;
    std::vector<u8> memory;
    std::string argv; ///< NUL-terminated; empty when the image has no loader data.
    size_t config_offset{};
    std::optional<size_t> exit_process_offset;

    size_t ArgvOffset() const {
        return config_offset + kConfigTableSize;
    }
    size_t NextPathOffset() const {
        return config_offset + Common::AlignUp(kConfigTableSize + argv.size(), 0x10);
    }
    size_t NextArgvOffset() const {
        return NextPathOffset() + kNextLoadPathSize;
    }
};

// The NRO with its .bss and, when argv is set, the homebrew loader data (config table, argv and
// NextLoadPath buffers) after it.
std::optional<HomebrewImage> BuildHomebrewImage(const std::vector<u8>& data, std::string argv) {
    if (data.size() < sizeof(NroHeader)) {
        return std::nullopt;
    }

    // Read NSO header
    NroHeader nro_header{};
    std::memcpy(&nro_header, data.data(), sizeof(NroHeader));
    if (nro_header.magic != Common::MakeMagic('N', 'R', 'O', '0')) {
        return std::nullopt;
    }

    // Build program image
    HomebrewImage image;
    auto& program_image = image.memory;
    program_image.resize(PageAlignSize(nro_header.file_size));
    std::memcpy(program_image.data(), data.data(), (std::min)(program_image.size(), data.size()));

    auto& codeset = image.codeset;
    for (std::size_t i = 0; i < nro_header.segments.size(); ++i) {
        codeset.segments[i].addr = nro_header.segments[i].offset;
        codeset.segments[i].offset = nro_header.segments[i].offset;
        codeset.segments[i].size = PageAlignSize(nro_header.segments[i].size);
    }

    // Default .bss to NRO header bss size if MOD0 section doesn't exist
    u32 bss_size{PageAlignSize(nro_header.bss_size)};

    // Read MOD header
    ModHeader mod_header{};
    if (nro_header.module_header_offset + sizeof(ModHeader) <= program_image.size()) {
        std::memcpy(&mod_header, program_image.data() + nro_header.module_header_offset,
                    sizeof(ModHeader));
    }

    const bool has_mod_header{mod_header.magic == Common::MakeMagic('M', 'O', 'D', '0')};
    if (has_mod_header) {
        // Resize program image to include .bss section and page align each section
        bss_size = PageAlignSize(mod_header.bss_end_offset - mod_header.bss_start_offset);
    }

    codeset.DataSegment().size += bss_size;
    program_image.resize(static_cast<u32>(program_image.size()) + bss_size);
    if (argv.empty()) {
        return image;
    }

    image.argv = std::move(argv);
    image.argv.push_back('\0');

    const auto& code = codeset.CodeSegment();
    const size_t code_end = (std::min)(program_image.size(), code.offset + code.size);
    for (size_t offset = code.offset; offset + sizeof(u32) <= code_end; offset += sizeof(u32)) {
        u32 instruction{};
        std::memcpy(&instruction, program_image.data() + offset, sizeof(instruction));
        if (instruction == kSvcExitProcessInstruction) {
            image.exit_process_offset = offset;
            break;
        }
    }
    if (!image.exit_process_offset) {
        LOG_WARNING(Loader, "Unable to find svcExitProcess in NRO; returning from main may fault");
    }

    image.config_offset = program_image.size();
    const size_t loader_data_size =
        Common::AlignUp(image.NextArgvOffset() + kNextLoadArgvSize - image.config_offset,
                        Core::Memory::YUZU_PAGESIZE);
    codeset.DataSegment().size += static_cast<u32>(loader_data_size);
    program_image.resize(image.config_offset + loader_data_size);
    return image;
}

// ounce-hbloader also passes its ldr:ro session, which kaihen's JIT commands go through.
void WriteLoaderConfig(Kernel::KProcess& process, const HomebrewImage& image, u64 base,
                       u32 main_thread_handle, u32 ldr_ro_handle, u64 heap_address = 0,
                       u64 heap_size = 0) {
    ConfigEntry entries[kNumEntries] = {
        {kEntryMainThreadHandle, 0, {main_thread_handle, 0}},
        {kEntryAppletType, 0, {kAppletTypeApplication, 0}},
        {kEntryNextLoadPath, 0, {base + image.NextPathOffset(), base + image.NextArgvOffset()}},
        {kEntryArgv, 0, {0, base + image.ArgvOffset()}},
        {kEntryOverrideService, 0, {kLdrRoServiceName, ldr_ro_handle}},
        {kEntryEndOfList, 0, {0, 0}},
        {kEntryEndOfList, 0, {0, 0}},
    };
    static_assert(kLdrRoEntry == 4);
    if (heap_size != 0) {
        entries[5] = {kEntryOverrideHeap, kEntryFlagMandatory, {heap_address, heap_size}};
    }
    auto& memory = process.GetMemory();
    memory.WriteBlock(Common::ProcessAddress{base + image.ArgvOffset()}, image.argv.data(),
                      image.argv.size());
    memory.WriteBlock(Common::ProcessAddress{base + image.config_offset}, entries,
                      sizeof(entries));
}

std::string QuoteArgument(const std::string& argument) {
    return argument.find(' ') != std::string::npos ? '"' + argument + '"' : argument;
}

// The host file for "sdmc:/switch/app.nro", or empty when it is not on the SD card.
std::string HostPathOfSdmc(std::string_view path) {
    // hbloader opens the path through libnx, whose default device is sdmc: RetroArch passes its
    // cores as "/retroarch/cores/...".
    constexpr std::string_view prefix = "sdmc:/";
    if (path.starts_with(prefix)) {
        path.remove_prefix(prefix.size());
    } else if (path.starts_with('/')) {
        path.remove_prefix(1);
    } else {
        return {};
    }
    const std::filesystem::path relative{Common::FS::ToU8String(path)};
    for (const auto& part : relative) {
        if (part == "..") {
            return {};
        }
    }
    return Common::FS::PathToUTF8String(
        Common::FS::GetEdenPath(Common::FS::EdenPath::SDMCDir) / relative.relative_path());
}

// What ounce-hbloader keeps between the NROs it runs in one process: the first one (the menu) and
// the one running now, with its mapping and NextLoadPath buffers.
struct HomebrewChain {
    u64 process_id{};
    std::string menu_path;
    std::string menu_argv;
    std::string current_path;
    u64 base{};
    size_t image_size{};
    u64 alias_source{}; ///< Heap pages the running NRO is aliased from; 0 for the first.
    u32 ldr_ro_handle{};
    u64 config_addr{};
    u64 next_path_addr{};
    u64 next_argv_addr{};
};

std::mutex g_chain_mutex;
std::optional<HomebrewChain> g_chain;

void SetChainImage(HomebrewChain& chain, const HomebrewImage& image, u64 base, size_t image_size) {
    chain.base = base;
    chain.image_size = image_size;
    chain.config_addr = base + image.config_offset;
    chain.next_path_addr = base + image.NextPathOffset();
    chain.next_argv_addr = base + image.NextArgvOffset();
}

} // Anonymous namespace

static bool LoadNroImpl(Core::System& system, Kernel::KProcess& process,
                        const std::vector<u8>& data, const std::string& host_path) {
    // Without an SD card path or arguments, the NRO runs like an NSO and its RomFS comes from the
    // process.
    const auto sdmc_path = SdmcPathOf(host_path);
    const auto& program_args = Settings::values.program_args.GetValue();
    std::string argv;
    if (sdmc_path || !program_args.empty()) {
        argv = sdmc_path ? QuoteArgument(*sdmc_path) : "homebrew";
        if (!program_args.empty()) {
            argv += ' ';
            argv += program_args;
        }
    }
    auto image = BuildHomebrewImage(data, argv);
    if (!image) {
        return false;
    }
    auto& codeset = image->codeset;
    auto& program_image = image->memory;
    size_t image_size = program_image.size();

#ifdef HAS_NCE
    const auto& code = codeset.CodeSegment();

    // NROs always have a 39-bit address space.
    Settings::SetNceEnabled(true);

    // Create NCE patcher
    Core::NCE::Patcher patch{};

    if (Settings::IsNceEnabled()) {
        // Patch SVCs and MRS calls in the guest code
        patch.PatchText(program_image, code);

        // We only support PostData patching for NROs.
        ASSERT(patch.GetPatchMode() == Core::NCE::PatchMode::PostData);

        // Update patch section.
        auto& patch_segment = codeset.PatchSegment();
        patch_segment.addr = image_size;
        patch_segment.size = static_cast<u32>(patch.GetSectionSize());

        // Add patch section size to the module size.
        image_size += patch_segment.size;
    }
#endif

    // Enable direct memory mapping in case of NCE.
    const u64 fastmem_base = [&]() -> size_t {
        if (Settings::IsNceEnabled()) {
            auto& buffer = system.DeviceMemory().buffer;
            buffer.EnableDirectMappedAddress();
            return reinterpret_cast<u64>(buffer.VirtualBasePointer());
        }
        return 0;
    }();

    // TODO: this is bad form of ASLR, it sucks
    std::uintptr_t aslr_offset = ((::Settings::values.rng_seed_enabled.GetValue()
        ? ::Settings::values.rng_seed.GetValue() : Common::Random::Random64(0)) << 12) & 0xfff000;

    // Homebrew runs in the application hbloader took over, with its system resource: without one
    // svcMapPhysicalMemory fails (Cemu maps its guest memory with it). 16 MiB, as Suika's NPDM.
    constexpr u32 HomebrewSystemResourceSize = 0x1000000;

    // Chromium NX reserves its Linux guest's address space in the alias region, and Blink's heap
    // cage does not fit in 64 GiB next to V8's and PartitionAlloc's: give homebrew 128 GiB.
    using namespace Common::Literals;
    process.SetAliasRegionPadding(64_GiB);

    // Setup the process code layout
    if (process
            .LoadFromMetadata(system.Kernel(),
                              FileSys::ProgramMetadata::GetDefault(HomebrewSystemResourceSize),
                              image_size, fastmem_base, aslr_offset)
            .IsError()) {
        return false;
    }
    // hbloader hands homebrew a heap of all the reported total memory, and Cemu maps its guest
    // memory past that with svcMapPhysicalMemory (1.34 GiB for NES Remix): keep 2 GiB of the
    // limit out of the total, at most half of it.
    process.ReservePhysicalMemoryOutsideTotal(2_GiB);

    // Relocate code patch and copy to the program_image if running under NCE.
    // This needs to be after LoadFromMetadata so we can use the process entry point.
#ifdef HAS_NCE
    if (Settings::IsNceEnabled()) {
        patch.RelocateAndCopy(process.GetEntryPoint(), code, program_image,
                              &process.GetPostHandlers());
    }
#endif

    // Load codeset for current process
    const u64 base = GetInteger(process.GetEntryPoint());
    {
        std::scoped_lock lock{g_chain_mutex};
        g_chain.reset();
        if (!image->argv.empty()) {
            g_chain = HomebrewChain{
                .process_id = process.GetProcessId(),
                .menu_path = host_path,
                .menu_argv = argv,
                .current_path = host_path,
            };
            SetChainImage(*g_chain, *image, base, image_size);
        }
    }
    codeset.memory = std::move(program_image);
    process.LoadModule(system.Kernel(), std::move(codeset), process.GetEntryPoint());
    if (!image->argv.empty()) {
        // The main thread and ldr:ro handles are patched in Run().
        WriteLoaderConfig(process, *image, base, 0, 0);
        const u64 config_addr = base + image->config_offset;
        process.SetRunCallback([&system, config_addr](Kernel::KProcess& running) {
            const u32 handle = Service::RO::OpenHomebrewSession(system, running);
            running.GetMemory().Write64(config_addr + kLdrRoEntry * sizeof(ConfigEntry) +
                                            offsetof(ConfigEntry, value) + sizeof(u64),
                                        handle);
            std::scoped_lock lock{g_chain_mutex};
            if (g_chain && g_chain->process_id == running.GetProcessId()) {
                g_chain->ldr_ro_handle = handle;
            }
        });
        process.SetArgPointer(Kernel::KProcessAddress{config_addr});
        if (image->exit_process_offset) {
            process.SetArgReturnAddress(Kernel::KProcessAddress{base + *image->exit_process_offset});
        }
        process.SetMainThreadHandleAddr(
            Kernel::KProcessAddress{config_addr + offsetof(ConfigEntry, value)});
    }

    return true;
}

bool ContinueHomebrewChain(Core::System& system, Kernel::KProcess& process,
                           std::span<u64, 8> args) {
    std::scoped_lock lock{g_chain_mutex};
    if (!g_chain || g_chain->process_id != process.GetProcessId()) {
        return false;
    }
    auto& chain = *g_chain;
    auto& memory = process.GetMemory();

    // As ounce-hbloader: run the NRO the last one asked for, else the menu again; the menu
    // returning without a request ends the process.
    std::vector<std::pair<std::string, std::string>> candidates;
    const std::string next_path = memory.ReadCString(chain.next_path_addr, kNextLoadPathSize);
    if (!next_path.empty()) {
        const std::string next_argv = memory.ReadCString(chain.next_argv_addr, kNextLoadArgvSize);
        const std::string host_path = HostPathOfSdmc(next_path);
        if (host_path.empty()) {
            LOG_ERROR(Loader, "Homebrew asked to load {}, which is not on the SD card", next_path);
        } else {
            candidates.emplace_back(host_path,
                                    next_argv.empty() ? QuoteArgument(next_path) : next_argv);
        }
    }
    if (chain.current_path != chain.menu_path) {
        candidates.emplace_back(chain.menu_path, chain.menu_argv);
    }
    if (candidates.empty()) {
        return false;
    }
    if (Settings::IsNceEnabled()) {
        LOG_WARNING(Loader, "Loading the next NRO in place is not supported with NCE");
        return false;
    }

    std::optional<HomebrewImage> image;
    std::string path;
    for (const auto& [candidate, candidate_argv] : candidates) {
        const auto file = system.GetFilesystem()->OpenFile(candidate, FileSys::OpenMode::Read);
        image = file ? BuildHomebrewImage(file->ReadAllBytes(), candidate_argv) : std::nullopt;
        if (image) {
            path = candidate;
            break;
        }
        LOG_ERROR(Loader, "Unable to load the homebrew NRO {}", candidate);
    }
    if (!image) {
        return false;
    }
    LOG_INFO(Loader, "Homebrew loader: running {} with argv {}", path,
             std::string_view{image->argv}.substr(0, image->argv.size() - 1));

    const u32 main_thread_handle =
        memory.Read32(chain.config_addr + offsetof(ConfigEntry, value));
    auto& page_table = process.GetPageTable();

    // Let the GPU thread run what the previous NRO queued: its unmaps flush GPU data back into
    // the memory it used.
    std::promise<void> gpu_done;
    system.GPU().RunInOrder([&gpu_done] { gpu_done.set_value(); });
    gpu_done.get_future().wait();

    // As ounce-hbloader: unmap the JIT code the previous NRO left, its source pages are in the
    // heap the next one gets.
    if (const size_t released = Service::RO::ReleaseHomebrewJitCode(process); released != 0) {
        LOG_INFO(Loader, "Released {} JIT code mappings of the previous NRO", released);
    }
    if (chain.ldr_ro_handle == 0) {
        chain.ldr_ro_handle = Service::RO::OpenHomebrewSession(system, process);
    }

    // Unmap the previous NRO, unless it is the one the process started with.
    if (chain.alias_source != 0) {
        const Result result =
            page_table.UnmapCodeMemory(chain.base, chain.alias_source, chain.image_size);
        if (R_FAILED(result)) {
            LOG_WARNING(Loader, "Unable to unmap the previous NRO at {:#x}+{:#x}: {:#x}",
                        chain.base, chain.image_size, result.raw);
        }
    }
    for (size_t core = 0; core < Core::Hardware::NUM_CPU_CORES; ++core) {
        if (auto* const arm = process.GetArmInterface(core); arm != nullptr) {
            arm->InvalidateCacheRange(chain.base, chain.image_size);
        }
    }

    // As ounce-hbloader: load the NRO into the top of the heap, alias that as code and give the
    // rest of the heap to it. The process has no memory left for new pages once libnx has sized
    // its heap, and the first mapping cannot be reused: crt0 locks its RELRO read-only.
    const u64 heap_address = GetInteger(page_table.GetHeapRegionStart());
    const u64 heap_size = page_table.GetCurrentHeapSize();
    const size_t image_size = image->memory.size();
    const auto find_free = [&]() -> u64 {
        const u64 region = GetInteger(page_table.GetAliasCodeRegionStart());
        const u64 region_end = region + page_table.GetAliasCodeRegionSize();
        for (u64 address = region; address < region_end;) {
            Kernel::KMemoryInfo info{};
            Kernel::Svc::PageInfo page_info{};
            if (R_FAILED(page_table.QueryInfo(std::addressof(info), std::addressof(page_info),
                                              address))) {
                break;
            }
            const u64 end = (std::min)(region_end, info.m_address + info.m_size);
            if (info.m_state == Kernel::KMemoryState::Free && end - address >= image_size) {
                return address;
            }
            address = end;
        }
        return 0;
    };
    const u64 base = heap_size > image_size ? find_free() : 0;
    const u64 alias_source = heap_address + heap_size - image_size;
    const Result result = base == 0 ? Kernel::ResultOutOfMemory
                                    : page_table.MapCodeMemory(base, alias_source, image_size);
    if (R_FAILED(result)) {
        LOG_ERROR(Loader, "Unable to map {:#x} bytes for {} (heap {:#x}): {:#x}", image_size, path,
                  heap_size, result.raw);
        g_chain.reset();
        return false;
    }
    chain.current_path = path;
    chain.alias_source = alias_source;
    SetChainImage(chain, *image, base, image_size);
    image->codeset.memory = std::move(image->memory);
    process.LoadModule(system.Kernel(), std::move(image->codeset), Kernel::KProcessAddress{base});
    for (size_t core = 0; core < Core::Hardware::NUM_CPU_CORES; ++core) {
        if (auto* const arm = process.GetArmInterface(core); arm != nullptr) {
            arm->InvalidateCacheRange(base, image_size);
        }
    }
    WriteLoaderConfig(process, *image, base, main_thread_handle, chain.ldr_ro_handle,
                      heap_address, heap_size - image_size);

    // Enter it on this thread, with the stack libnx restored before returning to the loader.
    auto* const arm = process.GetArmInterface(system.Kernel().CurrentPhysicalCoreIndex());
    Kernel::Svc::ThreadContext context{};
    arm->GetContext(context);
    context.pc = base;
    context.lr = image->exit_process_offset ? base + *image->exit_process_offset : 0;
    context.sp &= ~u64{0xF};
    arm->SetContext(context);
    args[0] = chain.config_addr;
    args[1] = UINT64_MAX;
    return true;
}

bool AppLoader_NRO::LoadNro(Core::System& system, Kernel::KProcess& process,
                            const FileSys::VfsFile& nro_file) {
    return LoadNroImpl(system, process, nro_file.ReadAllBytes(), nro_file.GetFullPath());
}

AppLoader_NRO::LoadResult AppLoader_NRO::Load(Kernel::KProcess& process, Core::System& system) {
    if (is_loaded) {
        return {ResultStatus::ErrorAlreadyLoaded, {}};
    }

    if (!LoadNro(system, process, *file)) {
        return {ResultStatus::ErrorLoadingNRO, {}};
    }

    u64 program_id{};
    ReadProgramId(program_id);
    system.GetFileSystemController().RegisterProcess(
        process.GetProcessId(), program_id,
        std::make_unique<FileSys::RomFSFactory>(*this, system.GetContentProvider(),
                                                system.GetFileSystemController()));

    is_loaded = true;
    return {ResultStatus::Success, LoadParameters{Kernel::KThread::DefaultThreadPriority,
                                                  Core::Memory::DEFAULT_STACK_SIZE}};
}

ResultStatus AppLoader_NRO::ReadIcon(std::vector<u8>& buffer) {
    if (icon_data.empty()) {
        return ResultStatus::ErrorNoIcon;
    }

    buffer = icon_data;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NRO::ReadProgramId(u64& out_program_id) {
    if (nacp == nullptr) {
        return ResultStatus::ErrorNoControl;
    }

    out_program_id = nacp->GetTitleId();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NRO::ReadRomFS(FileSys::VirtualFile& dir) {
    if (romfs == nullptr) {
        return ResultStatus::ErrorNoRomFS;
    }

    dir = romfs;
    return ResultStatus::Success;
}

ResultStatus AppLoader_NRO::ReadTitle(std::string& title) {
    if (nacp == nullptr) {
        return ResultStatus::ErrorNoControl;
    }

    title = nacp->GetApplicationName();
    return ResultStatus::Success;
}

ResultStatus AppLoader_NRO::ReadControlData(FileSys::NACP& control) {
    if (nacp == nullptr) {
        return ResultStatus::ErrorNoControl;
    }

    control = *nacp;
    return ResultStatus::Success;
}

bool AppLoader_NRO::IsRomFSUpdatable() const {
    return false;
}

} // namespace Loader
