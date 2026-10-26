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

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <sectors/sector.h>

namespace z13::net::sectors {

// What a sector holds; a new kind (an interest source) is added here.
enum class MemberKind : uint8_t { kPlayer, kBlock };
inline constexpr size_t kMemberKindCount = 2;

// A player's id or a block's flecs entity id; this library itself doesn't depend on flecs.
struct SectorMember {
  MemberKind kind {};
  uint64_t id {};
  bool operator==(const SectorMember&) const = default;
};

// Which sector each player and block is in, derived from where they are. A member is in one
// sector: a player's by its position, a block's by its anchor cell.
class SectorIndex {
 public:
  using Singleton = void;

  explicit SectorIndex(int sector_cells = DefaultSectorCells());

  int SectorCells() const { return sector_cells_; }
  // Adds the member, or moves it to `sector`.
  void Place(SectorMember member, const SectorKey& sector);
  void Remove(SectorMember member);

  // In no particular order.
  std::span<const SectorMember> MembersIn(const SectorKey& sector) const;
  std::optional<SectorKey> Locate(SectorMember member) const;
  std::vector<uint64_t> IdsOf(MemberKind kind) const;
  size_t Count(MemberKind kind) const { return where_[static_cast<size_t>(kind)].size(); }

 private:
  void Detach(SectorMember member, const SectorKey& sector);

  int sector_cells_ {};
  std::array<std::unordered_map<uint64_t, SectorKey>, kMemberKindCount> where_;
  std::unordered_map<SectorKey, std::vector<SectorMember>, SectorHash> members_;
};

}  // namespace z13::net::sectors
