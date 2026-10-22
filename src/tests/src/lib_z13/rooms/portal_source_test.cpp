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

#include <rooms/portal_source.h>
#include <z13/components/block.h>
#include <z13_tests/shipped_station.h>

namespace z13::station::rooms {
namespace {

const z13::building::primitives::Palette& ShippedPalette() {
  static const z13::building::primitives::Palette palette = z13::testing::ShippedPalette().value();
  return palette;
}

std::optional<PortalSource> PortalOfGlass(const Block& block) {
  return PortalOf(block, ShippedPalette().Find(block.spec.type_id).value());
}

Block Glass(const Eigen::Vector3i& size, int slant) {
  return {.spec = {.type_id = kGlassPanelPrimitiveId, .size = size, .slant = slant}, .cell = {2, 3, 4}};
}

TEST(PortalSourceTest, APaneSquareToTheGridIsAVisiblePortalAcrossItsThickness) {
  const std::optional<PortalSource> portal = PortalOfGlass(Glass({4, 6, 1}, 0));

  ASSERT_TRUE(portal.has_value());
  EXPECT_EQ(portal->opening, (z13::building::primitives::CellBox {.min = {2, 3, 4}, .extent = {4, 6, 1}}));
  EXPECT_EQ(portal->axis, Axis::kZ);
  EXPECT_TRUE(portal->visible);
  EXPECT_FALSE(portal->passable);
}

TEST(PortalSourceTest, ASlantedPaneIsNoPortalYet) {
  EXPECT_FALSE(PortalOfGlass(Glass({4, 6, 3}, 2)).has_value());
}

TEST(PortalSourceTest, APlainPanelIsNoPortal) {
  Block panel = Glass({4, 6, 1}, 0);
  panel.spec.type_id = kPanelPrimitiveId;
  EXPECT_FALSE(PortalOfGlass(panel).has_value());
}

}  // namespace
}  // namespace z13::station::rooms
