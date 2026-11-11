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

// What a Bullet step costs with N dynamic items on a shipped station, resting and with M of them awake: the
// prototype of the items' physics (docs/dynamic-objects-plan.md, stage 0), since the game steps no dynamic bodies yet.
// The blocks are static bodies of a btDiscreteDynamicsWorld, the items 30 cm crates piled on the bottom deck's floor.

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <numeric>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <Eigen/Dense>
#include <bullet/btBulletDynamicsCommon.h>

#include "../../src/block_shape.h"

#include <primitives/geometry.h>
#include <primitives/palette.h>
#include <primitives/placement.h>
#include <z13/components/station/block.h>
#include <z13_tests/shipped_station.h>

namespace z13::bullet_module {
namespace {

using z13::building::primitives::BlockShapeKey;
using z13::building::primitives::BlockShapeKeyHash;

constexpr std::string_view kScene = "spire";
constexpr float kStepSeconds = 1.f / 60.f;  // the simulation's tick
constexpr float kGravity = -9.81f;
constexpr float kItemHalfExtent = 0.15f;
constexpr float kItemMass = 1.f;
constexpr float kRestGap = 1e-2f;  // above the floor, so a crate does not start inside it
constexpr float kItemPitch = 0.4f;  // between crates in a pile's layer, so they start apart
constexpr float kLayerPitch = 0.32f;
constexpr int kMaxLayers = 8;  // under the bottom deck's ceiling
constexpr int kSettleSteps = 900;  // 15 s for everything to fall asleep
constexpr int kMeasuredSteps = 60;
constexpr float kLaunchSpeed = 3.f;
constexpr float kLaunchRise = 2.f;
constexpr uint32_t kSeed = 13;
constexpr float kCentimetersPerMeter = 100.f;
constexpr double kMedian = 0.5;
constexpr double kP95 = 0.95;
constexpr std::array kItemCounts {1000, 5000, 20000};
constexpr std::array kAwakeCounts {100, 500, 2000};

template <typename F>
double Ms(F&& work) {
  const auto start = std::chrono::steady_clock::now();
  work();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

double Quantile(std::vector<double> values, double fraction) {
  std::ranges::sort(values);
  return values.empty() ? 0.0 : values[std::min(values.size() - 1, static_cast<size_t>(fraction * values.size()))];
}

// The station's blocks as static bodies; shapes are shared by block type and size.
class StaticStation {
 public:
  StaticStation(btDiscreteDynamicsWorld& world, const std::vector<z13::station::Block>& blocks,
                const z13::building::primitives::Palette& palette)
      : world_(world) {
    for (const z13::station::Block& block : blocks) {
      const BlockShapeKey key = z13::building::primitives::ShapeKeyOf(block.spec);
      auto& shape = shapes_[key];
      if (!shape) {
        const auto solids = z13::building::primitives::Scaled(
            z13::building::primitives::BlockSolids(palette, key), z13::station::kCellSize);
        shape = std::make_unique<BlockShape>(solids);
      }
      const Eigen::Isometry3f pose =
          z13::building::primitives::WorldPose(z13::building::primitives::PoseOf(block), z13::station::kCellSize);
      const Eigen::Quaternionf rotation(pose.rotation());
      btTransform transform;
      transform.setOrigin(btVector3(pose.translation().x(), pose.translation().y(), pose.translation().z()));
      transform.setRotation(btQuaternion(rotation.x(), rotation.y(), rotation.z(), rotation.w()));
      auto body = std::make_unique<btRigidBody>(btRigidBody::btRigidBodyConstructionInfo(0.f, nullptr, &shape->Shape()));
      body->setWorldTransform(transform);
      world_.get().addRigidBody(body.get());
      bodies_.push_back(std::move(body));
    }
  }

  ~StaticStation() {
    for (const auto& body : bodies_) {
      world_.get().removeRigidBody(body.get());
    }
  }

  StaticStation(const StaticStation&) = delete;
  StaticStation& operator=(const StaticStation&) = delete;

 private:
  std::reference_wrapper<btDiscreteDynamicsWorld> world_;
  std::unordered_map<BlockShapeKey, std::unique_ptr<BlockShape>, BlockShapeKeyHash> shapes_;
  std::vector<std::unique_ptr<btRigidBody>> bodies_;
};

// Where the crates start: lattice slots over the bottom deck's floor blocks, then layers of them upwards.
std::vector<btVector3> ItemSlots(const std::vector<z13::station::Block>& blocks, size_t count) {
  int lowest = std::numeric_limits<int>::max();
  for (const z13::station::Block& block : blocks) {
    lowest = std::min(lowest, z13::building::primitives::OccupiedCells(block).min.z());
  }
  std::vector<btVector3> floor;
  for (const z13::station::Block& block : blocks) {
    const z13::building::primitives::CellBox box = z13::building::primitives::OccupiedCells(block);
    if (box.min.z() != lowest || box.extent.z() != 1 || box.extent.x() < 2 || box.extent.y() < 2) {
      continue;
    }
    const Eigen::Vector3f low = box.min.cast<float>() * z13::station::kCellSize;
    const Eigen::Vector3f high = box.End().cast<float>() * z13::station::kCellSize;
    for (float x = low.x() + kItemPitch / 2; x < high.x(); x += kItemPitch) {
      for (float y = low.y() + kItemPitch / 2; y < high.y(); y += kItemPitch) {
        floor.emplace_back(x, y, high.z() + kItemHalfExtent + kRestGap);
      }
    }
  }
  std::vector<btVector3> slots;
  for (int layer = 0; layer < kMaxLayers && slots.size() < count; ++layer) {
    for (const btVector3& spot : floor) {
      slots.push_back(spot + btVector3(0.f, 0.f, static_cast<float>(layer) * kLayerPitch));
      if (slots.size() == count) {
        break;
      }
    }
  }
  return slots;
}

class ItemWorld {
 public:
  ItemWorld()
      : dispatcher_(&collision_config_),
        world_(&dispatcher_, &broadphase_, &solver_, &collision_config_),
        crate_(btVector3(kItemHalfExtent, kItemHalfExtent, kItemHalfExtent)) {
    world_.setGravity(btVector3(0.f, 0.f, kGravity));
  }

  btDiscreteDynamicsWorld& World() { return world_; }

  void AddItems(const std::vector<btVector3>& slots) {
    btVector3 inertia;
    crate_.calculateLocalInertia(kItemMass, inertia);
    for (const btVector3& slot : slots) {
      auto body = std::make_unique<btRigidBody>(btRigidBody::btRigidBodyConstructionInfo(kItemMass, nullptr, &crate_, inertia));
      btTransform transform;
      transform.setIdentity();
      transform.setOrigin(slot);
      body->setWorldTransform(transform);
      world_.addRigidBody(body.get());
      items_.push_back(std::move(body));
    }
  }

  ~ItemWorld() {
    for (const auto& body : items_) {
      world_.removeRigidBody(body.get());
    }
  }

  ItemWorld(const ItemWorld&) = delete;
  ItemWorld& operator=(const ItemWorld&) = delete;

  void Step() { world_.stepSimulation(kStepSeconds, 1, kStepSeconds); }

  size_t Awake() const {
    return static_cast<size_t>(std::ranges::count_if(items_, [](const auto& body) { return body->isActive(); }));
  }

  int Contacts() { return dispatcher_.getNumManifolds(); }

  // Steps until every item sleeps or `kSettleSteps` are used; returns the steps taken.
  int Settle() {
    int steps = 0;
    while (steps < kSettleSteps && Awake() > 0) {
      Step();
      ++steps;
    }
    return steps;
  }

  // Wakes `count` items picked by `random` and throws each in a random direction.
  void Launch(size_t count, std::mt19937& random) {
    std::vector<size_t> order(items_.size());
    std::iota(order.begin(), order.end(), size_t {});
    std::ranges::shuffle(order, random);
    std::uniform_real_distribution<float> angle(0.f, 2.f * std::numbers::pi_v<float>);
    for (size_t i = 0; i < std::min(count, order.size()); ++i) {
      btRigidBody& body = *items_[order[i]];
      const float theta = angle(random);
      body.activate(true);
      body.setLinearVelocity(btVector3(kLaunchSpeed * std::cos(theta), kLaunchSpeed * std::sin(theta), kLaunchRise));
    }
  }

 private:
  btDefaultCollisionConfiguration collision_config_;
  btCollisionDispatcher dispatcher_;
  btDbvtBroadphase broadphase_;
  btSequentialImpulseConstraintSolver solver_;
  btDiscreteDynamicsWorld world_;
  btBoxShape crate_;
  std::vector<std::unique_ptr<btRigidBody>> items_;
};

struct StepRow {
  size_t awake_after {};
  double median_ms {};
  double p95_ms {};
  double max_ms {};
  double mean_contacts {};
};

StepRow MeasureSteps(ItemWorld& world) {
  std::vector<double> times;
  double contacts = 0.0;
  for (int step = 0; step < kMeasuredSteps; ++step) {
    times.push_back(Ms([&] { world.Step(); }));
    contacts += world.Contacts();
  }
  return {.awake_after = world.Awake(),
          .median_ms = Quantile(times, kMedian),
          .p95_ms = Quantile(times, kP95),
          .max_ms = std::ranges::max(times),
          .mean_contacts = contacts / kMeasuredSteps};
}

TEST(ItemPhysicsBench, DISABLED_Step) {
  const auto palette = z13::testing::ShippedPalette();
  ASSERT_TRUE(palette.has_value()) << palette.error();
  const auto station = z13::testing::ShippedBlueprint(kScene, *palette);
  ASSERT_TRUE(station.has_value()) << station.error();

  std::cout << std::format("[ bench ] '{}', {} static blocks, {} Hz, crates {:.0f} cm\n", kScene, station->size(),
                           static_cast<int>(1.f / kStepSeconds), kItemHalfExtent * 2.f * kCentimetersPerMeter);
  std::cout << "[ bench ] items  awake  add ms  settle steps/ms  step med/p95/max ms  contacts  awake after\n";
  for (const int total : kItemCounts) {
    ItemWorld world;
    StaticStation blocks(world.World(), *station, *palette);
    const std::vector<btVector3> slots = ItemSlots(*station, static_cast<size_t>(total));
    if (slots.size() < static_cast<size_t>(total)) {
      std::cout << std::format("[ bench ] {} items: the floor holds only {}\n", total, slots.size());
    }
    const double add_ms = Ms([&] { world.AddItems(slots); });
    int settle_steps = 0;
    const double settle_ms = Ms([&] { settle_steps = world.Settle(); });
    std::mt19937 random(kSeed);
    std::vector<int> awake_counts {0};
    awake_counts.insert(awake_counts.end(), kAwakeCounts.begin(), kAwakeCounts.end());
    for (const int awake : awake_counts) {
      if (awake > total) {
        continue;
      }
      world.Launch(static_cast<size_t>(awake), random);
      const StepRow row = MeasureSteps(world);
      std::cout << std::format("[ bench ] {:>5}  {:>5}  {:>6.0f}  {:>5}/{:<8.0f}  {:>6.2f}/{:<6.2f}/{:<6.2f}  {:>8.0f}  {:>11}\n",
                               slots.size(), awake, add_ms, settle_steps, settle_ms, row.median_ms, row.p95_ms,
                               row.max_ms, row.mean_contacts, row.awake_after);
      world.Settle();
    }
  }
  std::cout << std::flush;
}

}  // namespace
}  // namespace z13::bullet_module
