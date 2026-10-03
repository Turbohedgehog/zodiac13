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

#include <lib_core/time/tick_pacer.h>

namespace z13 {

TickPacer::TickPacer(Clock::duration tick_duration, Clock::time_point start, uint64_t max_backlog)
    : tick_duration_(tick_duration), next_tick_(start), max_backlog_(max_backlog) {}

bool TickPacer::TakeTick(Clock::time_point now) {
  if (now < next_tick_) {
    return false;
  }
  if (now - next_tick_ >= tick_duration_ * static_cast<Clock::rep>(max_backlog_)) {
    next_tick_ = now;
  }
  next_tick_ += tick_duration_;
  return true;
}

}  // namespace z13
