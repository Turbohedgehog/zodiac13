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

#include <net_module/clock_sync.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace z13::net {

int64_t TakeClockAdjustment(ClockSync& sync, const NetTuning& tuning, uint64_t tick) {
  if (!sync.offset_ticks) {
    return 0;
  }
  const int64_t offset = *sync.offset_ticks;
  int64_t adjust {};
  if (std::abs(offset) >= tuning.clock_jump_threshold_ticks) {
    adjust = offset;
  } else if (std::abs(offset) >= tuning.clock_catch_up_threshold_ticks && tick % tuning.clock_catch_up_every_ticks == 0) {
    adjust = offset > 0 ? 1 : -1;
  }
  *sync.offset_ticks -= adjust;
  if (sync.ping_sent_tick) {
    sync.adjusted_ticks_in_flight += adjust;
  }
  return adjust;
}

void ApplyPong(ClockSync& sync, const NetTuning& tuning, uint64_t sent_tick, uint64_t server_tick, uint64_t received_tick) {
  if (sync.ping_sent_tick != sent_tick || received_tick < sent_tick) {
    return;  // not the Ping in flight, or a reply claiming to predate it
  }
  sync.ping_sent_tick.reset();
  const int64_t adjusted_ticks = std::exchange(sync.adjusted_ticks_in_flight, 0);

  const int64_t rtt = std::max<int64_t>(static_cast<int64_t>(received_tick - sent_tick) - adjusted_ticks, 0);
  sync.rtt_ticks = rtt;
  // Assumes a symmetric path; asymmetry only costs extra rollbacks.
  const int64_t sample =
      static_cast<int64_t>(server_tick) + rtt / 2 - static_cast<int64_t>(received_tick);

  // Asymmetry skews a sample by at most rtt / 2; a bigger miss is a clock that moved.
  if (!sync.offset_ticks ||
      std::abs(sample - *sync.offset_ticks) >= std::max(tuning.clock_jump_threshold_ticks, rtt)) {
    sync.offset_ticks = sample;
    return;
  }
  const double smoothed = tuning.clock_offset_smoothing * static_cast<double>(sample) +
                          (1. - tuning.clock_offset_smoothing) * static_cast<double>(*sync.offset_ticks);
  sync.offset_ticks = static_cast<int64_t>(std::llround(smoothed));
}

}  // namespace z13::net
