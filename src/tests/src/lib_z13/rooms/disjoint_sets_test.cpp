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

#include <rooms/disjoint_sets.h>

namespace z13::station::rooms {
namespace {

TEST(DisjointSetsTest, ItemsAreAloneUntilJoined) {
  DisjointSets sets(4);

  EXPECT_NE(sets.Find(0), sets.Find(1));
  EXPECT_NE(sets.Find(2), sets.Find(3));
}

TEST(DisjointSetsTest, JoiningIsTransitive) {
  DisjointSets sets(5);

  sets.Union(0, 1);
  sets.Union(1, 2);
  sets.Union(3, 4);

  EXPECT_EQ(sets.Find(0), sets.Find(2));
  EXPECT_EQ(sets.Find(3), sets.Find(4));
  EXPECT_NE(sets.Find(2), sets.Find(3));
}

}  // namespace
}  // namespace z13::station::rooms
