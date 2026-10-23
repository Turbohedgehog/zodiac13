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
#include <cstdint>
#include <optional>
#include <random>
#include <vector>

#include <Eigen/Dense>

#include <cell_light/cell_light.h>

namespace z13::station::light {
namespace {

using z13::building::primitives::CellBox;

// A hall of 40 x 16 x 12 free cells from (1, 1, 1) inside six slabs.
constexpr int kLength = 40;
constexpr int kWidth = 16;
constexpr int kHeight = 12;
constexpr float kRadius = 64.f;
constexpr float kTolerance = 1e-4f;

const Lamp kLampInA {.position = {10.5f, 8.5f, 10.5f}, .color = Eigen::Vector3f::Ones(), .radius = kRadius};
const Lamp kOtherLamp {.position = {34.5f, 4.5f, 10.5f}, .color = {1.f, 0.5f, 0.25f}, .radius = kRadius};
const Eigen::Vector3i kCellInA(10, 8, 2);
const Eigen::Vector3i kCellInB(30, 8, 2);

std::vector<CellBox> Hall() {
  const Eigen::Vector3i outer(kLength + 2, kWidth + 2, kHeight + 2);
  return {
      {.min = {0, 0, 0}, .extent = {outer.x(), outer.y(), 1}},
      {.min = {0, 0, outer.z() - 1}, .extent = {outer.x(), outer.y(), 1}},
      {.min = {0, 0, 1}, .extent = {1, outer.y(), kHeight}},
      {.min = {outer.x() - 1, 0, 1}, .extent = {1, outer.y(), kHeight}},
      {.min = {1, 0, 1}, .extent = {kLength, 1, kHeight}},
      {.min = {1, outer.y() - 1, 1}, .extent = {kLength, 1, kHeight}},
  };
}

CellBox Partition(int x) {
  return {.min = {x, 1, 1}, .extent = {1, kWidth, kHeight}};
}

// The partition with a doorway 4 cells wide and 8 tall at its y = 1 end, as the blocks around it.
std::vector<CellBox> PartitionWithDoorway(int x) {
  return {{.min = {x, 5, 1}, .extent = {1, kWidth - 4, kHeight}}, {.min = {x, 1, 9}, .extent = {1, 4, kHeight - 8}}};
}

Eigen::Vector3f LightOf(const CellLight& light, const Eigen::Vector3i& cell) {
  const auto node = light.NodeAt(cell);
  return node ? light.LightAt(*node) : Eigen::Vector3f::Zero();
}

std::vector<CellBox> With(std::vector<CellBox> boxes, const std::vector<CellBox>& more) {
  boxes.insert(boxes.end(), more.begin(), more.end());
  return boxes;
}

// Every free cell of the hall lit alike by both.
void ExpectSameLight(const CellLight& actual, const CellLight& expected) {
  for (int z = 1; z <= kHeight; ++z) {
    for (int y = 1; y <= kWidth; ++y) {
      for (int x = 1; x <= kLength; ++x) {
        const Eigen::Vector3i cell(x, y, z);
        ASSERT_EQ(actual.NodeAt(cell).has_value(), expected.NodeAt(cell).has_value()) << cell.transpose();
        ASSERT_TRUE(LightOf(actual, cell).isApprox(LightOf(expected, cell), kTolerance) ||
                    (LightOf(actual, cell) - LightOf(expected, cell)).norm() < kTolerance)
            << cell.transpose();
      }
    }
  }
}

// A lamp block on the floor, its light half a cell below it in the floor slab.
TEST(CellLightTest, ALampOnTheFloorLightsTheRoomAboveIt) {
  const CellBox lamp_block {.min = {10, 8, 1}, .extent = {2, 2, 1}};
  const Lamp on_floor {
      .position = {11.f, 9.f, 0.5f}, .behind = {11.f, 9.f, 2.5f}, .color = Eigen::Vector3f::Ones(), .radius = kRadius};
  const CellLight light(With(Hall(), {lamp_block}), std::vector {on_floor});

  EXPECT_GT(LightOf(light, kCellInA).minCoeff(), 0.f);
  EXPECT_GT(LightOf(light, kCellInB).minCoeff(), 0.f);
  EXPECT_EQ(LightOf(light, {10, 8, -1}), Eigen::Vector3f::Zero()) << "under the floor";
}

TEST(CellLightTest, AWallStopsTheLight) {
  const CellLight light(With(Hall(), {Partition(20)}), std::vector {kLampInA});

  EXPECT_GT(LightOf(light, kCellInA).minCoeff(), 0.f);
  EXPECT_TRUE(LightOf(light, kCellInB).isZero());
}

TEST(CellLightTest, AWallThroughACubeMakesANodeOnEachSide) {
  // Cells 20..23 make one cube; the wall at 22 leaves 20-21 and 23 apart.
  const CellLight light(With(Hall(), {Partition(22)}), std::vector {kLampInA});

  ASSERT_TRUE(light.NodeAt({21, 8, 2}) && light.NodeAt({23, 8, 2}));
  EXPECT_NE(*light.NodeAt({21, 8, 2}), *light.NodeAt({23, 8, 2}));
  EXPECT_GT(LightOf(light, {21, 8, 2}).minCoeff(), 0.f);
  EXPECT_TRUE(LightOf(light, {23, 8, 2}).isZero());
}

TEST(CellLightTest, LightRoundADoorwayIsDimmerThanStraight) {
  const CellLight open(Hall(), std::vector {kLampInA});
  const CellLight walled(With(Hall(), PartitionWithDoorway(20)), std::vector {kLampInA});

  EXPECT_GT(LightOf(walled, kCellInB).minCoeff(), 0.f);
  EXPECT_LT(LightOf(walled, kCellInB).x(), LightOf(open, kCellInB).x());
}

TEST(CellLightTest, TheLightLeansTowardItsLamp) {
  const CellLight light(Hall(), std::vector {kLampInA});

  const auto node = light.NodeAt(kCellInA);
  ASSERT_TRUE(node);
  EXPECT_GT(light.DirectionAt(*node).z(), 0.9f);
}

TEST(CellLightTest, EditsEndWhereAFullBuildDoes) {
  CellLight edited(Hall(), std::vector {kLampInA});

  edited.Apply({.opaque_added = PartitionWithDoorway(22), .lamps_added = {kOtherLamp}});
  edited.Apply({.opaque_added = {Partition(30)}, .opaque_removed = {PartitionWithDoorway(22)[1]}});
  edited.Apply({.lamps_removed = {kLampInA}});

  const std::vector final_blocks = With(Hall(), {PartitionWithDoorway(22)[0], Partition(30)});
  ExpectSameLight(edited, CellLight(final_blocks, std::vector {kOtherLamp}));
}

// Random boxes and lamps in the hall, built and taken away, each step against a full build.
TEST(CellLightTest, RandomEditsEndWhereAFullBuildDoes) {
  constexpr int kSteps = 12;
  constexpr uint32_t kSeed = 2026;
  constexpr int kMaxBoxCells = 6;
  std::mt19937 random(kSeed);
  const auto below = [&random](int bound) { return static_cast<int>(random() % static_cast<uint32_t>(bound)); };
  std::vector<CellBox> boxes;
  std::vector<Lamp> lamps {kLampInA};
  CellLight edited(Hall(), lamps);
  for (int step = 0; step < kSteps; ++step) {
    LightEdits edits;
    if (!boxes.empty() && below(3) == 0) {
      const auto removed = boxes.begin() + below(static_cast<int>(boxes.size()));
      edits.opaque_removed.push_back(*removed);
      boxes.erase(removed);
    } else {
      const Eigen::Vector3i extent(1 + below(kMaxBoxCells), 1 + below(kMaxBoxCells), 1 + below(kMaxBoxCells));
      const CellBox box {.min = {1 + below(kLength - extent.x()), 1 + below(kWidth - extent.y()), 1 + below(kHeight - extent.z())},
                         .extent = extent};
      if (std::ranges::any_of(boxes, [&box](const CellBox& other) { return other.Overlaps(box); }) ||
          std::ranges::any_of(lamps, [&box](const Lamp& lamp) {
            return box.Contains(lamp.position.array().floor().cast<int>().matrix());
          })) {
        continue;
      }
      edits.opaque_added.push_back(box);
      boxes.push_back(box);
    }
    if (below(2) == 0) {
      const Lamp lamp {.position = Eigen::Vector3f(1 + below(kLength), 1 + below(kWidth), kHeight) +
                                   Eigen::Vector3f::Constant(0.5f),
                       .color = Eigen::Vector3f::Ones(),
                       .radius = kRadius};
      if (std::ranges::none_of(boxes, [&lamp](const CellBox& box) {
            return box.Contains(lamp.position.array().floor().cast<int>().matrix());
          })) {
        edits.lamps_added.push_back(lamp);
        lamps.push_back(lamp);
      }
    } else if (lamps.size() > 1) {
      edits.lamps_removed.push_back(lamps.back());
      lamps.pop_back();
    }
    edited.Apply(edits);
    ExpectSameLight(edited, CellLight(With(Hall(), boxes), lamps));
  }
}

TEST(CellLightTest, ABlockOutsideGrowsTheLayout) {
  CellLight edited(Hall(), std::vector {kLampInA});
  const CellBox far {.min = {100, 0, 0}, .extent = {1, 1, 1}};

  const LightChanges changes = edited.Apply({.opaque_added = {far}});

  EXPECT_TRUE(changes.resized);
  EXPECT_TRUE(edited.Layout().CellBounds().Contains(far.min));
  ExpectSameLight(edited, CellLight(With(Hall(), {far}), std::vector {kLampInA}));
}

constexpr float kPaneTransmission = 0.1f;
constexpr float kTransmissionTolerance = 1e-3f;

GlassBox Pane(int x, float transmission = kPaneTransmission) {
  return {.cells = Partition(x), .transmission = transmission};
}

TEST(CellLightTest, GlassPassesItsShareOfTheLight) {
  const CellLight open(Hall(), std::vector {kLampInA});
  const CellLight glazed(Hall(), std::vector {Pane(20)}, std::vector {kLampInA});

  EXPECT_TRUE(LightOf(glazed, kCellInA).isApprox(LightOf(open, kCellInA), kTolerance))
      << "the lamp's own side is as lit";
  EXPECT_GT(LightOf(glazed, kCellInB).minCoeff(), 0.f);
  EXPECT_TRUE(LightOf(glazed, kCellInB).isApprox(kPaneTransmission * LightOf(open, kCellInB), kTransmissionTolerance));
}

TEST(CellLightTest, TheTwoSidesOfAPaneAreNodesApart) {
  const CellLight glazed(Hall(), std::vector {Pane(22)}, std::vector {kLampInA});

  ASSERT_TRUE(glazed.NodeAt({21, 8, 2}) && glazed.NodeAt({23, 8, 2}));
  EXPECT_NE(*glazed.NodeAt({21, 8, 2}), *glazed.NodeAt({23, 8, 2}));
  EXPECT_FALSE(glazed.NodeAt({22, 8, 2})) << "the pane itself holds no light";
}

TEST(CellLightTest, GlassThatPassesNothingIsAWall) {
  const CellLight glazed(Hall(), std::vector {Pane(20, 0.f)}, std::vector {kLampInA});

  EXPECT_GT(LightOf(glazed, kCellInA).minCoeff(), 0.f);
  EXPECT_TRUE(LightOf(glazed, kCellInB).isZero());
}

TEST(CellLightTest, AnOpenWayBeatsTheWindow) {
  const CellLight doorway(With(Hall(), PartitionWithDoorway(20)), std::vector {kLampInA});
  const CellBox window_in_the_doorway {.min = {20, 1, 1}, .extent = {1, 4, 1}};
  const GlassBox window {.cells = window_in_the_doorway, .transmission = 0.5f};
  const CellLight both(With(Hall(), PartitionWithDoorway(20)), std::vector {window}, std::vector {kLampInA});

  // The glass is in the floor row of the doorway, which is open above it: B's light is the doorway's.
  EXPECT_TRUE(LightOf(both, kCellInB).isApprox(LightOf(doorway, kCellInB), kTolerance));
}

TEST(CellLightTest, GlassEditsEndWhereAFullBuildDoes) {
  CellLight edited(Hall(), std::vector {kLampInA});

  edited.Apply({.glass_added = {Pane(22)}, .lamps_added = {kOtherLamp}});
  edited.Apply({.opaque_added = {Partition(30)}, .glass_added = {Pane(14)}, .glass_removed = {Pane(22)}});
  edited.Apply({.glass_added = {Pane(14, 0.5f)}, .glass_removed = {Pane(14)}});
  edited.Apply({.lamps_removed = {kLampInA}});

  ExpectSameLight(edited,
                  CellLight(With(Hall(), {Partition(30)}), std::vector {Pane(14, 0.5f)}, std::vector {kOtherLamp}));
}

}  // namespace
}  // namespace z13::station::light
