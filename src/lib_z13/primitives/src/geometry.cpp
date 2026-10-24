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

#include <primitives/geometry.h>

#include <primitives_generated.h>

#include <primitives/panel_prism.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <numeric>
#include <utility>

namespace z13::building::primitives {

namespace {

using FaceIndices = std::vector<uint32_t>;

Eigen::Vector3f Centroid(const std::vector<Eigen::Vector3f>& vertices, const FaceIndices& indices) {
  Eigen::Vector3f sum = Eigen::Vector3f::Zero();
  for (const uint32_t index : indices) {
    sum += vertices[index];
  }
  return sum / static_cast<float>(indices.size());
}

// Orders a planar face's vertices counterclockwise seen from outside: around the face's
// centroid, with its normal pointing away from the (convex) solid's centroid.
FaceIndices Orient(const std::vector<Eigen::Vector3f>& vertices, FaceIndices face, const Eigen::Vector3f& solid_center) {
  const Eigen::Vector3f center = Centroid(vertices, face);
  Eigen::Vector3f normal =
      (vertices[face[1]] - vertices[face[0]]).cross(vertices[face[2]] - vertices[face[0]]).normalized();
  if (normal.dot(center - solid_center) < 0.f) {
    normal = -normal;
  }
  const Eigen::Vector3f u = (vertices[face[0]] - center).normalized();
  const Eigen::Vector3f v = normal.cross(u);
  std::ranges::sort(face, {}, [&](uint32_t index) {
    const Eigen::Vector3f offset = vertices[index] - center;
    return std::atan2(offset.dot(v), offset.dot(u));
  });
  return face;
}

ConvexSolid MakeSolid(std::vector<Eigen::Vector3f> vertices, std::vector<FaceIndices> faces, PartRole role) {
  FaceIndices all(vertices.size());
  for (uint32_t i = 0; i < all.size(); ++i) {
    all[i] = i;
  }
  const Eigen::Vector3f center = Centroid(vertices, all);
  for (FaceIndices& face : faces) {
    face = Orient(vertices, std::move(face), center);
  }
  return {.vertices = std::move(vertices), .faces = std::move(faces), .role = role};
}

// Corner i has bit 0/1/2 set where it takes max's X/Y/Z.
ConvexSolid MakeBox(const Eigen::Vector3f& min, const Eigen::Vector3f& max, PartRole role = PartRole::kBody) {
  std::vector<Eigen::Vector3f> corners;
  for (uint32_t i = 0; i < 8; ++i) {
    corners.emplace_back((i & 1) ? max.x() : min.x(), (i & 2) ? max.y() : min.y(), (i & 4) ? max.z() : min.z());
  }
  return MakeSolid(std::move(corners),
                   {{0, 2, 4, 6}, {1, 3, 5, 7}, {0, 1, 4, 5}, {2, 3, 6, 7}, {0, 1, 2, 3}, {4, 5, 6, 7}}, role);
}

ConvexSolid MakeWedge(const Eigen::Vector3f& s) {
  return MakeSolid({{0, 0, 0}, {s.x(), 0, 0}, {s.x(), 0, s.z()}, {0, s.y(), 0}, {s.x(), s.y(), 0}, {s.x(), s.y(), s.z()}},
                   {{0, 1, 3, 4}, {1, 2, 4, 5}, {0, 2, 3, 5}, {0, 1, 2}, {3, 4, 5}}, PartRole::kBody);
}

// The faces of a quad whose corners don't lie in one plane: two triangles along the diagonal that
// keeps the solid convex (the near face has the solid above it, the far face below).
std::vector<FaceIndices> CapFaces(const std::array<Eigen::Vector3f, 4>& points, uint32_t first, bool far) {
  const Eigen::Vector3f normal = (points[1] - points[0]).cross(points[2] - points[0]);
  const float side = normal.dot(points[3] - points[0]) * normal.y();
  if (side == 0.f) {
    return {{first, first + 1, first + 2, first + 3}};
  }
  if ((side > 0.f) == !far) {
    return {{first, first + 1, first + 2}, {first, first + 2, first + 3}};
  }
  return {{first, first + 1, first + 3}, {first + 1, first + 2, first + 3}};
}

// The section (x, z) runs (0, 0), (run, slant), (run, height), (0, height - slant), extended along
// Y; `cut_corners`, when set, put its four corners where a joint cut the ends (a triangle repeats
// its third), and `face_cuts` slope the faces along Y.
ConvexSolid MakePanel(
    const Eigen::Vector3f& s, float slant, const z13::station::PanelCorners& cut_corners,
    const z13::station::PanelFaceCuts& face_cuts) {
  const float thickness = s.z() - slant;
  std::vector<Eigen::Vector2f> section {
      Eigen::Vector2f {0, 0}, {s.x(), slant}, {s.x(), s.z()}, {0, thickness}};
  if (cut_corners != z13::station::PanelCorners {}) {
    for (size_t i = 0; i < section.size(); ++i) {
      section[i] =
          Eigen::Vector2f(static_cast<float>(cut_corners[2 * i]), static_cast<float>(cut_corners[(2 * i) + 1]));
    }
  }
  if (section[3] == section[2]) {
    section.pop_back();
  }
  const size_t count = section.size();
  const size_t far_cuts = face_cuts.size() / 2;
  std::vector<Eigen::Vector3f> corners;
  std::array<std::array<Eigen::Vector3f, 4>, 2> caps;
  for (const bool far : {false, true}) {
    for (size_t i = 0; i < count; ++i) {
      const float cut = static_cast<float>(face_cuts[(far ? far_cuts : 0) + i]);
      corners.emplace_back(section[i].x(), far ? s.y() - cut : cut, section[i].y());
      caps[far][i] = corners.back();
    }
  }
  std::vector<FaceIndices> faces;
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t next = (i + 1) % count;
    faces.push_back({i, next, static_cast<uint32_t>(count) + next, static_cast<uint32_t>(count) + i});
  }
  for (const bool far : {false, true}) {
    const uint32_t first = far ? static_cast<uint32_t>(count) : 0;
    if (count == kTriangleCorners) {
      faces.push_back({first, first + 1, first + 2});
      continue;
    }
    for (FaceIndices& cap : CapFaces(caps[far], first, far)) {
      faces.push_back(std::move(cap));
    }
  }
  return MakeSolid(std::move(corners), std::move(faces), PartRole::kBody);
}

ConvexSolid MakeCornerWedge(const Eigen::Vector3f& s) {
  return MakeSolid({{0, 0, 0}, {s.x(), 0, 0}, {s.x(), s.y(), 0}, {0, s.y(), 0}, {s.x(), s.y(), s.z()}},
                   {{0, 1, 2, 3}, {1, 2, 4}, {2, 3, 4}, {0, 1, 4}, {0, 3, 4}}, PartRole::kBody);
}

std::expected<std::vector<ConvexSolid>, std::string> MakeDoorFrame(
    const Eigen::Vector3i& size, const Eigen::Vector2i& opening) {
  const int width = opening.x();
  const int height = opening.y();
  if (width < 1 || height < 1 || width >= size.x() || height >= size.z() || (size.x() - width) % 2 != 0) {
    return std::unexpected(std::format(
        "door opening {}x{} doesn't fit a {}x{} frame with equal posts", width, height, size.x(), size.z()));
  }
  const Eigen::Vector3f s = size.cast<float>();
  const float left = static_cast<float>((size.x() - width) / 2);
  const float right = left + static_cast<float>(width);
  const float top = static_cast<float>(height);
  return std::vector<ConvexSolid> {
      MakeBox({0, 0, 0}, {left, s.y(), s.z()}),
      MakeBox({right, 0, 0}, {s.x(), s.y(), s.z()}),
      MakeBox({left, 0, top}, {right, s.y(), s.z()}),
      MakeBox({left, 0, 0}, {right, s.y(), top}, PartRole::kDoorLeaf),
  };
}

}  // namespace

// Temporary: shapes are built in code until they come from models in a later stage.
std::expected<std::vector<ConvexSolid>, std::string> BuildSolids(
    const Shape& shape, const Eigen::Vector3i& size, int slant, const z13::station::PanelCorners& corners,
    const z13::station::PanelFaceCuts& face_cuts) {
  if ((size.array() < 1).any()) {
    return std::unexpected(std::format("size {}x{}x{} has an empty axis", size.x(), size.y(), size.z()));
  }
  const Eigen::Vector3f extent = size.cast<float>();
  switch (shape.kind) {
    case ShapeKind::kBox:
      return std::vector<ConvexSolid> {MakeBox(Eigen::Vector3f::Zero(), extent)};
    case ShapeKind::kWedge:
      return std::vector<ConvexSolid> {MakeWedge(extent)};
    case ShapeKind::kCornerWedge:
      return std::vector<ConvexSolid> {MakeCornerWedge(extent)};
    case ShapeKind::kDoorFrame:
      return MakeDoorFrame(size, shape.door_opening);
    case ShapeKind::kPanel:
      return std::vector<ConvexSolid> {MakePanel(extent, static_cast<float>(slant), corners, face_cuts)};
  }
  return std::unexpected("unknown shape");
}

Mesh BuildMesh(std::span<const ConvexSolid> solids) {
  Mesh mesh;
  for (const ConvexSolid& solid : solids) {
    for (const FaceIndices& face : solid.faces) {
      const Eigen::Vector3f& origin = solid.vertices[face[0]];
      const Eigen::Vector3f normal =
          (solid.vertices[face[1]] - origin).cross(solid.vertices[face[2]] - origin).normalized();
      for (size_t i = 1; i + 1 < face.size(); ++i) {
        for (const uint32_t index : {face[0], face[i], face[i + 1]}) {
          const Eigen::Vector3f& position = solid.vertices[index];
          mesh.positions.push_back(position);
          mesh.normals.push_back(normal);
        }
      }
    }
  }
  return mesh;
}

size_t BlockShapeKeyHash::operator()(const BlockShapeKey& key) const {
  constexpr size_t kMultiplier = 31;
  const size_t sized =
      std::accumulate(key.size.begin(), key.size.end(), std::hash<uint32_t> {}(key.type_id),
                      [](size_t hash, int extent) { return hash * kMultiplier + std::hash<int> {}(extent); });
  const size_t slanted = sized * kMultiplier + std::hash<int> {}(key.slant);
  const size_t cornered =
      std::accumulate(key.corners.begin(), key.corners.end(), slanted,
                      [](size_t hash, int corner) { return hash * kMultiplier + std::hash<int> {}(corner); });
  return std::accumulate(key.face_cuts.begin(), key.face_cuts.end(), cornered,
                         [](size_t hash, int cut) { return hash * kMultiplier + std::hash<int> {}(cut); });
}

BlockShapeKey ShapeKeyOf(const z13::station::BlockSpec& spec) {
  return {.type_id = spec.type_id, .size = spec.size, .slant = spec.slant, .corners = spec.corners,
          .face_cuts = spec.face_cuts};
}

std::vector<ConvexSolid> BlockSolids(OptionalPalette palette, const BlockShapeKey& shape) {
  const auto primitive = palette ? palette->get().Find(shape.type_id) : std::nullopt;
  const Shape box {.kind = ShapeKind::kBox};
  auto solids =
      BuildSolids(primitive ? primitive->get().shape : box, shape.size, shape.slant, shape.corners, shape.face_cuts);
  if (!solids) {
    solids = BuildSolids(box, shape.size);
  }
  std::vector<ConvexSolid> built = solids.value_or(std::vector<ConvexSolid> {});
  if (primitive && primitive->get().Has(PrimitiveFlags::Door)) {
    std::ranges::for_each(built, [](ConvexSolid& solid) { solid.role = PartRole::kDoorLeaf; });
  }
  return built;
}

std::vector<ConvexSolid> Scaled(std::vector<ConvexSolid> solids, float factor) {
  std::ranges::for_each(solids, [factor](ConvexSolid& solid) {
    std::ranges::transform(solid.vertices, solid.vertices.begin(), [factor](const Eigen::Vector3f& vertex) {
      return Eigen::Vector3f(vertex * factor);
    });
  });
  return solids;
}

}  // namespace z13::building::primitives
