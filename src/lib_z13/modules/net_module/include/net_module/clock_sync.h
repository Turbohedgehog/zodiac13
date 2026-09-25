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

namespace z13::net {

// How far ahead of its own clock a client schedules its commands. Not a correctness
// threshold -- a command that still lands late is fixed by rollback; lowering this
// trades input latency for more rollbacks.
inline constexpr int64_t kInputDelayTicks = 12;

// Weight of a fresh offset sample in the running estimate. Low enough that one delayed
// Pong doesn't shift the schedule of every command after it.
inline constexpr double kClockOffsetSmoothing = 0.25;

// A client's running estimate of how far the server's clock is ahead of its own, and
// the Ping it is currently waiting on. Runtime only: rebuilt from Pongs every session.
struct ClockSync {
  using Singleton = void;
  // Nullopt until the first Pong: until then a client schedules off kInputDelayTicks
  // alone and leans on rollback for the difference.
  std::optional<int64_t> offset_ticks;
  std::optional<uint64_t> ping_sent_tick;
  // Last measured round trip, for the metrics the plan's rollback stage wants.
  std::optional<int64_t> rtt_ticks;
};

// The tick a command issued on `client_tick` is meant to apply on, for everyone. Pure:
// the client stamps this onto the wire so the server never has to know its offset.
uint64_t ScheduleTick(uint64_t client_tick, std::optional<int64_t> offset_ticks);

// Folds one Pong into `sync`. `sent_tick`/`received_tick` are the client's own clock
// when it sent the Ping and when the reply arrived; `server_tick` is what the server
// stamped on the way out. Ignores a reply that doesn't match the Ping in flight.
void ApplyPong(ClockSync& sync, uint64_t sent_tick, uint64_t server_tick, uint64_t received_tick);

}  // namespace z13::net
