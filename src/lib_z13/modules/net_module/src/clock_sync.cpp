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

namespace z13::net {

uint64_t ScheduleTick(uint64_t client_tick, std::optional<int64_t> offset_ticks) {
  const int64_t scheduled =
      static_cast<int64_t>(client_tick) + offset_ticks.value_or(0) + kInputDelayTicks;
  // A wildly negative offset would otherwise wrap around into the far future.
  return static_cast<uint64_t>(std::max<int64_t>(scheduled, 0));
}

void ApplyPong(ClockSync& sync, uint64_t sent_tick, uint64_t server_tick, uint64_t received_tick) {
  if (sync.ping_sent_tick != sent_tick || received_tick < sent_tick) {
    return;  // not the Ping in flight, or a reply claiming to predate it
  }
  sync.ping_sent_tick.reset();

  const int64_t rtt = static_cast<int64_t>(received_tick - sent_tick);
  sync.rtt_ticks = rtt;
  // Assumes a symmetric path: half the round trip is what the server's stamp aged in
  // flight. An asymmetric one biases the estimate, which only costs extra rollbacks.
  const int64_t sample =
      static_cast<int64_t>(server_tick) + rtt / 2 - static_cast<int64_t>(received_tick);

  if (!sync.offset_ticks) {
    sync.offset_ticks = sample;
    return;
  }
  const double smoothed = kClockOffsetSmoothing * static_cast<double>(sample) +
                          (1. - kClockOffsetSmoothing) * static_cast<double>(*sync.offset_ticks);
  sync.offset_ticks = static_cast<int64_t>(std::llround(smoothed));
}

}  // namespace z13::net
