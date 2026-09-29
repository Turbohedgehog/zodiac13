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

inline constexpr uint64_t kNetSendIntervalTicks = 3;

inline constexpr uint64_t kMaxLateTicks = 96;

// A synced client is ahead only by estimate noise.
inline constexpr uint64_t kMaxScheduleAheadTicks = 30;

// Not input, so it can wait for peers to receive it in time.
inline constexpr uint64_t kSessionEventDelayTicks = 12;

inline constexpr uint64_t kRollbackSnapshotsPerInterval = 4;

inline constexpr double kClockOffsetSmoothing = 0.25;

// Past the jump threshold (a hitch) the whole gap closes at once, before commands leave
// the server's window.
inline constexpr int64_t kClockCatchUpThresholdTicks = 2;
inline constexpr uint64_t kClockCatchUpEveryTicks = 4;
inline constexpr int64_t kClockJumpThresholdTicks = 15;

struct ClockSync {
  using Singleton = void;
  using SessionScoped = void;
  std::optional<int64_t> offset_ticks;
  std::optional<uint64_t> ping_sent_tick;
  std::optional<int64_t> rtt_ticks;
  // Extra minus skipped ticks since the Ping went out; not round trip.
  int64_t adjusted_ticks_in_flight {};
};

// Already taken off the offset.
int64_t TakeClockAdjustment(ClockSync& sync, uint64_t tick);

// Ignores a Pong that doesn't match the Ping in flight.
void ApplyPong(ClockSync& sync, uint64_t sent_tick, uint64_t server_tick, uint64_t received_tick);

}  // namespace z13::net
