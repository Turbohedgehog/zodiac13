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

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <functional>
#include <optional>
#include <string>

#include <rooms/room_graph.h>

namespace z13::station::rooms {

struct RoomCacheEntry {
  uint64_t fingerprint {};
  RoomGraph graph;
};

// The room graphs of the last few block fingerprints, the newest being the current one: a
// rollback through a build finds the graph again instead of building it twice. Derived,
// never state.
class RoomCache {
 public:
  using Singleton = void;
  using Build = std::function<std::expected<RoomGraph, std::string>()>;

  static constexpr size_t kCapacity = 4;

  // Makes the graph of `fingerprint` the current one, calling `build` unless it is cached.
  // On failure there is no current graph: rooms of other blocks would be wrong.
  std::expected<std::reference_wrapper<const RoomGraph>, std::string> Select(
      uint64_t fingerprint, const Build& build);

  std::optional<std::reference_wrapper<const RoomGraph>> Current() const;
  std::optional<uint64_t> CurrentFingerprint() const;

  size_t BlocksSeen() const { return blocks_seen_; }
  void SetBlocksSeen(size_t blocks) { blocks_seen_ = blocks; }

 private:
  std::deque<RoomCacheEntry> entries_;
  std::optional<uint64_t> current_;
  size_t blocks_seen_ {};
};

}  // namespace z13::station::rooms
