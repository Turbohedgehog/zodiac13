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

#include <rooms/room_changes.h>

#include <algorithm>
#include <span>

namespace z13::station::rooms {

namespace {

bool SamePortals(const RoomGraph& before, const RoomGraph& after, RoomId room) {
  const std::span<const size_t> first = before.PortalsOf(room);
  const std::span<const size_t> second = after.PortalsOf(room);
  return std::ranges::equal(first, second, [&](size_t a, size_t b) { return before.portals[a] == after.portals[b]; });
}

}  // namespace

RoomChanges ChangesBetween(const RoomGraph& before, const RoomGraph& after) {
  RoomChanges changes;
  for (const auto& [id, room] : after.rooms) {
    const auto old = before.rooms.find(id);
    if (old == before.rooms.end()) {
      changes.added.push_back(id);
    } else if (old->second != room || !SamePortals(before, after, id)) {
      changes.changed.push_back(id);
    }
  }
  for (const auto& [id, room] : before.rooms) {
    if (!after.rooms.contains(id)) {
      changes.removed.push_back(id);
    }
  }
  std::ranges::sort(changes.added);
  std::ranges::sort(changes.removed);
  std::ranges::sort(changes.changed);
  return changes;
}

}  // namespace z13::station::rooms
