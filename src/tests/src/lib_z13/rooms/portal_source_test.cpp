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

#include <primitives/occupancy.h>
#include <rooms/portal_source.h>
#include <z13/components/block.h>
#include <z13_tests/shipped_station.h>

namespace z13::station::rooms {
namespace {

using z13::building::primitives::CellBox;

const z13::building::primitives::Palette& ShippedPalette() {
  static const z13::building::primitives::Palette palette = z13::testing::ShippedPalette().value();
  return palette;
}

std::vector<PortalSource> PortalsOfBlock(const Block& block) {
  return PortalsOf(block, ShippedPalette().Find(block.spec.type_id).value(), ShippedPalette());
}

Block Glass(const Eigen::Vector3i& size, int slant) {
  return {.spec = {.type_id = kGlassPanelPrimitiveId, .size = size, .slant = slant}, .cell = {2, 3, 4}};
}

TEST(PortalSourceTest, APaneSquareToTheGridIsOneVisiblePortalAcrossItsThickness) {
  const std::vector<PortalSource> portals = PortalsOfBlock(Glass({4, 6, 1}, 0));

  ASSERT_EQ(portals.size(), 1U);
  EXPECT_EQ(portals.front().opening, (CellBox {.min = {2, 3, 4}, .extent = {4, 6, 1}}));
  EXPECT_EQ(portals.front().axis, Axis::kZ);
  EXPECT_TRUE(portals.front().visible);
  EXPECT_FALSE(portals.front().passable);
}

TEST(PortalSourceTest, ASlantedPaneIsAPortalForEachBoxItSealsCrossedAlongItsThickness) {
  const Block pane = Glass({6, 2, 4}, 3);

  const std::vector<PortalSource> portals = PortalsOfBlock(pane);

  const std::vector<CellBox> sealed = z13::building::primitives::SealedBoxes(pane, ShippedPalette());
  ASSERT_EQ(portals.size(), sealed.size());
  ASSERT_GT(portals.size(), 1U);
  for (size_t i = 0; i < portals.size(); ++i) {
    EXPECT_EQ(portals[i].opening, sealed[i]);
    EXPECT_EQ(portals[i].axis, Axis::kZ);
    EXPECT_TRUE(portals[i].visible);
    EXPECT_FALSE(portals[i].passable);
  }
}

Block DoorLeaf(const Eigen::Vector3i& size, int slant) {
  Block leaf = Glass(size, slant);
  leaf.spec.type_id = kDoorPanelPrimitiveId;
  return leaf;
}

TEST(PortalSourceTest, ADoorLeafSquareToTheGridIsOneClosedPortalYouCannotSeeThrough) {
  const std::vector<PortalSource> portals = PortalsOfBlock(DoorLeaf({4, 6, 1}, 0));

  ASSERT_EQ(portals.size(), 1U);
  EXPECT_EQ(portals.front().opening, (CellBox {.min = {2, 3, 4}, .extent = {4, 6, 1}}));
  EXPECT_FALSE(portals.front().visible);
  EXPECT_FALSE(portals.front().passable);
}

TEST(PortalSourceTest, ASlantedDoorLeafIsAClosedPortalForEachBoxItSeals) {
  const Block leaf = DoorLeaf({6, 2, 4}, 3);

  const std::vector<PortalSource> portals = PortalsOfBlock(leaf);

  ASSERT_EQ(portals.size(), z13::building::primitives::SealedBoxes(leaf, ShippedPalette()).size());
  ASSERT_GT(portals.size(), 1U);
  for (const PortalSource& portal : portals) {
    EXPECT_FALSE(portal.visible);
    EXPECT_FALSE(portal.passable);
  }
}

TEST(PortalSourceTest, APlainPanelIsNoPortal) {
  Block panel = Glass({4, 6, 1}, 0);
  panel.spec.type_id = kPanelPrimitiveId;
  EXPECT_TRUE(PortalsOfBlock(panel).empty());
}

}  // namespace
}  // namespace z13::station::rooms
