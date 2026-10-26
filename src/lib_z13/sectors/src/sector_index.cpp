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

#include <sectors/sector_index.h>

#include <algorithm>
#include <iterator>
#include <ranges>

namespace z13::net::sectors {

SectorIndex::SectorIndex(int sector_cells) : sector_cells_(sector_cells) {
}

void SectorIndex::Place(SectorMember member, const SectorKey& sector) {
  auto& where = where_[static_cast<size_t>(member.kind)];
  const auto [placed, inserted] = where.try_emplace(member.id, sector);
  if (!inserted) {
    if (placed->second == sector) {
      return;
    }
    Detach(member, placed->second);
    placed->second = sector;
  }
  members_[sector].push_back(member);
}

void SectorIndex::Remove(SectorMember member) {
  auto& where = where_[static_cast<size_t>(member.kind)];
  const auto placed = where.find(member.id);
  if (placed == where.end()) {
    return;
  }
  Detach(member, placed->second);
  where.erase(placed);
}

void SectorIndex::Detach(SectorMember member, const SectorKey& sector) {
  const auto held = members_.find(sector);
  auto& in_sector = held->second;
  *std::ranges::find(in_sector, member) = in_sector.back();
  in_sector.pop_back();
  if (in_sector.empty()) {
    members_.erase(held);
  }
}

std::span<const SectorMember> SectorIndex::MembersIn(const SectorKey& sector) const {
  const auto held = members_.find(sector);
  return held == members_.end() ? std::span<const SectorMember> {} : std::span<const SectorMember> {held->second};
}

std::vector<SectorKey> SectorIndex::Occupied() const {
  std::vector<SectorKey> sectors;
  sectors.reserve(members_.size());
  for (const auto& [sector, held] : members_) {
    sectors.push_back(sector);
  }
  return sectors;
}

std::optional<SectorKey> SectorIndex::Locate(SectorMember member) const {
  const auto& where = where_[static_cast<size_t>(member.kind)];
  const auto placed = where.find(member.id);
  return placed == where.end() ? std::nullopt : std::optional<SectorKey> {placed->second};
}

std::vector<uint64_t> SectorIndex::IdsOf(MemberKind kind) const {
  const auto& where = where_[static_cast<size_t>(kind)];
  std::vector<uint64_t> ids;
  ids.reserve(where.size());
  std::ranges::copy(where | std::views::keys, std::back_inserter(ids));
  return ids;
}

}  // namespace z13::net::sectors
