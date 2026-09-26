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

// What the local player just did, stamped with this participant's own tick. Only
// net_module knows how to turn that into the tick everyone applies it on, so the
// recorder stops here; whoever consumes it clears it.
struct OutgoingCommands {
  using Singleton = void;
  std::vector<PlayerActionRecord> records;
};

// Commands the server has put in order, waiting for the tick they apply on. Kept sorted
// by (tick, player_id, action_id): every participant can derive that key on its own, so
// nobody has to exchange sequence numbers to agree on the order.
struct ScheduledCommands {
  using Singleton = void;
  std::vector<PlayerActionRecord> records;
};

// A rolling log of PlayerActionRecord; runtime only, never itself part of the state it
// logs.
struct PlayerActionLog {
  using Singleton = void;
  BoundedHistory<PlayerActionRecord> log;
  // Ticks older than this may already be pruned from `log` -- distinguishes "nothing
  // changed" from "that history was discarded", which look the same in `log` alone.
  uint64_t retained_since_tick {};
};

// Held (player, action) values RemoteActionFramePhase injects into ActionListener,
// rebuilt from PlayerActionLog whenever the tick isn't a plain continuation of the last
// one synced (a rollback, or the very first tick).
struct RemoteActionState {
  using Singleton = void;
  boost::container::flat_map<std::pair<uint32_t, z13::input::ActionInfo::IdType>, float> current_values;
  // Nullopt until the first run; anything but synced_tick + 1 means rebuild from the log.
  std::optional<uint64_t> synced_tick;
};

}  // namespace z13::gameplay
