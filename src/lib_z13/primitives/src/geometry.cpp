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

#include <z13_primitives/geometry.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace z13::primitives {

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

// Projects onto the plane the normal is most nearly perpendicular to, in cells.
Eigen::Vector2f CellUv(const Eigen::Vector3f& position, const Eigen::Vector3f& normal) {
  const Eigen::Vector3f magnitude = normal.cwiseAbs();
  if (magnitude.x() >= magnitude.y() && magnitude.x() >= magnitude.z()) {
    return {position.y(), position.z()};
  }
  if (magnitude.y() >= magnitude.z()) {
    return {position.x(), position.z()};
  }
  return {position.x(), position.y()};
}

}  // namespace

std::expected<std::vector<ConvexSolid>, std::string> BuildSolids(const Shape& shape, const Eigen::Vector3i& size) {
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
          mesh.uvs.push_back(CellUv(position, normal));
        }
      }
    }
  }
  return mesh;
}

}  // namespace z13::primitives
