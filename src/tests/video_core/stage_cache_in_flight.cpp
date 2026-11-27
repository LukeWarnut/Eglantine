// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <chrono>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "common/common_types.h"
#include "video_core/stage_cache.h"

using VideoCommon::StageCache;
using namespace std::chrono_literals;

TEST_CASE("StageCache in-flight: a claimed stage is claimed once", "[stage_cache]") {
    StageCache cache;
    const std::array<u64, 2> hashes{1, 2};
    StageCache::InFlight first = cache.ClaimInFlight(hashes);
    // Nothing left to claim: the other thread would wait instead.
    REQUIRE(cache.WaitForInFlight(std::array<u64, 1>{2}, 1ms));
    StageCache::InFlight second = cache.ClaimInFlight(std::array<u64, 2>{2, 3});
    first.Release();
    REQUIRE(!cache.WaitForInFlight(hashes, 1ms));
    // 3 is still held by the second claim.
    REQUIRE(cache.WaitForInFlight(std::array<u64, 1>{3}, 1ms));
}

TEST_CASE("StageCache in-flight: release wakes a waiter", "[stage_cache]") {
    StageCache cache;
    const std::array<u64, 1> hashes{7};
    auto claim = std::make_unique<StageCache::InFlight>(cache.ClaimInFlight(hashes));
    std::thread releaser{[&] {
        std::this_thread::sleep_for(20ms);
        claim.reset();
    }};
    const auto begin = std::chrono::steady_clock::now();
    REQUIRE(cache.WaitForInFlight(hashes, 5s));
    REQUIRE(std::chrono::steady_clock::now() - begin < 4s);
    releaser.join();
    REQUIRE(!cache.WaitForInFlight(hashes, 1ms));
}

TEST_CASE("StageCache in-flight: a moved claim is released once", "[stage_cache]") {
    StageCache cache;
    const std::array<u64, 1> hashes{9};
    StageCache::InFlight moved;
    {
        StageCache::InFlight claim = cache.ClaimInFlight(hashes);
        moved = std::move(claim);
    }
    REQUIRE(cache.WaitForInFlight(hashes, 1ms));
    moved.Release();
    REQUIRE(!cache.WaitForInFlight(hashes, 1ms));
}
