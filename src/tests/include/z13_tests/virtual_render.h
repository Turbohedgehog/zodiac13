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

#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <primitives/chunk_mesh.h>
#include <primitives/decor.h>
#include <primitives/draw_order.h>
#include <primitives/palette.h>
#include <lib_core/utils/camera_pose.h>
#include <lib_core/utils/math.h>
#include <primitives/placement.h>
#include <rooms/view_culling.h>
#include <z13/components/block.h>

// What the renderer picks to draw, without drawing it (docs/frame-time-plan.md): the station's
// chunks, glass and props as the renderer keeps them, and a count of what a view of them draws.
namespace z13::testing {

// A prop is culled by a box this far (meters) around its position: its model isn't loaded here.
inline constexpr float kPropHalfExtentM = 0.5f;

// A chunk as the renderer keeps it: where it is (meters) and what drawing it costs.
struct Chunk {
  Eigen::Vector3i key = Eigen::Vector3i::Zero();
  Eigen::AlignedBox3f bounds;
  size_t meshes {};
  size_t triangles {};
};

inline std::vector<Chunk> ChunksOf(
    const std::vector<z13::station::Block>& blocks, z13::building::primitives::OptionalPalette palette,
    int chunk_cells) {
  namespace primitives = z13::building::primitives;
  std::vector<Chunk> chunks;
  for (const auto& [key, chunk_blocks] : primitives::GroupByChunk(blocks, chunk_cells)) {
    const primitives::ChunkMesh mesh = primitives::BuildChunkMesh(chunk_blocks, palette);
    Chunk chunk {.key = key,
                 .bounds = {mesh.bounds.min.cast<float>() * z13::station::kCellSize,
                            mesh.bounds.End().cast<float>() * z13::station::kCellSize},
                 .meshes = mesh.parts.size()};
    for (const primitives::MaterialMesh& part : mesh.parts) {
      chunk.triangles += part.mesh.positions.size() / 3;
    }
    chunks.push_back(chunk);
  }
  std::ranges::sort(chunks, [](const Chunk& a, const Chunk& b) {
    return std::lexicographical_compare(a.key.begin(), a.key.end(), b.key.begin(), b.key.end());
  });
  return chunks;
}

// The station as the renderer shows it: the blocks the props don't stand in for, split into opaque
// and glass chunks, and the props' boxes.
struct Scene {
  std::vector<Chunk> opaque;
  std::vector<Chunk> glass;
  std::vector<Eigen::AlignedBox3f> props;
};

inline Scene SceneOf(const std::vector<z13::station::Block>& all, const z13::building::primitives::Palette& palette,
                     const z13::building::primitives::Decor& decor, int chunk_cells) {
  namespace primitives = z13::building::primitives;
  std::vector<uint32_t> hidden;
  for (const std::string& name : decor.replaces) {
    if (const auto primitive = palette.Find(name)) {
      hidden.push_back(primitive->get().id);
    }
  }
  std::vector<z13::station::Block> opaque;
  std::vector<z13::station::Block> glass;
  for (const z13::station::Block& block : all) {
    if (std::ranges::contains(hidden, block.spec.type_id)) {
      continue;
    }
    (primitives::IsTransparent(block, palette) ? glass : opaque).push_back(block);
  }
  Scene scene {.opaque = ChunksOf(opaque, palette, chunk_cells), .glass = ChunksOf(glass, palette, chunk_cells)};
  for (const primitives::Prop& prop : decor.props) {
    scene.props.push_back({prop.position.array() - kPropHalfExtentM, prop.position.array() + kPropHalfExtentM});
  }
  return scene;
}

inline z13::building::primitives::CellBox HullOf(const std::vector<z13::station::Block>& blocks) {
  Eigen::Vector3i min = Eigen::Vector3i::Constant(std::numeric_limits<int>::max());
  Eigen::Vector3i max = Eigen::Vector3i::Constant(std::numeric_limits<int>::min());
  for (const z13::station::Block& block : blocks) {
    const z13::building::primitives::CellBox box = z13::building::primitives::OccupiedCells(block);
    min = min.cwiseMin(box.min);
    max = max.cwiseMax(box.End());
  }
  return {.min = min, .extent = max - min};
}

// The transform of a player standing at `pose.eye` looking along it.
inline Eigen::Matrix4f PoseMatrix(const z13::CameraPose& pose) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  transform.block<3, 3>(0, 0) =
      Eigen::Quaternionf::FromTwoVectors(Eigen::Vector3f::UnitX(), z13::Forward(pose)).toRotationMatrix();
  z13::math::SetTranslation(pose.eye, transform);
  return transform;
}

struct Drawn {
  size_t chunks {};
  size_t meshes {};
  size_t triangles {};
  size_t glass {};
  size_t props {};
};

// The renderer's picking for one view: the opaque chunks in view, the glass in view sorted
// farthest first, and the props in view. `eye` in meters.
inline Drawn PickVisible(const Scene& scene, const z13::station::rooms::ViewCulling& culling,
                         const Eigen::Vector3f& eye) {
  Drawn drawn;
  for (const Chunk& chunk : scene.opaque) {
    if (culling.Visible(chunk.bounds)) {
      ++drawn.chunks;
      drawn.meshes += chunk.meshes;
      drawn.triangles += chunk.triangles;
    }
  }
  std::vector<z13::building::primitives::ChunkBounds> glass;
  for (const Chunk& chunk : scene.glass) {
    if (culling.Visible(chunk.bounds)) {
      glass.push_back({.key = chunk.key, .bounds = chunk.bounds});
      drawn.meshes += chunk.meshes;
      drawn.triangles += chunk.triangles;
    }
  }
  z13::building::primitives::SortFarthestFirst(glass, eye);
  drawn.glass = glass.size();
  drawn.props = std::ranges::count_if(scene.props, [&](const auto& box) { return culling.Visible(box); });
  return drawn;
}

}  // namespace z13::testing
