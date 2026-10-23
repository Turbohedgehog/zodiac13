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

#include "lamp_flood.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace z13::station::light {

namespace {

// A path along the cube grid is at most √2 times as long as the straight line in a plane, so
// shortened by that it only exceeds the straight distance where it has to go round something.
constexpr float kPathShortening = 1.f / std::numbers::sqrt2_v<float>;

struct QueuedNode {
  uint32_t node {};
  uint32_t steps {};
  float transmission = 1.f;
};

}  // namespace

std::optional<Eigen::Vector3f> EmitterOf(const LightNodes& nodes, const Lamp& lamp) {
  for (const Eigen::Vector3f& side : {lamp.position, lamp.behind}) {
    if (nodes.NodeAt(side.array().floor().cast<int>())) {
      return side;
    }
  }
  return std::nullopt;
}

std::vector<Contribution> FloodLamp(const LightNodes& nodes, const GlassBridges& bridges, const Lamp& lamp,
                                    const Eigen::Vector3f& emitter, FloodMarks& marks) {
  const auto seed = nodes.NodeAt(emitter.array().floor().cast<int>());
  if (!seed || lamp.radius <= 0.f) {
    return {};
  }
  marks.flood_of_node.resize(nodes.Layout().CubeCount() * kPiecesPerNode);
  const uint32_t flood = ++marks.last_flood;
  const float radius_squared = lamp.radius * lamp.radius;
  std::vector<Contribution> reached;
  std::vector<QueuedNode> queue {{.node = static_cast<uint32_t>(*seed)}};
  marks.flood_of_node[*seed] = flood;
  // Past a pane the flood waits until the open ways are used up, so a lit room's nodes
  // never take their light through its window from the dimmer side.
  std::vector<QueuedNode> behind_glass;
  for (size_t head = 0;;) {
    if (head == queue.size()) {
      std::ranges::sort(behind_glass, [](const QueuedNode& a, const QueuedNode& b) {
        return a.transmission != b.transmission ? a.transmission > b.transmission : a.steps < b.steps;
      });
      for (const QueuedNode& waiting : behind_glass) {
        if (marks.flood_of_node[waiting.node] != flood) {
          marks.flood_of_node[waiting.node] = flood;
          queue.push_back(waiting);
        }
      }
      behind_glass.clear();
      if (head == queue.size()) {
        break;
      }
    }
    const QueuedNode current = queue[head++];
    const Eigen::Vector3f& centre = nodes.Centre(current.node);
    const float dx = centre.x() - emitter.x();
    const float dy = centre.y() - emitter.y();
    const float dz = centre.z() - emitter.z();
    const float straight = std::sqrt((dx * dx) + (dy * dy) + (dz * dz));
    const float path = static_cast<float>(current.steps * kNodeCells) * kPathShortening;
    const float distance = std::max(straight, path);
    if (distance >= lamp.radius) {
      continue;
    }
    const float fade = 1.f - (distance * distance / radius_squared);
    reached.push_back({.node = current.node, .weight = fade * fade * current.transmission});
    for (const Bridge& bridge : bridges.From(current.node)) {
      behind_glass.push_back({.node = bridge.node,
                              .steps = current.steps + 1,
                              .transmission = current.transmission * bridge.transmission});
    }
    const uint32_t links = nodes.Links(current.node);
    if (links == 0) {
      continue;
    }
    const size_t cube = current.node / kPiecesPerNode;
    for (size_t side = 0; side < kCubeSides; ++side) {
      const uint32_t pieces = (links >> (side * kPiecesPerNode)) & ((1u << kPiecesPerNode) - 1);
      if (pieces == 0) {
        continue;
      }
      const size_t next_cube = nodes.NeighbourCube(cube, side);
      for (uint8_t piece = 0; piece < kPiecesPerNode; ++piece) {
        if ((pieces & (1u << piece)) == 0) {
          continue;
        }
        const auto next = static_cast<uint32_t>(LightLayout::NodeIndex(next_cube, piece));
        if (marks.flood_of_node[next] != flood) {
          marks.flood_of_node[next] = flood;
          queue.push_back({.node = next, .steps = current.steps + 1, .transmission = current.transmission});
        }
      }
    }
  }
  return reached;
}

}  // namespace z13::station::light
