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

#include <chrono>
#include <cstdint>
#include <utility>

namespace z13 {

// Keeps simulation ticks on wall-clock time: a slow frame is made up by the next ticks
// running back to back, so a peer's clock doesn't drift behind the others'.
class TickPacer {
 public:
  using Clock = std::chrono::steady_clock;

  // A backlog of `max_backlog` ticks or more is a hitch (a breakpoint, a stalled disk):
  // dropped, not replayed in a burst.
  TickPacer(Clock::duration tick_duration, Clock::time_point start, uint64_t max_backlog);

  // True when a tick is due at `now`; consumes it.
  bool TakeTick(Clock::time_point now);

  // The ticks hitches dropped since the last call.
  uint64_t TakeDroppedTicks() { return std::exchange(dropped_ticks_, 0); }

  // Keeps the schedule, so a hitch before the change is still counted.
  void SetPace(Clock::duration tick_duration, uint64_t max_backlog);

  Clock::time_point NextTickTime() const { return next_tick_; }

 private:
  Clock::duration tick_duration_;
  Clock::time_point next_tick_;
  uint64_t max_backlog_;
  uint64_t dropped_ticks_ {};
};

}  // namespace z13
