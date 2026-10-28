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
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <bullet/btBulletDynamicsCommon.h>

#include "../src/physics_world.h"

#include <primitives/geometry.h>
#include <primitives/placement.h>
#include <z13/components/block.h>
#include <z13_settings/physics_tuning.h>
#include <z13_tests/shipped_station.h>

// Н3 (docs/net-core.md): a client builds its Bullet world from a snapshot, the server built
// its own block by block, so the broadphase trees differ; every query must still agree bit for bit.
namespace z13::bullet_module {
namespace {

using z13::building::primitives::BlockShapeKey;

constexpr uint32_t kSeed = 13;
constexpr size_t kProbes = 2000;
// Entity ids are arbitrary but must be the same in every world: offset from the index.
constexpr flecs::entity_t kFirstEntity = 1000;
// Lets the DBVT's incremental optimization reshape the tree, which no tick does now: a stricter check.
constexpr int kReshapes = 120;
// How far a probe's sweep and ray reach, in meters: across a block and into its neighbors.
constexpr float kProbeReach = 1.5f;
// One block in this many is destroyed and rebuilt in the grown world.
constexpr size_t kRebuiltEvery = 3;

const float kRadius = z13::PhysicsTuning {}.player_collider_radius;
const uint32_t kSweepIterations = z13::PhysicsTuning {}.max_sweep_iterations;

struct ProbeResult {
  btVector3 resolved;
  btVector3 swept;
  btVector3 fell;
  std::optional<flecs::entity_t> hit;
};

bool SameBits(const btVector3& a, const btVector3& b) {
  return std::bit_cast<uint32_t>(a.x()) == std::bit_cast<uint32_t>(b.x()) &&
         std::bit_cast<uint32_t>(a.y()) == std::bit_cast<uint32_t>(b.y()) &&
         std::bit_cast<uint32_t>(a.z()) == std::bit_cast<uint32_t>(b.z());
}

bool SameBits(const ProbeResult& a, const ProbeResult& b) {
  return SameBits(a.resolved, b.resolved) && SameBits(a.swept, b.swept) && SameBits(a.fell, b.fell) && a.hit == b.hit;
}

btVector3 ToBt(const Eigen::Vector3f& v) {
  return {v.x(), v.y(), v.z()};
}

class StationBodies {
 public:
  StationBodies(std::span<const z13::station::Block> blocks, const z13::building::primitives::Palette& palette)
      : blocks_(blocks), palette_(palette) {}

  void Add(PhysicsWorld& world, size_t index) const {
    const z13::station::Block& block = blocks_[index];
    const BlockShapeKey shape {.type_id = block.spec.type_id, .size = block.spec.size};
    const Eigen::Isometry3f pose =
        z13::building::primitives::WorldPose(z13::building::primitives::PoseOf(block), z13::station::kCellSize);
    world.SyncBody(kFirstEntity + index, shape, ToBtTransform(pose.matrix()), [this, &shape]() {
      return z13::building::primitives::Scaled(
          z13::building::primitives::BlockSolids(palette_.get(), shape), z13::station::kCellSize);
    });
  }

  void Remove(PhysicsWorld& world, size_t index) const { world.RemoveBody(kFirstEntity + index); }

  void AddAll(PhysicsWorld& world, std::span<const size_t> order) const {
    for (const size_t index : order) {
      Add(world, index);
    }
  }

  // In meters, grown by the player's radius so a probe can touch the block from outside.
  Eigen::AlignedBox3f Reach(size_t index) const {
    const z13::building::primitives::CellBox cells = z13::building::primitives::OccupiedCells(blocks_[index]);
    const Eigen::Vector3f margin = Eigen::Vector3f::Constant(kRadius);
    return {cells.min.cast<float>() * z13::station::kCellSize - margin,
            cells.End().cast<float>() * z13::station::kCellSize + margin};
  }

  size_t Size() const { return blocks_.size(); }

 private:
  std::span<const z13::station::Block> blocks_;
  std::reference_wrapper<const z13::building::primitives::Palette> palette_;
};

struct Probe {
  Eigen::Vector3f center;
  Eigen::Vector3f motion;
};

// Spheres placed around random blocks, so most touch a block or several.
std::vector<Probe> MakeProbes(const StationBodies& bodies) {
  std::mt19937 random(kSeed);
  std::uniform_int_distribution<size_t> pick(0, bodies.Size() - 1);
  std::uniform_real_distribution<float> unit(0.f, 1.f);
  std::uniform_real_distribution<float> signed_unit(-1.f, 1.f);
  std::vector<Probe> probes(kProbes);
  std::ranges::generate(probes, [&]() {
    const Eigen::AlignedBox3f reach = bodies.Reach(pick(random));
    const Eigen::Vector3f t(unit(random), unit(random), unit(random));
    const Eigen::Vector3f direction(signed_unit(random), signed_unit(random), signed_unit(random));
    return Probe {.center = reach.min() + reach.sizes().cwiseProduct(t), .motion = direction * kProbeReach};
  });
  return probes;
}

std::vector<ProbeResult> RunProbes(PhysicsWorld& world, std::span<const Probe> probes) {
  std::vector<ProbeResult> results;
  results.reserve(probes.size());
  for (const Probe& probe : probes) {
    const btVector3 center = ToBt(probe.center);
    results.push_back({
        .resolved = world.ResolveSpherePosition(center, kRadius),
        .swept = world.SweepSphere(center, center + ToBt(probe.motion), kRadius, kSweepIterations),
        .fell = world.SweepSphere(center, center - btVector3(0.f, 0.f, kProbeReach), kRadius, 1),
        .hit = world.RaycastEntity(center, center + ToBt(probe.motion)),
    });
  }
  return results;
}

void Reshape(PhysicsWorld& world) {
  for (int pass = 0; pass < kReshapes; ++pass) {
    world.World().performDiscreteCollisionDetection();
  }
}

std::string Describe(const ProbeResult& r) {
  return std::format("resolved ({} {} {}) swept ({} {} {}) fell ({} {} {}) hit {}", r.resolved.x(), r.resolved.y(),
                     r.resolved.z(), r.swept.x(), r.swept.y(), r.swept.z(), r.fell.x(), r.fell.y(), r.fell.z(),
                     r.hit ? std::to_string(*r.hit) : std::string("none"));
}

void ExpectSameResults(std::span<const ProbeResult> expected, std::span<const ProbeResult> actual,
                       std::string_view world_name) {
  ASSERT_EQ(expected.size(), actual.size());
  size_t differing = 0;
  std::optional<size_t> first;
  for (size_t i = 0; i < expected.size(); ++i) {
    if (!SameBits(expected[i], actual[i])) {
      ++differing;
      first = first.value_or(i);
    }
  }
  EXPECT_EQ(differing, 0u) << world_name << ": " << differing << " of " << expected.size() << " probes differ";
  if (first) {
    ADD_FAILURE() << "probe " << *first << "\n  expected " << Describe(expected[*first]) << "\n  actual   "
                  << Describe(actual[*first]);
  }
}

TEST(BroadphaseOrderTest, QueriesDontDependOnTheOrderBodiesWereAdded) {
  const auto palette = z13::testing::ShippedPalette();
  ASSERT_TRUE(palette.has_value()) << palette.error();
  const auto blocks = z13::testing::ShippedBlueprint(z13::testing::kTestScene, *palette);
  ASSERT_TRUE(blocks.has_value()) << blocks.error();
  const StationBodies bodies(*blocks, *palette);
  const std::vector<Probe> probes = MakeProbes(bodies);

  std::vector<size_t> order(bodies.Size());
  std::iota(order.begin(), order.end(), size_t {});

  // As a client restores a snapshot: everything at once.
  PhysicsWorld restored;
  bodies.AddAll(restored, order);
  const std::vector<ProbeResult> expected = RunProbes(restored, probes);

  PhysicsWorld reversed;
  bodies.AddAll(reversed, std::vector<size_t>(order.rbegin(), order.rend()));
  ExpectSameResults(expected, RunProbes(reversed, probes), "reversed");

  std::vector<size_t> shuffled = order;
  std::shuffle(shuffled.begin(), shuffled.end(), std::mt19937(kSeed));
  PhysicsWorld shuffled_world;
  bodies.AddAll(shuffled_world, shuffled);
  ExpectSameResults(expected, RunProbes(shuffled_world, probes), "shuffled");

  // As the server builds: half the station, ticks, the rest, ticks, some blocks rebuilt.
  PhysicsWorld grown;
  const auto half = shuffled.begin() + static_cast<std::ptrdiff_t>(shuffled.size() / 2);
  bodies.AddAll(grown, std::span(shuffled.begin(), half));
  Reshape(grown);
  bodies.AddAll(grown, std::span(half, shuffled.end()));
  Reshape(grown);
  for (size_t i = 0; i < order.size(); i += kRebuiltEvery) {
    bodies.Remove(grown, shuffled[i]);
  }
  Reshape(grown);
  for (size_t i = 0; i < order.size(); i += kRebuiltEvery) {
    bodies.Add(grown, shuffled[i]);
  }
  Reshape(grown);
  ExpectSameResults(expected, RunProbes(grown, probes), "grown");
}

}  // namespace
}  // namespace z13::bullet_module
