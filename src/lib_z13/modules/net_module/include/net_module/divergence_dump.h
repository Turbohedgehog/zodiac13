/*
 * Copyright 2026 Ivan Kulenko / Zodiac13
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://apache.org
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <flecs.h>

namespace z13::net {

// Where a session writes the snapshots of a diverged tick, when set (z13.py --net-sim sets it).
constexpr std::string_view kDivergenceDirectoryVariable = "Z13_DIVERGENCE_DIR";

constexpr std::string_view kActionLogExtension = ".actions.txt";

// Writes the snapshot of `tick` as JSON named after `side` into $Z13_DIVERGENCE_DIR, and the
// action log beside it. Returns the snapshot's file, or nothing when the variable isn't set.
std::expected<std::optional<std::filesystem::path>, std::string> DumpDivergedTick(
    flecs::world world, uint64_t tick, std::string_view side);

}  // namespace z13::net
