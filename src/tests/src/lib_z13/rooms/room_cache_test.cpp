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

#include <expected>
#include <string>

#include <rooms/room_cache.h>

namespace z13::station::rooms {
namespace {

constexpr uint64_t kFirst = 1;
constexpr uint64_t kSecond = 2;

RoomCache::Build Counting(int& builds, size_t rooms) {
  return [&builds, rooms](RoomId first_id, OptionalRoomGraph) -> std::expected<RoomGraph, std::string> {
    ++builds;
    RoomGraph graph {.next_id = first_id};
    for (size_t i = 0; i < rooms; ++i) {
      graph.rooms.emplace(graph.next_id++, Room {});
    }
    return graph;
  };
}

TEST(RoomCacheTest, ACachedFingerprintIsNotBuiltAgain) {
  RoomCache cache;
  int builds {};
  ASSERT_TRUE(cache.Select(kFirst, Counting(builds, 1)).has_value());
  ASSERT_TRUE(cache.Select(kSecond, Counting(builds, 2)).has_value());

  const auto again = cache.Select(kFirst, Counting(builds, 9));

  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(builds, 2);
  EXPECT_EQ(again->get().rooms.size(), 1U);
  EXPECT_EQ(cache.CurrentFingerprint(), kFirst);
}

TEST(RoomCacheTest, TheOldestGraphsAreDroppedPastTheCapacity) {
  RoomCache cache;
  int builds {};
  for (uint64_t fingerprint = 0; fingerprint <= RoomCache::kCapacity; ++fingerprint) {
    ASSERT_TRUE(cache.Select(fingerprint, Counting(builds, 1)).has_value());
  }

  ASSERT_TRUE(cache.Select(0, Counting(builds, 1)).has_value());

  EXPECT_EQ(builds, static_cast<int>(RoomCache::kCapacity) + 2);
}

TEST(RoomCacheTest, AnotherBuildGivesNewRoomIds) {
  RoomCache cache;
  int builds {};
  ASSERT_TRUE(cache.Select(kFirst, Counting(builds, 2)).has_value());

  const auto second = cache.Select(kSecond, Counting(builds, 1));

  ASSERT_TRUE(second.has_value());
  EXPECT_TRUE(second->get().rooms.contains(kVacuumRoom + 3));
}

TEST(RoomCacheTest, AFailedBuildLeavesNoCurrentGraph) {
  RoomCache cache;
  int builds {};
  ASSERT_TRUE(cache.Select(kFirst, Counting(builds, 1)).has_value());

  const auto failed = cache.Select(kSecond, [](RoomId, OptionalRoomGraph) -> std::expected<RoomGraph, std::string> {
    return std::unexpected("too wide");
  });

  ASSERT_FALSE(failed.has_value());
  EXPECT_EQ(failed.error(), "too wide");
  EXPECT_FALSE(cache.Current().has_value());
  EXPECT_FALSE(cache.CurrentFingerprint().has_value());
  ASSERT_TRUE(cache.Select(kFirst, Counting(builds, 9)).has_value());
  EXPECT_EQ(builds, 1);
  EXPECT_TRUE(cache.Current().has_value());
}

TEST(RoomCacheTest, ASwitchReportsTheRoomsItChanged) {
  RoomCache cache;
  int builds {};
  ASSERT_TRUE(cache.Select(kFirst, Counting(builds, 1)).has_value());
  const uint64_t seen = cache.Switches();

  ASSERT_TRUE(cache.Select(kSecond, Counting(builds, 2)).has_value());

  const RoomChanges expected {.added = {kVacuumRoom + 2, kVacuumRoom + 3}, .removed = {kVacuumRoom + 1}};
  EXPECT_EQ(cache.ChangesSince(seen), expected);
  EXPECT_EQ(cache.ChangesSince(cache.Switches()), RoomChanges {});
}

TEST(RoomCacheTest, ACachedGraphSelectedAgainIsASwitchTooButTheCurrentOneIsNot) {
  RoomCache cache;
  int builds {};
  ASSERT_TRUE(cache.Select(kFirst, Counting(builds, 1)).has_value());
  ASSERT_TRUE(cache.Select(kSecond, Counting(builds, 2)).has_value());
  const uint64_t seen = cache.Switches();

  ASSERT_TRUE(cache.Select(kSecond, Counting(builds, 9)).has_value());
  EXPECT_EQ(cache.Switches(), seen);
  ASSERT_TRUE(cache.Select(kFirst, Counting(builds, 9)).has_value());

  const RoomChanges expected {.added = {kVacuumRoom + 1}, .removed = {kVacuumRoom + 2, kVacuumRoom + 3}};
  EXPECT_EQ(cache.ChangesSince(seen), expected);
}

TEST(RoomCacheTest, AConsumerThatMissedASwitchOrHadNoGraphGetsNoChanges) {
  RoomCache cache;
  int builds {};
  ASSERT_TRUE(cache.Select(kFirst, Counting(builds, 1)).has_value());
  EXPECT_FALSE(cache.ChangesSince(0).has_value());
  const uint64_t seen = cache.Switches();
  ASSERT_TRUE(cache.Select(kSecond, Counting(builds, 1)).has_value());
  ASSERT_TRUE(cache.Select(kFirst, Counting(builds, 1)).has_value());

  EXPECT_FALSE(cache.ChangesSince(seen).has_value());
}

}  // namespace
}  // namespace z13::station::rooms
