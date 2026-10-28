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

// What the blocks of empty sectors cost the player's queries and the building of the Bullet world: the shipped
// stations side by side, the player in the first, all copies' bodies against the first's alone.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <format>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <bullet/btBulletDynamicsCommon.h>

#include "../../src/physics_world.h"

#include <primitives/geometry.h>
#include <primitives/palette.h>
#include <primitives/placement.h>
#include <z13/components/block.h>
#include <z13_settings/physics_tuning.h>
#include <z13_tests/shipped_station.h>

namespace z13::bullet_module {
namespace {

using z13::building::primitives::BlockShapeKey;

constexpr int kCopies[] = {1, 2, 4};
constexpr int kQueries = 2000;
constexpr double kMicrosPerMs = 1000.0;
constexpr flecs::entity_t kFirstEntity = 1000;
constexpr float kCopyGapCells = 14.f;
// Where in the first copy the player's queries start, in meters: over its bottom deck.
constexpr float kPlayerHeight = 1.5f;
constexpr float kQueryReach = 0.1f;

template <typename F>
double Ms(F&& work) {
  const auto start = std::chrono::steady_clock::now();
  work();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// The station `copies` times along +x; the first `first_copy_blocks` blocks are the first copy.
struct Stand {
  std::vector<z13::station::Block> blocks;
  size_t first_copy_blocks {};
};

Stand MakeStand(const std::vector<z13::station::Block>& station, int copies) {
  int width = 0;
  for (const z13::station::Block& block : station) {
    width = std::max(width, z13::building::primitives::OccupiedCells(block).End().x());
  }
  const int pitch = width + static_cast<int>(kCopyGapCells);
  Stand stand {.first_copy_blocks = station.size()};
  for (int copy = 0; copy < copies; ++copy) {
    for (z13::station::Block block : station) {
      block.cell.x() += pitch * copy;
      stand.blocks.push_back(block);
    }
  }
  return stand;
}

void AddBodies(PhysicsWorld& world, const Stand& stand, const z13::building::primitives::Palette& palette,
               size_t count) {
  for (size_t index = 0; index < count; ++index) {
    const z13::station::Block& block = stand.blocks[index];
    const BlockShapeKey shape {.type_id = block.spec.type_id, .size = block.spec.size};
    const Eigen::Isometry3f pose =
        z13::building::primitives::WorldPose(z13::building::primitives::PoseOf(block), z13::station::kCellSize);
    world.SyncBody(kFirstEntity + index, shape, ToBtTransform(pose.matrix()), [&] {
      return z13::building::primitives::Scaled(
          z13::building::primitives::BlockSolids(palette, shape), z13::station::kCellSize);
    });
  }
}

double QueryMicros(PhysicsWorld& world, const btVector3& from) {
  const float radius = z13::PhysicsTuning {}.player_collider_radius;
  const uint32_t iterations = z13::PhysicsTuning {}.max_sweep_iterations;
  return Ms([&] {
           for (int query = 0; query < kQueries; ++query) {
             world.ResolveSpherePosition(from, radius);
             world.SweepSphere(from, from + btVector3(kQueryReach, 0.f, 0.f), radius, iterations);
           }
         }) *
         kMicrosPerMs / kQueries;
}

TEST(EmptySectorBench, DISABLED_Queries) {
  const auto palette = z13::testing::ShippedPalette();
  ASSERT_TRUE(palette.has_value()) << palette.error();
  for (const std::string& scene : z13::testing::ShippedScenes()) {
    const auto station = z13::testing::ShippedBlueprint(scene, *palette);
    ASSERT_TRUE(station.has_value()) << station.error();
    Eigen::Vector3i min = Eigen::Vector3i::Constant(std::numeric_limits<int>::max());
    Eigen::Vector3i max = Eigen::Vector3i::Constant(std::numeric_limits<int>::min());
    for (const z13::station::Block& block : *station) {
      const z13::building::primitives::CellBox box = z13::building::primitives::OccupiedCells(block);
      min = min.cwiseMin(box.min);
      max = max.cwiseMax(box.End());
    }
    const Eigen::Vector3f middle = (min + max).cast<float>() * z13::station::kCellSize / 2.f;
    const btVector3 player(middle.x(), middle.y(), static_cast<float>(min.z()) * z13::station::kCellSize + kPlayerHeight);

    std::cout << std::format("[ bench ] station '{}': blocks in the world, build ms, player queries us "
                             "(all copies / first copy alone)\n",
                             scene);
    for (const int copies : kCopies) {
      const Stand stand = MakeStand(*station, copies);
      PhysicsWorld everything;
      const double build_ms = Ms([&] { AddBodies(everything, stand, *palette, stand.blocks.size()); });
      PhysicsWorld alone;
      AddBodies(alone, stand, *palette, stand.first_copy_blocks);
      std::cout << std::format("[ bench ] x{}  {:>6} / {:<6}  build {:>6.0f}  queries {:>6.1f} / {:<6.1f}\n", copies,
                               stand.blocks.size(), stand.first_copy_blocks, build_ms,
                               QueryMicros(everything, player), QueryMicros(alone, player));
    }
  }
  std::cout << std::flush;
}

}  // namespace
}  // namespace z13::bullet_module
