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
#include <string>

#include <lib_core/core_types.h>
#include <lib_core/world_snapshot_history.h>

namespace z13::state {

// Client-side rollback: restores `world` to `snapshot`, then replays every
// PlayerActionLog record in (snapshot.tick, target_tick] via normal world.progress(),
// feeding ActionListener.action_values the same way live input does -- doesn't know
// what any action_id means. Reads the log from `world` itself, not a different world's.
class Replay {
 public:
  static void Register(flecs::world& world);

  static std::expected<void, std::string> Run(
      flecs::world& world, const z13::flecs_tools::TimestampedSnapshot& snapshot, uint64_t target_tick);
};

}  // namespace z13::state
