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
#include <optional>
#include <utility>
#include <vector>

#include <boost/container/flat_map.hpp>

#include <lib_core/bounded_history.h>

#include <z13/components/input.h>

namespace z13::gameplay {

// One action's value at one tick, for one player -- covers movement, look, build,
// destroy, toggle the same way, so a new action never needs its own record type.
struct PlayerActionRecord {
  uint64_t tick {};
  uint32_t player_id {};
  z13::input::ActionInfo::IdType action_id {};
  float value {};
};

struct OutgoingCommands {
  using Singleton = void;
  using SessionScoped = void;
  std::vector<PlayerActionRecord> records;
};

// Not ActionListener's prev_value: RemoteActionFramePhase overwrites it every tick.
struct LastRecordedActionValues {
  using Singleton = void;
  using SessionScoped = void;
  boost::container::flat_map<z13::input::ActionInfo::IdType, float> values;
};

// Sorted by (tick, player_id, action_id), a key every participant derives itself.
struct ScheduledCommands {
  using Singleton = void;
  using SessionScoped = void;
  std::vector<PlayerActionRecord> records;
};

// A rolling log of PlayerActionRecord; runtime only, never itself part of the state it
// logs.
struct PlayerActionLog {
  using Singleton = void;
  using SessionScoped = void;
  BoundedHistory<PlayerActionRecord> log;
  // Ticks older than this may already be pruned from `log` -- distinguishes "nothing
  // changed" from "that history was discarded", which look the same in `log` alone.
  uint64_t retained_since_tick {};
};

struct RemoteActionState {
  using Singleton = void;
  boost::container::flat_map<std::pair<uint32_t, z13::input::ActionInfo::IdType>, float> current_values;
  // Nullopt until the first run; anything but synced_tick + 1 means rebuild from the log.
  std::optional<uint64_t> synced_tick;
};

}  // namespace z13::gameplay
