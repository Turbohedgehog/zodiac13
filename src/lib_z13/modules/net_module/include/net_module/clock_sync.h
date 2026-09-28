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

// Late commands are fixed by rollback; this trades input latency for fewer rollbacks.
inline constexpr int64_t kInputDelayTicks = 12;

inline constexpr uint64_t kNetSendIntervalTicks = 3;

inline constexpr uint64_t kMaxScheduleAheadTicks = 8 * static_cast<uint64_t>(kInputDelayTicks);

inline constexpr uint64_t kMaxLateTicks = kMaxScheduleAheadTicks;

inline constexpr uint64_t kSessionEventDelayTicks = static_cast<uint64_t>(kInputDelayTicks);

inline constexpr double kClockOffsetSmoothing = 0.25;

struct ClockSync {
  using Singleton = void;
  using SessionScoped = void;
  std::optional<int64_t> offset_ticks;
  std::optional<uint64_t> ping_sent_tick;
  std::optional<int64_t> rtt_ticks;
};

uint64_t ScheduleTick(uint64_t client_tick, std::optional<int64_t> offset_ticks);

// Ignores a Pong that doesn't match the Ping in flight.
void ApplyPong(ClockSync& sync, uint64_t sent_tick, uint64_t server_tick, uint64_t received_tick);

}  // namespace z13::net
