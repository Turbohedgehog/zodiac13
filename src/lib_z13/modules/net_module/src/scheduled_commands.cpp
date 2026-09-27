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

#include "scheduled_commands.h"

#include <algorithm>
#include <tuple>

namespace z13::net {

bool RecordLess(const z13::gameplay::PlayerActionRecord& a, const z13::gameplay::PlayerActionRecord& b) {
  return std::tie(a.tick, a.player_id, a.action_id) < std::tie(b.tick, b.player_id, b.action_id);
}

void QueueInOrder(z13::gameplay::ScheduledCommands& queue, const z13::gameplay::PlayerActionRecord& record) {
  const auto at = std::ranges::upper_bound(queue.records, record, RecordLess);
  queue.records.insert(at, record);
}

}  // namespace z13::net
