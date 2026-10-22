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


#include <algorithm>

#include <gtest/gtest.h>

#include <rooms/space_walk.h>

namespace z13::station::rooms {
namespace {

using z13::building::primitives::CellBox;

const CellBox kHull {.min = {0, 0, 0}, .extent = {400, 200, 20}};

bool Outside(const CellBox& hull, const Eigen::Vector3f& point) {
  return (point.array() < hull.min.cast<float>().array()).any() || (point.array() >= hull.End().cast<float>().array()).any();
}

TEST(SpaceWalkRouteTest, ExitsThroughTheNearestWallAndLooksBack) {
  const std::vector<Eigen::Vector3f> inside {{100.f, 100.f, 7.f}, {380.f, 90.f, 7.f}};
  const std::vector<SpaceWalkStop> route = SpaceWalkRoute(kHull, inside);
  const auto exit = std::ranges::find(route, "exit", &SpaceWalkStop::label);
  ASSERT_NE(exit, route.end());
  EXPECT_GT(exit->eye.x(), static_cast<float>(kHull.End().x()));
  EXPECT_FLOAT_EQ(exit->eye.y(), 90.f);
  EXPECT_FLOAT_EQ(exit->eye.z(), 7.f);
  EXPECT_EQ(exit->target, inside.back());
}

TEST(SpaceWalkRouteTest, InsideStopsComeFirstThenSpaceStopsAllOutsideTheHull) {
  const std::vector<Eigen::Vector3f> inside {{100.f, 100.f, 7.f}};
  const std::vector<SpaceWalkStop> route = SpaceWalkRoute(kHull, inside);
  ASSERT_EQ(route.size(), 1u + 1u + 8u + 1u);
  EXPECT_FALSE(Outside(kHull, route.front().eye));
  for (auto stop = route.begin() + 1; stop != route.end(); ++stop) {
    EXPECT_TRUE(Outside(kHull, stop->eye)) << stop->label;
  }
}

TEST(SpaceWalkRouteTest, TheSameInputGivesTheSameRoute) {
  const std::vector<Eigen::Vector3f> inside {{100.f, 100.f, 7.f}};
  const auto first = SpaceWalkRoute(kHull, inside);
  const auto second = SpaceWalkRoute(kHull, inside);
  ASSERT_EQ(first.size(), second.size());
  for (size_t i = 0; i < first.size(); ++i) {
    EXPECT_EQ(first[i].label, second[i].label);
    EXPECT_EQ(first[i].eye, second[i].eye);
  }
}

TEST(SpaceWalkRouteTest, WithoutInsidePointsThereIsNoExit) {
  const std::vector<SpaceWalkStop> route = SpaceWalkRoute(kHull, {});
  EXPECT_EQ(std::ranges::count(route, "exit", &SpaceWalkStop::label), 0);
  EXPECT_EQ(route.size(), 9u);
}

}  // namespace
}  // namespace z13::station::rooms
