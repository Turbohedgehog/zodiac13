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

#include <Eigen/Dense>

#include <sectors/sector.h>

namespace z13::net::sectors {
namespace {

constexpr int kSectorCells = 16;

TEST(SectorTest, ACellIsInTheSectorByFloorDivision) {
  EXPECT_EQ(SectorOf({0, 0, 0}, kSectorCells), SectorKey(0, 0, 0));
  EXPECT_EQ(SectorOf({15, 15, 15}, kSectorCells), SectorKey(0, 0, 0));
  EXPECT_EQ(SectorOf({16, 0, 0}, kSectorCells), SectorKey(1, 0, 0));
  EXPECT_EQ(SectorOf({-1, 0, 0}, kSectorCells), SectorKey(-1, 0, 0)) << "below zero is the sector below";
  EXPECT_EQ(SectorOf({-16, -17, 0}, kSectorCells), SectorKey(-1, -2, 0));
}

TEST(SectorTest, ABoxInsideASectorTouchesOnlyIt) {
  const auto touched = SectorsTouched({.min = {1, 1, 1}, .extent = {4, 4, 4}}, kSectorCells);

  EXPECT_EQ(touched, std::vector<SectorKey> {SectorKey(0, 0, 0)});
}

TEST(SectorTest, ABoxOnABorderTouchesBothSides) {
  const auto touched = SectorsTouched({.min = {14, 0, 0}, .extent = {4, 1, 1}}, kSectorCells);

  EXPECT_EQ(touched, (std::vector<SectorKey> {SectorKey(0, 0, 0), SectorKey(1, 0, 0)}));
}

TEST(SectorTest, ABoxAcrossACornerTouchesEightSectors) {
  const auto touched = SectorsTouched({.min = {-1, -1, -1}, .extent = {2, 2, 2}}, kSectorCells);

  EXPECT_EQ(touched.size(), 8u);
}

TEST(SectorTest, ABoxEndingOnABorderDoesNotTouchTheNextSector) {
  const auto touched = SectorsTouched({.min = {12, 0, 0}, .extent = {4, 1, 1}}, kSectorCells);

  EXPECT_EQ(touched, std::vector<SectorKey> {SectorKey(0, 0, 0)});
}

TEST(SectorTest, AnEmptyBoxTouchesNothing) {
  EXPECT_TRUE(SectorsTouched({.min = {3, 3, 3}, .extent = {0, 1, 1}}, kSectorCells).empty());
}

TEST(SectorBoxTest, ABoxHoldsTheSectorsBetweenItsCornersAndGrowsOnEverySide) {
  const SectorBox box {.min = {-1, 0, 2}, .max = {1, 1, 2}};

  EXPECT_TRUE(box.Contains({0, 1, 2}));
  EXPECT_TRUE(box.Contains({1, 0, 2}));
  EXPECT_FALSE(box.Contains({2, 0, 2}));
  EXPECT_FALSE(box.Contains({0, 0, 3}));
  EXPECT_EQ(box.Sectors().size(), 6u);
  EXPECT_TRUE(box.Grown(1).Contains({2, -1, 3}));
  EXPECT_FALSE(box.Grown(1).Contains({3, 0, 2}));
}

}  // namespace
}  // namespace z13::net::sectors
