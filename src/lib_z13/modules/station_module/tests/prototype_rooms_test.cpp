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
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <z13/components/station/grid.h>
#include <rooms/room_cache.h>
#include <station_graph/graph.h>
#include <z13_tests/shipped_station.h>

#include "../../z13_module/tests/support/z13_test_world.h"

// The graph pipeline's output (docs/station-editor-plan.md): the game's rooms are the graph's nodes, its portals the edges.
// The scene is the one the pipeline wrote from assets/station/graph/prototype.json, which shares its name.
namespace z13::station {
namespace {

using z13::testing::Z13TestWorld;

// A cell inside a room, above its floor slab and below the lamp under its ceiling.
constexpr int kAboveFloor = 2;

class PrototypeRoomsTest : public ::testing::Test {
 protected:
  PrototypeRoomsTest()
      : world_({std::string(z13::testing::kStationSceneArg), std::string(z13::testing::kPrototypeGraph)}),
        graph_(z13::testing::ShippedGraph(z13::testing::kPrototypeGraph).value()) {
    world_.Tick();
  }

  const rooms::RoomGraph& Rooms() {
    return z13::station::MainGridEntity(world_.World()).get<rooms::RoomCache>().Current()->get();
  }

  std::optional<rooms::RoomId> RoomOf(std::string_view node_id) {
    const auto index = editor::graph::FindNode(graph_, node_id);
    if (!index) {
      return std::nullopt;
    }
    const editor::graph::RoomBox& box = graph_.nodes[*index].box;
    const Eigen::Vector3i inside {box.origin.x() + box.size.x() / 2, box.origin.y() + box.size.y() / 2,
                                  box.origin.z() + kAboveFloor};
    return Rooms().RoomAt(inside);
  }

  std::string PortalsText() {
    std::string text;
    for (const rooms::Portal& portal : Rooms().portals) {
      const Eigen::Vector3i& at = portal.opening.min;
      text += std::format("[rooms {} and {}, {} cells at {},{},{}] ", portal.a, portal.b, portal.area_cells, at.x(),
                          at.y(), at.z());
    }
    return text;
  }

  bool Joined(rooms::RoomId a, rooms::RoomId b) {
    return std::ranges::any_of(Rooms().portals, [&](const rooms::Portal& portal) {
      return std::min(portal.a, portal.b) == std::min(a, b) && std::max(portal.a, portal.b) == std::max(a, b);
    });
  }

  Z13TestWorld world_;
  editor::graph::Graph graph_;
};

TEST_F(PrototypeRoomsTest, EveryNodeIsARoomOfItsOwnAndThereAreNoOthers) {
  std::vector<rooms::RoomId> found;
  for (const editor::graph::Node& node : graph_.nodes) {
    const auto room = RoomOf(node.id);
    ASSERT_TRUE(room) << node.id;
    EXPECT_NE(*room, rooms::kVacuumRoom) << node.id;
    found.push_back(*room);
  }

  std::ranges::sort(found);
  EXPECT_EQ(std::ranges::adjacent_find(found), found.end());
  EXPECT_EQ(std::ranges::count_if(Rooms().rooms, [](const auto& entry) { return entry.first != rooms::kVacuumRoom; }),
            static_cast<std::ptrdiff_t>(graph_.nodes.size()));
}

TEST_F(PrototypeRoomsTest, EveryEdgeIsAPortalBetweenItsRoomsAndThereAreNoOthers) {
  for (const editor::graph::Edge& edge : graph_.edges) {
    const auto from = RoomOf(edge.from);
    const auto to = RoomOf(edge.to);
    ASSERT_TRUE(from && to) << edge.id;
    EXPECT_TRUE(Joined(*from, *to)) << edge.id;
  }

  EXPECT_EQ(Rooms().portals.size(), graph_.edges.size()) << PortalsText();
}

}  // namespace
}  // namespace z13::station
