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
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <primitives/placement.h>
#include <primitives/station_assets.h>
#include <z13/components/gameplay/gameplay.h>
#include <z13/components/gameplay/net.h>
#include <z13/components/station/block.h>
#include <z13/components/station/block_changes.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/grid_pose.h>
#include <z13/components/station/state_changes.h>
#include <player/gameplay_entities.h>
#include <z13_settings/net_tuning.h>
#include <z13_settings/settings.h>

#include "../../../building_module/tests/support/station_builders.h"
#include "../../../z13_module/tests/support/building_test_helpers.h"
#include "../../../z13_module/tests/support/load_session.h"
#include "../../../z13_module/tests/support/z13_test_world.h"
#include <net_module/state_digest.h>

#include "../../src/sectors/block_sectors.h"
#include "../../src/sectors/interest_view.h"

// The hub's districts stand posed in the world: a client under interest events gets the blocks of the districts that
// touch its sectors, and the edits made in them.
namespace z13::net {
namespace {

using z13::station::Block;
using z13::testing::Z13TestWorld;

constexpr uint64_t kConnectTicks = 600;
constexpr uint64_t kSettleTicks = 30;
constexpr uint64_t kWalkTicks = 300;
constexpr uint64_t kJumpResyncTicks = 150;
constexpr z13::station::GridId kDistrict = 1;
constexpr uint32_t kOutsideActor = 900;
// Above the district's floor, in cells of the district.
const Eigen::Vector3i kAboveFloor {10, 10, 3};
const Eigen::Vector3i kNewBlockCell {10, 10, 2};

struct District {
  z13::station::Grid grid;
  std::vector<std::pair<std::string, Block>> blocks;
};

District DistrictOf(Z13TestWorld& server) {
  District district;
  server.World().query<const z13::station::Grid>().each([&](const z13::station::Grid& grid) {
    if (grid.id == kDistrict) {
      district.grid = grid;
    }
  });
  server.World().query<const Block>().each([&](flecs::entity e, const Block& block) {
    if (block.grid == kDistrict) {
      district.blocks.emplace_back(e.name().c_str(), block);
    }
  });
  return district;
}

bool Holds(Z13TestWorld& world, const std::string& name) {
  return world.World().lookup(name.c_str()).is_valid();
}

class InterestDistrictTest : public ::testing::Test {
 protected:
  InterestDistrictTest()
      : session_(
            2, {.walk = false, .look = false}, [](z13::Settings& settings) { settings.net->interest_events = true; },
            z13::building::primitives::kDefaultScene) {}

  void SetUp() override {
    ASSERT_TRUE(session_.Connect(kConnectTicks));
    Run(kSettleTicks);
    district_ = DistrictOf(session_.Server());
    ASSERT_FALSE(district_.blocks.empty());
  }

  void Run(uint64_t ticks) {
    for (uint64_t tick = 0; tick < ticks; ++tick) {
      session_.Tick();
    }
  }

  uint32_t PlayerOf(size_t client) {
    return session_.Client(client).World().get<z13::gameplay::LocalPlayer>().id.value();
  }

  Eigen::Vector3f DistrictPoint(const Eigen::Vector3i& cell) const {
    return z13::station::GridToWorld(district_.grid) * (cell.cast<float>() * z13::station::kCellSize);
  }

  // The client's player stands over the district's floor.
  void StandInTheDistrict(size_t client) {
    const flecs::entity player = session_.Server().World().lookup(z13::gameplay::PlayerEntityName(PlayerOf(client)).c_str());
    ASSERT_TRUE(player.is_valid());
    player.set(z13::testing::Facing(DistrictPoint(kAboveFloor), Eigen::Vector3f::UnitX()));
    Run(kSettleTicks);
  }

  // The district blocks that touch the view of the client's player.
  std::vector<std::string> InViewOf(size_t client) {
    flecs::world server = session_.Server().World();
    const auto view = ViewOf(server, PlayerOf(client));
    EXPECT_TRUE(view.has_value());
    const z13::station::GridPoses poses = z13::station::PosesOf(server.query<const z13::station::Grid>());
    const int sector_cells = server.get<z13::NetTuning>().sector_size_cells;
    std::vector<std::string> names;
    if (!view) {
      return names;
    }
    for (const auto& [name, block] : DistrictOf(session_.Server()).blocks) {
      const auto touched = BlockSectors(block, poses, sector_cells);
      if (std::ranges::any_of(touched, [&](const auto& sector) { return view->sectors.contains(sector); })) {
        names.push_back(name);
      }
    }
    return names;
  }

  z13::testing::LoadSession session_;
  District district_;
};

TEST_F(InterestDistrictTest, AClientInTheDistrictHoldsItsBlocks) {
  StandInTheDistrict(1);

  const std::vector<std::string> wanted = InViewOf(1);

  ASSERT_FALSE(wanted.empty());
  for (const std::string& name : wanted) {
    EXPECT_TRUE(Holds(session_.Client(1), name)) << name;
  }
}

TEST_F(InterestDistrictTest, AnEditInTheDistrictReachesTheClientHoldingIt) {
  StandInTheDistrict(1);
  flecs::world server = session_.Server().World();
  const std::string removed = InViewOf(1).front();
  ASSERT_TRUE(Holds(session_.Client(1), removed));

  {
    const z13::gameplay::ActorScope scope(server.get_mut<z13::gameplay::EditActor>(), kOutsideActor);
    const flecs::entity gone = server.lookup(removed.c_str());
    z13::station::DestroyBlock(gone, gone.get<Block>());
    z13::station::SpawnBlock(
        server.entity("DistrictEdit"),
        {.spec = z13::station::CubeSpec(), .cell = kNewBlockCell, .grid = kDistrict});
  }
  Run(kSettleTicks);

  EXPECT_FALSE(Holds(session_.Client(1), removed));
  EXPECT_TRUE(Holds(session_.Client(1), "DistrictEdit"));
}

TEST_F(InterestDistrictTest, AnEditInTheDistrictDoesNotReachAClientFarFromIt) {
  StandInTheDistrict(1);
  flecs::world server = session_.Server().World();
  const std::string added = "DistrictEdit";

  {
    const z13::gameplay::ActorScope scope(server.get_mut<z13::gameplay::EditActor>(), kOutsideActor);
    z13::station::SpawnBlock(
        server.entity(added.c_str()), {.spec = z13::station::CubeSpec(), .cell = kNewBlockCell, .grid = kDistrict});
  }
  Run(kSettleTicks);

  // Client 0 stands at the spawn, outside the sectors the district touches.
  const auto view = ViewOf(server, PlayerOf(0));
  ASSERT_TRUE(view.has_value());
  const std::vector<std::string> near_zero = InViewOf(0);
  EXPECT_EQ(Holds(session_.Client(0), added), !near_zero.empty());
}

// The wall segments of a district reach every client; the panels derived from them must not, outside its sectors.
TEST_F(InterestDistrictTest, AClientFarFromTheDistrictDerivesNoPanelOfIt) {
  StandInTheDistrict(1);
  Run(kSettleTicks);

  size_t district_blocks {};
  session_.Client(0).World().query_builder<const z13::station::Block>().build().each(
      [&district_blocks](const z13::station::Block& block) { district_blocks += block.grid == kDistrict ? 1 : 0; });
  EXPECT_EQ(district_blocks, 0u);
}

TEST_F(InterestDistrictTest, ClientsStayInSyncWithADistrictInTheirSectors) {
  StandInTheDistrict(1);
  // Standing the player there is a jump the other peers did not see: its client resyncs at the next digests.
  Run(kJumpResyncTicks);
  std::vector<uint64_t> resyncs_before;
  for (size_t index = 0; index < session_.ClientCount(); ++index) {
    resyncs_before.push_back(session_.Client(index).World().get<StateDigests>().resyncs);
  }
  Run(kWalkTicks);

  for (size_t index = 0; index < session_.ClientCount(); ++index) {
    const auto& digests = session_.Client(index).World().get<StateDigests>();
    EXPECT_GT(digests.checked, 0u) << "client " << index;
    EXPECT_EQ(digests.resyncs, resyncs_before[index]) << "client " << index;
  }
}

}  // namespace
}  // namespace z13::net
