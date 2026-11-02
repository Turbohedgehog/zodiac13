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
#include <format>
#include <string>
#include <string_view>

namespace z13::gameplay {

// Who spawns something and when. What is spawned is named by these, so every peer that simulates the spawn names
// it alike; a counter kept by each peer would lag for one that doesn't simulate another player's builds.
struct SpawnStamp {
  uint64_t tick {};
  uint32_t player_id {};
};

// The spawner of what the scene makes, in no player's name.
constexpr uint32_t kSceneSpawner = 0xFFFF;

constexpr int kSpawnPlayerBits = 16;
constexpr int kSpawnIndexBits = 16;
constexpr uint64_t kSpawnPlayerMask = (uint64_t {1} << kSpawnPlayerBits) - 1;
constexpr uint64_t kSpawnIndexMask = (uint64_t {1} << kSpawnIndexBits) - 1;

// The id of the `index`-th thing the stamp spawns: the tick in the high bits, so that a later spawn has a greater
// id, then the player and the index. A tick past 32 bits wraps, a player past 16 and an index past 16 are cut.
constexpr uint64_t SpawnId(const SpawnStamp& stamp, uint32_t index) {
  return (stamp.tick << (kSpawnPlayerBits + kSpawnIndexBits)) |
      ((stamp.player_id & kSpawnPlayerMask) << kSpawnIndexBits) | (index & kSpawnIndexMask);
}

inline std::string SpawnName(std::string_view prefix, const SpawnStamp& stamp, uint32_t index) {
  return std::format("{}_{}_{}_{}", prefix, stamp.tick, stamp.player_id, index);
}

// Numbers the things one player spawns in one tick, in the order they are spawned. Shared by everything that
// spawns for the player in the tick: the entities of a deferred system are not there to be found by name.
class SpawnNamer {
 public:
  explicit SpawnNamer(const SpawnStamp& stamp) : stamp_(stamp) {}

  uint64_t NextId() { return SpawnId(stamp_, next_++); }
  std::string NextName(std::string_view prefix) { return SpawnName(prefix, stamp_, next_++); }

 private:
  SpawnStamp stamp_;
  uint32_t next_ {};
};

}  // namespace z13::gameplay
