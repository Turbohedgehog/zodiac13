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

#include <vector>

#include <Eigen/Dense>

#include <z13/components/block.h>
#include <z13/components/conduit.h>
#include <z13/components/scene_mesh.h>

#include "../../building_module/tests/support/station_builders.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/conduit_mesh.h"

namespace z13::conduit_render {
namespace {

using z13::conduit::Conduit;
using z13::conduit::ConduitAxis;
using z13::conduit::ConduitKind;
using z13::render::SceneMesh;

constexpr float kCell = z13::station::kCellSize;
constexpr uint32_t kCable = 1;
constexpr int kBoxVertices = 36;

ConduitKind CableKind() {
  return {.id = kCable, .name = "Cable", .network = "power", .thickness_cells = 0.4f, .color = {200, 60, 40, 255}};
}

Eigen::AlignedBox3f BoundsOf(const SceneMesh& mesh) {
  Eigen::AlignedBox3f bounds;
  for (const Eigen::Vector3f& position : mesh.positions) {
    bounds.extend(position);
  }
  return bounds;
}

// Every triangle winds counter-clockwise seen from along its normal, so its cross product points out.
bool FacesOutward(const SceneMesh& mesh) {
  for (size_t i = 0; i + 2 < mesh.positions.size(); i += 3) {
    const Eigen::Vector3f cross =
        (mesh.positions[i + 1] - mesh.positions[i]).cross(mesh.positions[i + 2] - mesh.positions[i]);
    if (cross.dot(mesh.normals[i]) <= 0.f) {
      return false;
    }
  }
  return true;
}

TEST(ConduitMeshTest, ARunIsABoxAlongItsAxisThroughTheCentreOfItsCells) {
  const Conduit run {.kind = kCable, .cell = {2, 3, 4}, .axis = ConduitAxis::kY, .length = 3};

  const SceneMesh mesh = BuildConduitMesh(run, CableKind());

  ASSERT_EQ(mesh.positions.size(), static_cast<size_t>(kBoxVertices));
  EXPECT_EQ(mesh.normals.size(), mesh.positions.size());
  EXPECT_EQ(mesh.color, CableKind().color);
  const Eigen::AlignedBox3f bounds = BoundsOf(mesh);
  const float half = 0.4f * kCell / 2.f;
  EXPECT_TRUE(bounds.min().isApprox(Eigen::Vector3f(2.5f * kCell - half, 3.f * kCell, 4.5f * kCell - half)));
  EXPECT_TRUE(bounds.max().isApprox(Eigen::Vector3f(2.5f * kCell + half, 6.f * kCell, 4.5f * kCell + half)));
}

TEST(ConduitMeshTest, EveryFaceTurnsOutwardOnEveryAxis) {
  for (const ConduitAxis axis : {ConduitAxis::kX, ConduitAxis::kY, ConduitAxis::kZ}) {
    const SceneMesh mesh = BuildConduitMesh({.kind = kCable, .cell = {0, 0, 0}, .axis = axis, .length = 2}, CableKind());

    EXPECT_TRUE(FacesOutward(mesh));
  }
}

class ConduitMeshSystemTest : public ::testing::Test {
 protected:
  flecs::entity Lay(const Conduit& conduit) {
    const flecs::entity entity = world_.World().entity().set(conduit);
    world_.Tick();
    return entity;
  }

  z13::testing::Z13TestWorld world_ = z13::testing::StationWorld();
};

TEST_F(ConduitMeshSystemTest, ALaidConduitGetsItsMeshAndAChangedOneANewRevision) {
  const flecs::entity entity = Lay({.kind = kCable, .cell = {0, 0, 0}, .axis = ConduitAxis::kX, .length = 2});

  ASSERT_TRUE(entity.has<SceneMesh>());
  const uint64_t first = entity.get<SceneMesh>().revision;
  world_.Tick();
  EXPECT_EQ(entity.get<SceneMesh>().revision, first);

  entity.set(Conduit {.kind = kCable, .cell = {0, 0, 0}, .axis = ConduitAxis::kX, .length = 5});
  world_.Tick();
  EXPECT_GT(entity.get<SceneMesh>().revision, first);
  EXPECT_NEAR(BoundsOf(entity.get<SceneMesh>()).max().x(), 5.f * kCell, 1e-5f);
}

TEST_F(ConduitMeshSystemTest, AConduitOfAnUnknownKindHasNoMesh) {
  const flecs::entity entity = Lay({.kind = 999, .cell = {0, 0, 0}, .axis = ConduitAxis::kZ, .length = 1});

  EXPECT_FALSE(entity.has<SceneMesh>());
}

}  // namespace
}  // namespace z13::conduit_render
