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

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <Eigen/Dense>

#include <lib_core/state/world_json_store.h>
#include <net_module/in_memory_transport.h>
#include <net_module/state_digest.h>
#include <z13/components/block_brush.h>
#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13/components/wall.h>

#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../z13_module/tests/support/test_network.h"
#include "../../z13_module/tests/support/world_json_test_helpers.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/wall_entities.h"
#include "support/station_builders.h"

// The Merge mode over a session: a client's two clicks merge two nodes on the server and in
// every world, with no resync.
namespace z13::building {
namespace {

using z13::station::BrushPreview;
using z13::station::WallAxis;
using z13::station::WallMergeDraft;
using z13::station::WallNode;
using z13::testing::kConnectArg;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kTestServerEndpoint;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;
using Keycode = z13::fbs::input::Keycode;

// The hotbar cell of the build tool, and CYCLE_TOOL_MODE's default key (building.fbs).
constexpr Keycode kToolKey = Keycode::KEY_1;
constexpr Keycode kModeKey = Keycode::KEY_V;
constexpr uint64_t kSettleTicks = 30;
constexpr uint64_t kMaxTicks = 600;
constexpr int kMaxModeTaps = 10;
// Turns the view down far enough to aim at another node than before.
constexpr int kLookDownPixels = 400;
constexpr int kThickness = 2;
constexpr int kStickLength = 8;
constexpr int kPartnerDistance = 10;

// Where the client's brush points before and after it looks down: the nodes go there.
struct Aims {
  Eigen::Vector3i first;
  Eigen::Vector3i second;
};

std::vector<uint32_t> NodeIds(flecs::world world) {
  std::vector<uint32_t> ids;
  world.query<const WallNode>().each([&ids](const WallNode& node) { ids.push_back(node.id); });
  return ids;
}

std::string Checkpoint(Z13TestWorld& test_world) {
  const auto json = z13::flecs_tools::WorldJsonStore::Save(test_world.World());
  EXPECT_TRUE(json.has_value()) << (json ? "" : json.error());
  return z13::testing::WithNormalizedSimulationTick(json.value_or(""));
}

// A wall from a node at `cell` to one `kPartnerDistance` cells away along y, `toward` being 1 or -1.
void AddWallFrom(flecs::world world, const Eigen::Vector3i& cell, int toward) {
  auto& counters = world.get_mut<z13::gameplay::IdCounters>();
  const uint32_t node = SpawnWallNode(world, counters, cell).get<WallNode>().id;
  const uint32_t partner =
      SpawnWallNode(world, counters, cell + Eigen::Vector3i(0, toward * kPartnerDistance, 0)).get<WallNode>().id;
  SpawnWallSegment(world, counters,
                   {.node_a = node, .node_b = partner, .stick_a = {WallAxis::kZ, kStickLength},
                       .stick_b = {WallAxis::kZ, kStickLength}, .thickness = kThickness});
}

class WallMergeNetTest : public ::testing::Test {
 protected:
  // One session: a client joins, takes the Merge mode and clicks twice, looking down between the
  // clicks. With `planted`, the server starts with a wall from each aimed cell; without, the run
  // only reports the cells aimed at.
  Aims Play(const std::optional<Aims>& planted) {
    network_ = std::make_shared<z13::net::InMemoryNetwork>();
    server_ = std::make_unique<Z13TestWorld>(z13::testing::WithServerArg(z13::testing::SiteArgs()), network_);
    if (planted) {
      AddWallFrom(server_->World(), planted->first, 1);
      AddWallFrom(server_->World(), planted->second, -1);
    }
    client_ = std::make_unique<Z13TestWorld>(
        std::vector<std::string> {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network_);
    EXPECT_TRUE(RunNetworkUntil(*network_, {*server_, *client_}, kNetTestDeltaTime, kMaxTicks, [&] {
      return client_->World().get<z13::net::ConnectionStatus>().state == z13::net::ConnectionState::kConnected &&
          z13::testing::BuildToolOf(client_->Player());
    }));
    const auto step = [&] { Run(1); };
    z13::testing::Tap(*client_, kToolKey, step);
    for (int taps = 0; taps < kMaxModeTaps && ModeOfClient() != ToolMode::kMerge; ++taps) {
      z13::testing::Tap(*client_, kModeKey, step);
    }
    EXPECT_EQ(ModeOfClient(), ToolMode::kMerge);
    Run(kSettleTicks);

    Aims aims {.first = BrushCell()};
    z13::testing::Click(*client_, Keycode::MOUSE_BUTTON_LEFT, step);
    Run(kSettleTicks);
    if (planted) {
      EXPECT_TRUE(client_->Player().has<WallMergeDraft>()) << "the first click never picked a node";
    }
    z13::input::MouseMoveEvent look;
    look.delta = {.x = 0, .y = kLookDownPixels};
    client_->EmitInput(look);
    Run(kSettleTicks);
    aims.second = BrushCell();
    z13::testing::Click(*client_, Keycode::MOUSE_BUTTON_LEFT, step);
    Run(kSettleTicks);
    return aims;
  }

  void Run(uint64_t ticks) {
    RunNetworkUntil(*network_, {*server_, *client_}, kNetTestDeltaTime, ticks, [] { return false; });
  }

  ToolMode ModeOfClient() { return z13::testing::BuildToolOf(client_->Player()).get<BuildTool>().mode; }

  Eigen::Vector3i BrushCell() {
    std::optional<Eigen::Vector3i> cell;
    client_->Player().children([&cell](flecs::entity child) {
      if (child.has<BrushPreview>()) {
        cell = child.get<BrushPreview>().block.cell;
      }
    });
    EXPECT_TRUE(cell.has_value()) << "the client has no brush preview";
    return cell.value_or(Eigen::Vector3i::Zero());
  }

  std::shared_ptr<z13::net::InMemoryNetwork> network_;
  std::unique_ptr<Z13TestWorld> server_;
  std::unique_ptr<Z13TestWorld> client_;
};

TEST_F(WallMergeNetTest, AClientsTwoClicksMergeTheNodesEverywhere) {
  const Aims aims = Play(std::nullopt);
  ASSERT_GE((aims.first - aims.second).cwiseAbs().maxCoeff(), 3) << "the look did not move the aim";

  Play(aims);
  EXPECT_EQ(NodeIds(server_->World()).size(), 3u) << "four nodes, two merged into one";
  EXPECT_EQ(NodeIds(client_->World()), NodeIds(server_->World()));
  EXPECT_FALSE(client_->Player().has<WallMergeDraft>());
  EXPECT_EQ(client_->World().get<z13::net::StateDigests>().resyncs, 0u);
  EXPECT_EQ(Checkpoint(*server_), Checkpoint(*client_));
}

}  // namespace
}  // namespace z13::building
