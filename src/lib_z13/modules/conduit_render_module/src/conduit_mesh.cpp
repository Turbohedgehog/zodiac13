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

#include "conduit_mesh.h"

#include <array>

#include <z13/components/block.h>

namespace z13::conduit_render {

namespace {

constexpr int kAxes = 3;
constexpr std::array kSides = {-1, 1};
constexpr std::array kCornersFacingOut = {0, 1, 2, 0, 2, 3};
constexpr std::array kCornersFacingIn = {0, 2, 1, 0, 3, 2};

// The corners run (low, low), (high, low), (high, high), (low, high) in the two axes after `axis`, whose cross
// product is +axis: counter-clockwise seen from the positive side.
void AddFace(z13::render::SceneMesh& mesh, const Eigen::Vector3f& low, const Eigen::Vector3f& high, int axis, int side) {
  const int u = (axis + 1) % kAxes;
  const int v = (axis + 2) % kAxes;
  const float plane = side > 0 ? high[axis] : low[axis];
  std::array<Eigen::Vector3f, 4> corners;
  for (size_t i = 0; i < corners.size(); ++i) {
    corners[i][axis] = plane;
    corners[i][u] = i == 1 || i == 2 ? high[u] : low[u];
    corners[i][v] = i >= 2 ? high[v] : low[v];
  }
  const Eigen::Vector3f normal = Eigen::Vector3f::Unit(axis) * static_cast<float>(side);
  for (const int corner : side > 0 ? kCornersFacingOut : kCornersFacingIn) {
    mesh.positions.push_back(corners[static_cast<size_t>(corner)]);
    mesh.normals.push_back(normal);
  }
}

}  // namespace

z13::render::SceneMesh BuildConduitMesh(const z13::conduit::Conduit& conduit, const z13::conduit::ConduitKind& kind) {
  const float cell = z13::station::kCellSize;
  const float half_thickness = kind.thickness_cells * cell / 2.f;
  const Eigen::Vector3f centre = (conduit.cell.cast<float>() + Eigen::Vector3f::Constant(0.5f)) * cell;
  Eigen::Vector3f low = centre - Eigen::Vector3f::Constant(half_thickness);
  Eigen::Vector3f high = centre + Eigen::Vector3f::Constant(half_thickness);
  const auto axis = static_cast<int>(conduit.axis);
  low[axis] = static_cast<float>(conduit.cell[axis]) * cell;
  high[axis] = static_cast<float>(conduit.cell[axis] + conduit.length) * cell;

  z13::render::SceneMesh mesh {.color = kind.color};
  for (int face_axis = 0; face_axis < kAxes; ++face_axis) {
    for (const int side : kSides) {
      AddFace(mesh, low, high, face_axis, side);
    }
  }
  return mesh;
}

}  // namespace z13::conduit_render
