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
#include <string>

#include <z13/components/gameplay/inventory.h>
#include <z13/components/gameplay/scan.h>

#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/scanner_system.h"
#include "support/station_builders.h"

// The scanner in the hand reports the block the player aims at.
namespace z13::building {
namespace {

using z13::inventory::ScanReport;
using z13::testing::Z13TestWorld;

class ScannerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    world_.Tick();
    z13::testing::Ticks(world_, z13::testing::kToolSettleTicks);
  }

  void HoldScanner() {
    world_.Player().set(z13::inventory::Hotbar {.active_cell = kScannerCell});
    z13::testing::Ticks(world_, z13::testing::kToolSettleTicks);
  }

  Z13TestWorld world_ = z13::testing::StationWorld();
};

TEST_F(ScannerTest, EveryPlayerIsGivenTheScannerOnce) {
  z13::testing::Ticks(world_, 2);

  EXPECT_EQ(world_.World().count<z13::inventory::ScannerTool>(), 1);
  EXPECT_TRUE(world_.Player().has<ScannerGiven>());
}

TEST_F(ScannerTest, NothingIsReportedWhileItIsNotInTheHand) {
  z13::testing::AddWallAhead(world_, world_.Player());
  z13::testing::Ticks(world_, 2);

  EXPECT_FALSE(world_.Player().has<ScanReport>());
}

TEST_F(ScannerTest, InTheHandItNamesTheAimedBlockAndItsCell) {
  const z13::station::Block wall = z13::testing::AddWallAhead(world_, world_.Player()).block;
  HoldScanner();

  ASSERT_TRUE(world_.Player().has<ScanReport>());
  const auto& lines = world_.Player().get<ScanReport>().lines;
  ASSERT_GE(lines.size(), 2u);
  EXPECT_EQ(lines.front(), "Wall");
  EXPECT_NE(lines[1].find(std::to_string(wall.cell.x())), std::string::npos) << lines[1];
}

}  // namespace
}  // namespace z13::building
