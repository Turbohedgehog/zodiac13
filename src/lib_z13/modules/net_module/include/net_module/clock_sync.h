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

#include <lib_core/state/rollback.h>
#include <z13_settings/net_tuning.h>

namespace z13::net {

struct ClockSync {
  using Singleton = void;
  using SessionScoped = void;
  std::optional<int64_t> offset_ticks;
  std::optional<uint64_t> ping_sent_tick;
  std::optional<int64_t> rtt_ticks;
  // ClockAdjustRequest::applied when the Ping went out.
  int64_t applied_at_ping {};
};

// The tuning a joining client gets: the lead a server with no player of its own can afford
// replaces the listen server's.
NetTuning TuningForClients(const NetTuning& tuning, bool dedicated);

// Already taken off the offset.
int64_t TakeClockAdjustment(ClockSync& sync, const NetTuning& tuning, uint64_t tick);

// Ignores a Pong that doesn't match the Ping in flight. The clock is taken as if the adjustment
// still to run had run: a Pong handled mid-jump would otherwise ask for the rest again.
void ApplyPong(ClockSync& sync, const NetTuning& tuning, uint64_t sent_tick, uint64_t server_tick, uint64_t received_tick,
               const z13::flecs_tools::ClockAdjustRequest& adjust = {});

}  // namespace z13::net
