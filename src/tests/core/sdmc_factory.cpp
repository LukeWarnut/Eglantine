// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/literals.h"
#include "core/file_sys/sdmc_factory.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "core/file_sys/vfs/vfs_vector.h"
#include "tests/temp_path.h"

namespace {

using namespace Common::Literals;

TEST_CASE("SDMCFactory space queries use host filesystem without directory walk",
          "[core][file_sys][sdmc]") {
    Tests::TempDir temp{"sdmc-space"};
    FileSys::RealVfsFilesystem vfs;
    auto sd_dir = vfs.OpenDirectory(Common::FS::PathToUTF8String(temp.Path()),
                                    FileSys::OpenMode::ReadWrite);
    REQUIRE(sd_dir != nullptr);

    FileSys::SDMCFactory factory{sd_dir, nullptr};
    const u64 expected_total = Common::FS::GetTotalSpaceSize(temp.Path());
    const u64 total = factory.GetSDMCTotalSpace();
    const u64 free_space = factory.GetSDMCFreeSpace();

    REQUIRE(expected_total > 0);
    CHECK(total == expected_total);
    CHECK(free_space > 0);
    CHECK(free_space <= total);
}

TEST_CASE("SDMCFactory falls back for virtual directories", "[core][file_sys][sdmc]") {
    SECTION("Empty in-memory directory uses 4 GiB chunk sizing") {
        auto sd_dir = std::make_shared<FileSys::VectorVfsDirectory>(
            std::vector<FileSys::VirtualFile>{}, std::vector<FileSys::VirtualDir>{}, "sdmc");
        FileSys::SDMCFactory factory{sd_dir, nullptr};
        CHECK(factory.GetSDMCTotalSpace() == 4_GiB);
        CHECK(factory.GetSDMCFreeSpace() == 4_GiB);
    }

    SECTION("Populated in-memory directory subtracts virtual size") {
        auto file = std::make_shared<FileSys::VectorVfsFile>(std::vector<u8>(4096, 0), "blob.bin");
        auto sd_dir = std::make_shared<FileSys::VectorVfsDirectory>(
            std::vector<FileSys::VirtualFile>{file}, std::vector<FileSys::VirtualDir>{}, "sdmc");
        FileSys::SDMCFactory factory{sd_dir, nullptr};

        CHECK(factory.GetSDMCTotalSpace() == 4_GiB);
        CHECK(factory.GetSDMCFreeSpace() == 4_GiB - 4096);
    }
}

} // namespace
