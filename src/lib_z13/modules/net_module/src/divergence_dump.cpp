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

#include <net_module/divergence_dump.h>

#include <algorithm>
#include <cstdlib>
#include <format>

#include <lib_core/state/world_json_store.h>
#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/utils/file_io.h>

namespace z13::net {

namespace ft = z13::flecs_tools;

std::expected<std::optional<std::filesystem::path>, std::string> DumpDivergedTick(
    flecs::world world, uint64_t tick, std::string_view side) {
  const char* directory = std::getenv(kDivergenceDirectoryVariable.data());
  if (directory == nullptr) {
    return std::nullopt;
  }
  const auto& entries = world.get<ft::WorldSnapshotHistory>().history.Entries();
  const auto entry = std::ranges::find(entries, tick, &ft::TimestampedSnapshot::tick);
  if (entry == entries.end()) {
    return std::unexpected(std::format("no snapshot of tick {} is kept any more", tick));
  }
  auto json = ft::WorldJsonStore::ToJson(world, entry->snapshot);
  if (!json) {
    return std::unexpected(std::move(json.error()));
  }
  const std::filesystem::path path = std::filesystem::path(directory) / std::format("divergence_{}_{}.json", tick, side);
  if (auto written = z13::WriteFile(path, *json); !written) {
    return std::unexpected(std::move(written.error()));
  }
  return path;
}

}  // namespace z13::net
