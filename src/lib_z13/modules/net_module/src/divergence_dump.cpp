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
#include <string>

#include <lib_core/state/world_json_store.h>
#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/utils/file_io.h>
#include <z13/components/player_action.h>

namespace z13::net {

namespace ft = z13::flecs_tools;

// The kept action log, one record a line: what the replays of the diverged tick played.
std::string DescribeActionLog(flecs::world world) {
  std::string text;
  for (const z13::gameplay::PlayerActionRecord& record : world.get<z13::gameplay::PlayerActionLog>().log.Entries()) {
    text += std::format("{} player {} action {} = {}\n", record.tick, record.player_id, record.action_id, record.value);
  }
  return text;
}

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
  const std::filesystem::path actions_path = std::filesystem::path(path).replace_extension(kActionLogExtension);
  if (auto written = z13::WriteFile(path, *json).and_then([&] {
        return z13::WriteFile(actions_path, DescribeActionLog(world));
      });
      !written) {
    return std::unexpected(std::move(written.error()));
  }
  return path;
}

}  // namespace z13::net
