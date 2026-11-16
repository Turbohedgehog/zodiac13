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

#include "door_lamp.h"

#include <algorithm>
#include <array>
#include <utility>

#include <primitives/chunk_mesh.h>

namespace z13::door_render {

namespace {

constexpr float kLampSizeM = 0.12f;
constexpr float kLampBelowTopM = 0.3f;
constexpr float kLampAwayFromFaceM = 0.01f;
constexpr z13::Rgba kGreen {40, 220, 60, 255};
constexpr z13::Rgba kYellow {240, 200, 20, 255};
constexpr z13::Rgba kRed {230, 30, 30, 255};

// The twelve triangles of a box, normals outward.
void AppendBox(const Eigen::Vector3f& low, const Eigen::Vector3f& high, z13::render::SceneMesh& mesh) {
  const auto corner = [&](int index) {
    return Eigen::Vector3f((index & 1) != 0 ? high.x() : low.x(), (index & 2) != 0 ? high.y() : low.y(),
                           (index & 4) != 0 ? high.z() : low.z());
  };
  // Each face: four corner indices counter-clockwise from outside, and its normal.
  struct Face {
    std::array<int, 4> corners;
    Eigen::Vector3f normal;
  };
  const std::array<Face, 6> faces {{
      {{0, 2, 6, 4}, {-1, 0, 0}},
      {{1, 5, 7, 3}, {1, 0, 0}},
      {{0, 4, 5, 1}, {0, -1, 0}},
      {{2, 3, 7, 6}, {0, 1, 0}},
      {{0, 1, 3, 2}, {0, 0, -1}},
      {{4, 6, 7, 5}, {0, 0, 1}},
  }};
  for (const Face& face : faces) {
    for (const int index : {0, 1, 2, 0, 2, 3}) {
      mesh.positions.push_back(corner(face.corners[static_cast<size_t>(index)]));
      mesh.normals.push_back(face.normal);
    }
  }
}

}  // namespace

DoorSignal SignalOf(bool interlocked, bool may_use) {
  if (interlocked) {
    return DoorSignal::kLocked;
  }
  return may_use ? DoorSignal::kOpens : DoorSignal::kNoAccess;
}

z13::Rgba ColorOf(DoorSignal signal) {
  switch (signal) {
    case DoorSignal::kOpens:
      return kGreen;
    case DoorSignal::kNoAccess:
      return kYellow;
    case DoorSignal::kLocked:
      return kRed;
  }
  return kRed;
}

std::optional<z13::render::SceneMesh> BuildLampMesh(const z13::station::Block& block, DoorSignal signal,
                                                    z13::building::primitives::OptionalPalette palette) {
  const z13::building::primitives::Mesh leaf = z13::building::primitives::DoorLeafMesh(block, palette);
  if (leaf.positions.empty()) {
    return std::nullopt;
  }
  Eigen::AlignedBox3f bounds;
  for (const Eigen::Vector3f& position : leaf.positions) {
    bounds.extend(position * z13::station::kCellSize);
  }
  const Eigen::Vector3f size = bounds.sizes();
  // The leaf is thin along one horizontal axis; the lamps sit on its two faces at the middle of the leaf.
  const int thin = size.x() < size.y() ? 0 : 1;
  Eigen::Vector3f centre = bounds.center();
  centre.z() = bounds.max().z() - kLampBelowTopM;
  z13::render::SceneMesh mesh {.color = ColorOf(signal), .emissive = true};
  for (const float side : {-1.f, 1.f}) {
    Eigen::Vector3f at = centre;
    at[thin] += side * ((size[thin] / 2.f) + kLampAwayFromFaceM + (kLampSizeM / 2.f));
    const Eigen::Vector3f half(kLampSizeM / 2.f, kLampSizeM / 2.f, kLampSizeM / 2.f);
    AppendBox(at - half, at + half, mesh);
  }
  return mesh;
}

}  // namespace z13::door_render
