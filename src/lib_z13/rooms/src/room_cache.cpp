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

#include <rooms/room_cache.h>

#include <algorithm>
#include <utility>

namespace z13::station::rooms {

std::expected<std::reference_wrapper<const RoomGraph>, std::string> RoomCache::Select(
    uint64_t fingerprint, const Build& build) {
  const auto cached = std::ranges::find(entries_, fingerprint, &RoomCacheEntry::fingerprint);
  if (cached != entries_.end()) {
    if (current_ != fingerprint) {
      const OptionalRoomGraph before = Current();
      Switch(before ? std::optional(ChangesBetween(*before, cached->graph)) : std::nullopt);
    }
    RoomCacheEntry entry = std::move(*cached);
    entries_.erase(cached);
    entries_.push_back(std::move(entry));
    current_ = fingerprint;
    return std::cref(entries_.back().graph);
  }
  auto graph = build(next_id_, Current());
  if (!graph) {
    current_.reset();
    Switch(std::nullopt);
    return std::unexpected(std::move(graph.error()));
  }
  const OptionalRoomGraph before = Current();
  Switch(before ? std::optional(ChangesBetween(*before, *graph)) : std::nullopt);
  next_id_ = graph->next_id;
  entries_.push_back({.fingerprint = fingerprint, .graph = std::move(*graph)});
  if (entries_.size() > kCapacity) {
    entries_.pop_front();
  }
  current_ = fingerprint;
  return std::cref(entries_.back().graph);
}

OptionalRoomGraph RoomCache::Current() const {
  if (!current_) {
    return std::nullopt;
  }
  return std::cref(entries_.back().graph);
}

std::optional<uint64_t> RoomCache::CurrentFingerprint() const {
  return current_;
}

std::optional<RoomChanges> RoomCache::ChangesSince(uint64_t seen) const {
  if (seen == switches_) {
    return RoomChanges {};
  }
  if (seen + 1 == switches_) {
    return last_changes_;
  }
  return std::nullopt;
}

void RoomCache::Switch(std::optional<RoomChanges> changes) {
  ++switches_;
  last_changes_ = std::move(changes);
}

}  // namespace z13::station::rooms
