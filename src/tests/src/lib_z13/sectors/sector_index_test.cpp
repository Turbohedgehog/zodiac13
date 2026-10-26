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

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include <sectors/sector_index.h>

namespace z13::net::sectors {
namespace {

constexpr int kSectorCells = 16;
constexpr SectorMember kPlayer {MemberKind::kPlayer, 7};
constexpr SectorMember kBlock {MemberKind::kBlock, 7};

std::vector<SectorMember> MembersOf(const SectorIndex& index, const SectorKey& sector) {
  const auto members = index.MembersIn(sector);
  return {members.begin(), members.end()};
}

TEST(SectorIndexTest, AMemberIsFoundInItsSector) {
  SectorIndex index(kSectorCells);

  index.Place(kPlayer, {1, 0, 0});

  EXPECT_EQ(index.Locate(kPlayer), SectorKey(1, 0, 0));
  EXPECT_EQ(MembersOf(index, {1, 0, 0}), std::vector {kPlayer});
  EXPECT_TRUE(index.MembersIn({0, 0, 0}).empty());
}

TEST(SectorIndexTest, APlayerAndABlockOfOneIdAreTwoMembers) {
  SectorIndex index(kSectorCells);

  index.Place(kPlayer, {0, 0, 0});
  index.Place(kBlock, {2, 0, 0});

  EXPECT_EQ(index.Locate(kPlayer), SectorKey(0, 0, 0));
  EXPECT_EQ(index.Locate(kBlock), SectorKey(2, 0, 0));
  EXPECT_EQ(index.Count(MemberKind::kPlayer), 1u);
  EXPECT_EQ(index.Count(MemberKind::kBlock), 1u);
}

TEST(SectorIndexTest, AMemberMovesBetweenSectors) {
  SectorIndex index(kSectorCells);
  index.Place(kPlayer, {0, 0, 0});

  index.Place(kPlayer, {0, 1, 0});

  EXPECT_EQ(index.Locate(kPlayer), SectorKey(0, 1, 0));
  EXPECT_TRUE(index.MembersIn({0, 0, 0}).empty());
  EXPECT_EQ(MembersOf(index, {0, 1, 0}), std::vector {kPlayer});
  EXPECT_EQ(index.Count(MemberKind::kPlayer), 1u);
}

TEST(SectorIndexTest, PlacingAMemberWhereItIsChangesNothing) {
  SectorIndex index(kSectorCells);
  index.Place(kPlayer, {0, 0, 0});

  index.Place(kPlayer, {0, 0, 0});

  EXPECT_EQ(MembersOf(index, {0, 0, 0}), std::vector {kPlayer});
}

TEST(SectorIndexTest, RemovingAMemberLeavesTheOthersInTheSector) {
  SectorIndex index(kSectorCells);
  const SectorMember other {MemberKind::kPlayer, 8};
  index.Place(kPlayer, {0, 0, 0});
  index.Place(other, {0, 0, 0});

  index.Remove(kPlayer);

  EXPECT_FALSE(index.Locate(kPlayer).has_value());
  EXPECT_EQ(MembersOf(index, {0, 0, 0}), std::vector {other});
}

TEST(SectorIndexTest, RemovingAnAbsentMemberIsNothing) {
  SectorIndex index(kSectorCells);

  index.Remove(kPlayer);

  EXPECT_EQ(index.Count(MemberKind::kPlayer), 0u);
}

TEST(SectorIndexTest, IdsOfAKindListOnlyThatKind) {
  SectorIndex index(kSectorCells);
  index.Place({MemberKind::kPlayer, 1}, {0, 0, 0});
  index.Place({MemberKind::kPlayer, 2}, {1, 0, 0});
  index.Place(kBlock, {0, 0, 0});

  std::vector<uint64_t> ids = index.IdsOf(MemberKind::kPlayer);
  std::ranges::sort(ids);

  EXPECT_EQ(ids, (std::vector<uint64_t> {1, 2}));
}

}  // namespace
}  // namespace z13::net::sectors
