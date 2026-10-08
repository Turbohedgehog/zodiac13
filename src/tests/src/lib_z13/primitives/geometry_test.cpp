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

#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <Eigen/Dense>

#include <primitives/geometry.h>

namespace z13::building::primitives {
namespace {

constexpr float kTolerance = 1e-4f;

struct Case {
  std::string name;
  Shape shape;
  Eigen::Vector3i size;
  float volume {};
};

std::vector<Case> Cases() {
  const Shape door {.kind = ShapeKind::kDoorFrame, .door_opening = {4, 8}};
  return {
      {"box", {.kind = ShapeKind::kBox}, {5, 1, 3}, 15.f},
      {"wedge", {.kind = ShapeKind::kWedge}, {4, 2, 3}, 12.f},
      {"corner wedge", {.kind = ShapeKind::kCornerWedge}, {4, 4, 3}, 16.f},
      {"door frame", door, {6, 1, 10}, 60.f},
  };
}

std::vector<ConvexSolid> Solids(const Case& c) {
  auto solids = BuildSolids(c.shape, c.size);
  EXPECT_TRUE(solids.has_value()) << c.name;
  return solids.value_or(std::vector<ConvexSolid> {});
}

// Divergence theorem over the triangles; positive only if they all face outward.
float SignedVolume(const Mesh& mesh) {
  float volume = 0.f;
  for (size_t i = 0; i < mesh.positions.size(); i += 3) {
    volume += mesh.positions[i].dot(mesh.positions[i + 1].cross(mesh.positions[i + 2])) / 6.f;
  }
  return volume;
}

TEST(PrimitiveGeometryTest, EverySolidIsClosed) {
  for (const Case& c : Cases()) {
    for (const ConvexSolid& solid : Solids(c)) {
      // Closed and consistently wound: each directed edge appears once, its reverse once.
      std::map<std::pair<uint32_t, uint32_t>, int> edges;
      for (const auto& face : solid.faces) {
        for (size_t i = 0; i < face.size(); ++i) {
          ++edges[{face[i], face[(i + 1) % face.size()]}];
        }
      }
      for (const auto& [edge, count] : edges) {
        EXPECT_EQ(count, 1) << c.name;
        EXPECT_EQ(edges.count({edge.second, edge.first}), 1u) << c.name;
      }
    }
  }
}

TEST(PrimitiveGeometryTest, NormalsPointOutward) {
  for (const Case& c : Cases()) {
    for (const ConvexSolid& solid : Solids(c)) {
      Eigen::Vector3f center = Eigen::Vector3f::Zero();
      for (const Eigen::Vector3f& vertex : solid.vertices) {
        center += vertex;
      }
      center /= static_cast<float>(solid.vertices.size());

      const std::vector<ConvexSolid> one {solid};
      const Mesh mesh = BuildMesh(one);
      for (size_t i = 0; i < mesh.positions.size(); ++i) {
        EXPECT_GT(mesh.normals[i].dot(mesh.positions[i] - center), 0.f) << c.name;
      }
    }
  }
}

TEST(PrimitiveGeometryTest, MeshVolumeMatchesTheShape) {
  for (const Case& c : Cases()) {
    EXPECT_NEAR(SignedVolume(BuildMesh(Solids(c))), c.volume, kTolerance) << c.name;
  }
}

TEST(PrimitiveGeometryTest, VerticesStayInsideTheSize) {
  for (const Case& c : Cases()) {
    for (const ConvexSolid& solid : Solids(c)) {
      for (const Eigen::Vector3f& vertex : solid.vertices) {
        EXPECT_TRUE((vertex.array() >= 0.f).all() && (vertex.array() <= c.size.cast<float>().array()).all())
            << c.name;
      }
    }
  }
}

TEST(PrimitiveGeometryTest, DoorLeafFillsTheOpening) {
  const auto solids = BuildSolids({.kind = ShapeKind::kDoorFrame, .door_opening = {4, 8}}, {6, 1, 10});
  ASSERT_TRUE(solids.has_value());
  int leaves = 0;
  for (const ConvexSolid& solid : *solids) {
    if (solid.role != PartRole::kDoorLeaf) {
      continue;
    }
    ++leaves;
    const std::vector<ConvexSolid> one {solid};
    EXPECT_NEAR(SignedVolume(BuildMesh(one)), 4.f * 1.f * 8.f, kTolerance);
  }
  EXPECT_EQ(leaves, 1);
}

TEST(PrimitiveGeometryTest, RejectsADoorOpeningThatDoesNotFit) {
  const Eigen::Vector3i size {6, 1, 10};
  EXPECT_FALSE(BuildSolids({.kind = ShapeKind::kDoorFrame, .door_opening = {6, 8}}, size).has_value());
  EXPECT_FALSE(BuildSolids({.kind = ShapeKind::kDoorFrame, .door_opening = {3, 8}}, size).has_value());
  EXPECT_FALSE(BuildSolids({.kind = ShapeKind::kDoorFrame, .door_opening = {4, 10}}, size).has_value());
  EXPECT_FALSE(BuildSolids({.kind = ShapeKind::kBox}, {0, 1, 1}).has_value());
}

}  // namespace
}  // namespace z13::building::primitives
