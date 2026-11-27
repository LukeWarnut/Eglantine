// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2018 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <memory>
#include <mutex>
#include "common/assert.h"
#include "common/common_types.h"
#include "common/logging.h"
#include "core/file_sys/common_funcs.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/nca_metadata.h"
#include "core/file_sys/patch_manager.h"
#include "core/file_sys/registered_cache.h"
#include "core/file_sys/romfs_factory.h"
#include "core/hle/kernel/k_process.h"
#include "core/hle/service/filesystem/filesystem.h"
#include "core/loader/loader.h"

namespace FileSys {

RomFSFactory::RomFSFactory(Loader::AppLoader& app_loader, ContentProvider& provider,
                           Service::FileSystem::FileSystemController& controller)
    : content_provider{provider}, filesystem_controller{controller} {
    // Load the RomFS from the app
    if (app_loader.ReadRomFS(file) != Loader::ResultStatus::Success) {
        LOG_WARNING(Service_FS, "Unable to read base RomFS");
    }

    updatable = app_loader.IsRomFSUpdatable();

    // Containers with several programs serve the others to OpenDataStorageWithProgramIndex.
    std::vector<u64> program_ids;
    if (app_loader.ReadProgramIds(program_ids) == Loader::ResultStatus::Success &&
        program_ids.size() > 1) {
        for (const u64 id : program_ids) {
            VirtualFile romfs;
            if (app_loader.ReadProgramRomFS(id, romfs) == Loader::ResultStatus::Success &&
                romfs != nullptr) {
                program_romfs.emplace_back(id, std::move(romfs));
            }
        }
    }
}

RomFSFactory::~RomFSFactory() = default;

void RomFSFactory::SetPackedUpdate(VirtualFile update_raw_file) {
    packed_update_raw = std::move(update_raw_file);
}

VirtualFile RomFSFactory::OpenCurrentProcess(u64 current_process_title_id) const {
    if (!updatable) {
        // LOCAL TEST (Zelda): a RomFS with the update already merged (NUP containers) still takes
        // LayeredFS mods (romfs folders of the title's mods); without, they were never applied.
        // No update is looked up (no base NCA). Built once.
        std::scoped_lock lock{layered_mutex};
        if (!layered_file) {
            const PatchManager patch_manager{current_process_title_id, filesystem_controller,
                                             content_provider};
            layered_file = patch_manager.PatchRomFS(nullptr, file, ContentRecordType::Program,
                                                    nullptr);
            if (!layered_file) {
                layered_file = file;
            }
        }
        return layered_file;
    }

    const auto type = ContentRecordType::Program;
    const auto nca = content_provider.GetEntry(current_process_title_id, type);
    const PatchManager patch_manager{current_process_title_id, filesystem_controller,
                                     content_provider};
    return patch_manager.PatchRomFS(nca.get(), file, ContentRecordType::Program, packed_update_raw);
}

VirtualFile RomFSFactory::OpenPatchedRomFS(u64 title_id, ContentRecordType type) const {
    auto nca = content_provider.GetEntry(title_id, type);

    if (nca == nullptr) {
        return nullptr;
    }

    const PatchManager patch_manager{title_id, filesystem_controller, content_provider};

    return patch_manager.PatchRomFS(nca.get(), nca->GetRomFS(), type);
}

VirtualFile RomFSFactory::OpenPatchedRomFSWithProgramIndex(u64 title_id, u8 program_index,
                                                           ContentRecordType type) const {
    const auto res_title_id = GetBaseTitleIDWithProgramIndex(title_id, program_index);
    if (type != ContentRecordType::Program) {
        return OpenPatchedRomFS(res_title_id, type);
    }

    // A program of the running game file (multi-program NUP or .nx), else installed content.
    const auto open_program = [this, type](u64 id) -> VirtualFile {
        for (const auto& [program, romfs] : program_romfs) {
            if (program == id) {
                // Updates are already applied by the container; this adds LayeredFS mods.
                const PatchManager patch_manager{id, filesystem_controller, content_provider};
                auto layered = patch_manager.PatchRomFS(nullptr, romfs, type, nullptr);
                return layered != nullptr ? layered : romfs;
            }
        }
        return OpenPatchedRomFS(id, type);
    };
    if (auto romfs = open_program(res_title_id)) {
        return romfs;
    }

    // A Switch 2 Edition is program index 1 or later of its application; the index 0 it asks
    // for is the Switch 1 game (as Lichii and Orchid map it).
    const u64 switch1_id = GetSwitch1ApplicationID(res_title_id);
    if (program_index != 0 || (title_id & 0xF) == 0 || switch1_id == res_title_id) {
        return nullptr;
    }
    if (auto romfs = open_program(switch1_id)) {
        LOG_INFO(Service_FS,
                 "Program {:016X} (index 0) is not present: serving the Switch 1 application "
                 "{:016X} to the Switch 2 Edition {:016X}",
                 res_title_id, switch1_id, title_id);
        return romfs;
    }
    // Dumps of a Switch 2 Edition usually carry the game data in the edition's own RomFS.
    auto own = OpenCurrentProcess(title_id);
    if (own != nullptr) {
        LOG_WARNING(Service_FS,
                    "Program {:016X} (index 0) and the Switch 1 application {:016X} are not "
                    "present: serving the running program {:016X}'s own RomFS instead. Files "
                    "only the Switch 1 game has will be missing",
                    res_title_id, switch1_id, title_id);
    }
    return own;
}

namespace {
// One global set, cleared on every boot and shutdown; only one title runs at a time.
std::mutex loose_aoc_mutex;
std::vector<std::pair<u64, VirtualFile>> loose_aoc;
} // namespace

void SetLooseAOC(std::vector<std::pair<u64, VirtualFile>> entries) {
    std::scoped_lock lk{loose_aoc_mutex};
    loose_aoc = std::move(entries);
}

std::vector<u64> GetLooseAOCTitleIDs() {
    std::scoped_lock lk{loose_aoc_mutex};
    std::vector<u64> ids;
    for (const auto& [id, romfs] : loose_aoc) {
        ids.push_back(id);
    }
    return ids;
}

VirtualFile RomFSFactory::Open(u64 title_id, StorageId storage, ContentRecordType type) const {
    if (type == ContentRecordType::Data) {
        std::scoped_lock lk{loose_aoc_mutex};
        for (const auto& [id, romfs] : loose_aoc) {
            if (id == title_id) {
                LOG_INFO(Service_FS, "Serving loose add-on content {:016X}", title_id);
                return romfs;
            }
        }
    }

    const std::shared_ptr<NCA> res = GetEntry(title_id, storage, type);
    if (res == nullptr) {
        return nullptr;
    }

    return res->GetRomFS();
}

std::shared_ptr<NCA> RomFSFactory::GetEntry(u64 title_id, StorageId storage,
                                            ContentRecordType type) const {
    switch (storage) {
    case StorageId::None:
        return content_provider.GetEntry(title_id, type);
    case StorageId::NandSystem:
        return filesystem_controller.GetSystemNANDContents()->GetEntry(title_id, type);
    case StorageId::NandUser:
        return filesystem_controller.GetUserNANDContents()->GetEntry(title_id, type);
    case StorageId::SdCard:
        return filesystem_controller.GetSDMCContents()->GetEntry(title_id, type);
    case StorageId::Host:
    case StorageId::GameCard:
    default:
        UNIMPLEMENTED_MSG("Unimplemented storage_id={:02X}", static_cast<u8>(storage));
        return nullptr;
    }
}

} // namespace FileSys
