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
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/world_serializer.h>
#include <sectors/sector.h>
#include <sectors/sector_index.h>
#include <z13/components/block.h>
#include <z13/components/block_changes.h>
#include <z13/components/gameplay.h>
#include <z13/components/inventory.h>
#include <primitives/placement.h>
#include <z13/components/net.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/building_tuning.h>
#include <z13_settings/net_tuning.h>
#include <z13_settings/settings.h>
#include <z13_tests/shipped_station.h>

#include "../../../building_module/tests/support/station_builders.h"
#include "../../../station_module/tests/support/room_blocks.h"
#include "../../../z13_module/tests/support/building_test_helpers.h"
#include "../../../z13_module/tests/support/load_session.h"
#include "../../../z13_module/tests/support/z13_test_world.h"
#include <net_module/state_digest.h>

#include "../../src/catch_up.h"
#include "../../src/interest_view.h"
#include "../../src/interest_deltas.h"
#include "../../src/interest_events.h"
#include "../../src/net_session.h"
#include "../../src/session_deltas.h"
#include "../../src/wire_records.h"

namespace z13::net {
namespace {

namespace ft = z13::flecs_tools;
using z13::station::Block;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;

constexpr int kNearbySector = 1;
constexpr int kFarSector = 40;
constexpr int kStrayRow = 5;
// Past an index chunk from row 1, within the same sector.
constexpr int kOtherChunkRow = 40;
constexpr uint32_t kEntityPlayer = 40;
constexpr float kBuilderHeightMeters = 1.5f;

bool HasBlock(Z13TestWorld& world, const std::string& name) {
  return world.World().lookup(name.c_str()).is_valid();
}

flecs::entity AddBlockInSector(Z13TestWorld& world, int sector_x, int row = 1) {
  const int sector_cells = world.World().get<z13::NetTuning>().sector_size_cells;
  return z13::station::testing::AddBlock(
      world, {.spec = z13::station::CubeSpec(), .cell = Eigen::Vector3i(sector_x * sector_cells + 1, row, 1)});
}

// A floor of the longest length a block may have, anchored in `sector` and reaching into the next one along x.
flecs::entity AddFloorIntoNextSector(Z13TestWorld& world, const sectors::SectorKey& sector) {
  flecs::world w = world.World();
  const int sector_cells = w.get<z13::NetTuning>().sector_size_cells;
  const int length = w.get<z13::BuildingTuning>().max_block_cells;
  const Eigen::Vector3i cell = sector * sector_cells + Eigen::Vector3i(sector_cells - length / 2, 1, 1);
  return z13::station::testing::AddBlock(
      world, {.spec = {.type_id = z13::station::testing::IdOf(z13::station::testing::kFloor), .size = {length, 1, 1}},
              .cell = cell});
}

uint64_t ChunkIdOf(Z13TestWorld& world, const flecs::entity block) {
  const int chunk_cells = world.World().get<z13::BuildingTuning>().index_chunk_cells;
  return z13::building::primitives::PackChunk(z13::building::primitives::ChunkOf(block.get<Block>().cell, chunk_cells));
}

// Entering `sector_x` with the groups of the chunks anchored in it.
fbs::net::SectorEnteredT EnteredSector(Z13TestWorld& world, int sector_x) {
  flecs::world w = world.World();
  const auto snapshot = ft::CaptureState(w);
  EXPECT_TRUE(snapshot.has_value());
  const int chunk_cells = w.get<z13::BuildingTuning>().index_chunk_cells;
  const int sector_cells = w.get<z13::NetTuning>().sector_size_cells;
  fbs::net::SectorEnteredT entered;
  entered.sector_x = sector_x;
  ft::WorldSnapshot blocks;
  for (const auto& group : snapshot->groups) {
    if (sectors::SectorOf(z13::building::primitives::UnpackChunk(group->id) * chunk_cells, sector_cells).x() ==
        sector_x) {
      blocks.groups.push_back(group);
      entered.chunks.push_back(group->id);
    }
  }
  entered.blocks = ToWire(blocks);
  return entered;
}

TEST(InterestDeltasTest, ASectorEnteredReplacesTheBlocksOfThatSectorOnly) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  const std::string brought = AddBlockInSector(world, kNearbySector).name().c_str();
  const fbs::net::SectorEnteredT entered = EnteredSector(world, kNearbySector);
  const std::string stray = AddBlockInSector(world, kNearbySector, kStrayRow).name().c_str();
  const flecs::entity gone = world.World().lookup(brought.c_str());
  z13::station::DestroyBlock(gone, gone.get<Block>());
  const std::string elsewhere = AddBlockInSector(world, kFarSector).name().c_str();
  world.Tick();

  ApplySectorEntered(world.World(), entered);

  EXPECT_TRUE(HasBlock(world, brought));
  EXPECT_FALSE(HasBlock(world, stray));
  EXPECT_TRUE(HasBlock(world, elsewhere));
}

// One the server removed while the client didn't hold the sector, which the event therefore doesn't bring.
TEST(InterestDeltasTest, ASectorEnteredRemovesABlockOfAnotherSectorReachingIntoIt) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  const std::string reaching = AddFloorIntoNextSector(world, {kNearbySector - 1, 0, 0}).name().c_str();
  const std::string elsewhere = AddBlockInSector(world, kFarSector).name().c_str();
  world.Tick();
  fbs::net::SectorEnteredT entered;
  entered.sector_x = kNearbySector;

  ApplySectorEntered(world.World(), entered);

  EXPECT_FALSE(HasBlock(world, reaching));
  EXPECT_TRUE(HasBlock(world, elsewhere));
}

TEST(InterestDeltasTest, ASectorLeftRemovesTheBlocksOfTheChunksItNames) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  const flecs::entity here = AddBlockInSector(world, kNearbySector);
  const std::string here_name = here.name().c_str();
  const std::string kept = AddBlockInSector(world, kNearbySector, kOtherChunkRow).name().c_str();
  const std::string elsewhere = AddBlockInSector(world, kFarSector).name().c_str();
  world.Tick();
  fbs::net::SectorLeftT left;
  left.sector_x = kNearbySector;
  left.chunks = {ChunkIdOf(world, world.World().lookup(here_name.c_str()))};

  ApplySectorLeft(world.World(), left);

  EXPECT_FALSE(HasBlock(world, here_name));
  EXPECT_TRUE(HasBlock(world, kept));
  EXPECT_TRUE(HasBlock(world, elsewhere));
}

TEST(InterestDeltasTest, AnEntityLeftRemovesThePlayerAndAnEntityEnteredBringsItBack) {
  Z13TestWorld world = StationWorld();
  const flecs::entity builder = z13::testing::AddBuilder(world, kEntityPlayer, Eigen::Matrix4f::Identity());
  const std::string name = z13::gameplay::PlayerEntityName(kEntityPlayer);
  fbs::net::EntityEnteredT entered;
  const auto state = ft::CaptureEntityState(world.World(), builder);
  ASSERT_TRUE(state.has_value());
  entered.entity_state = ToWire(*state);
  fbs::net::EntityLeftT left;
  left.player_id = kEntityPlayer;

  ApplyEntityLeft(world.World(), left);
  EXPECT_FALSE(HasBlock(world, name));

  ApplyEntityEntered(world.World(), entered);
  EXPECT_TRUE(HasBlock(world, name));
}

constexpr uint64_t kConnectTicks = 600;
constexpr uint64_t kSettleTicks = 30;
// A catch-up starts from a snapshot older than the late window, so what it holds is that old.
constexpr uint64_t kSnapshotAgeTicks = 250;

class InterestSessionTest : public ::testing::Test {
 protected:
  explicit InterestSessionTest(bool walk)
      : session_(2, {.walk = walk, .look = false},
                 [](z13::Settings& settings) { settings.net->interest_events = true; }) {}

  void SetUp() override {
    ASSERT_TRUE(session_.Connect(kConnectTicks));
    Run(kSettleTicks);
  }

  void Run(uint64_t ticks) {
    for (uint64_t tick = 0; tick < ticks; ++tick) {
      session_.Tick();
    }
  }

  static uint32_t PlayerOf(Z13TestWorld& client) { return client.World().get<z13::gameplay::LocalPlayer>().id.value(); }

  // Client 1's player walks out of reach of client 0 and comes back; returns its id.
  uint32_t LeaveAndReenterThePresenceOfTheOtherClient() {
    const uint32_t walker = PlayerOf(session_.Client(1));
    const flecs::entity player = session_.Server().World().lookup(z13::gameplay::PlayerEntityName(walker).c_str());
    const flecs::entity watcher = session_.Server().World().lookup(
        z13::gameplay::PlayerEntityName(PlayerOf(session_.Client(0))).c_str());
    EXPECT_TRUE(player.is_valid() && watcher.is_valid());
    const int sector_cells = session_.Server().World().get<z13::NetTuning>().sector_size_cells;
    const float far_meters = static_cast<float>(kFarSector * sector_cells) * z13::station::kCellSize;
    player.set(z13::testing::Facing({far_meters, 0.f, kBuilderHeightMeters}, Eigen::Vector3f::UnitX()));
    Run(kSettleTicks);
    player.set(watcher.get<Eigen::Matrix4f>());
    Run(kSettleTicks);
    return walker;
  }

  z13::testing::LoadSession session_;
};

// Both clients walk, so their commands flow.
class InterestEventsTest : public InterestSessionTest {
 protected:
  InterestEventsTest() : InterestSessionTest(true) {}
};

// Both clients stand still, so a position set on the server stays.
class InterestStillTest : public InterestSessionTest {
 protected:
  InterestStillTest() : InterestSessionTest(false) {}
};

template <typename Event>
bool Received(Z13TestWorld& client, const std::function<bool(const Event&)>& matches) {
  const auto& deltas = client.World().get<ScheduledSessionDeltas>();
  const auto holds = [&](const ScheduledSessionDelta& item) {
    const auto* event = std::get_if<Event>(&item.delta);
    return event && matches(*event);
  };
  return std::ranges::any_of(deltas.pending, holds) || std::ranges::any_of(deltas.history, holds);
}

TEST_F(InterestEventsTest, APlayerWhoWalksOutOfReachIsRemovedFromTheOtherClient) {
  const uint32_t gone = PlayerOf(session_.Client(1));
  const flecs::entity player = session_.Server().World().lookup(z13::gameplay::PlayerEntityName(gone).c_str());
  ASSERT_TRUE(player.is_valid());
  const int sector_cells = session_.Server().World().get<z13::NetTuning>().sector_size_cells;
  const float far_meters = static_cast<float>(kFarSector * sector_cells) * z13::station::kCellSize;

  player.set(z13::testing::Facing({far_meters, 0.f, kBuilderHeightMeters}, Eigen::Vector3f::UnitX()));
  Run(kSettleTicks);

  EXPECT_TRUE(Received<fbs::net::EntityLeftT>(session_.Client(0), [gone](const auto& e) { return e.player_id == gone; }));
}

TEST_F(InterestEventsTest, APlayerEnteringAClientsPresenceBringsItsHeldInput) {
  const uint32_t walker = LeaveAndReenterThePresenceOfTheOtherClient();

  EXPECT_TRUE(Received<fbs::net::EntityEnteredT>(session_.Client(0), [walker](const auto& e) {
    return !e.held_values.empty() && e.held_values.front().player_id() == walker;
  }));
}

size_t ItemsCarriedBy(Z13TestWorld& world, uint32_t player_id) {
  size_t items = 0;
  world.World().query<const z13::inventory::Item, const z13::inventory::CarriedBy>().each(
      [&](const z13::inventory::Item&, const z13::inventory::CarriedBy& carried) {
        items += carried.player_id == player_id ? 1 : 0;
      });
  return items;
}

TEST_F(InterestStillTest, APlayerEnteringAClientsPresenceBringsItsItems) {
  const uint32_t walker = LeaveAndReenterThePresenceOfTheOtherClient();

  EXPECT_GT(ItemsCarriedBy(session_.Server(), walker), 0u);
  EXPECT_EQ(ItemsCarriedBy(session_.Client(0), walker), ItemsCarriedBy(session_.Server(), walker));
}

std::optional<uint8_t> ActiveCellOf(Z13TestWorld& world, uint32_t player_id) {
  const flecs::entity player = world.World().lookup(z13::gameplay::PlayerEntityName(player_id).c_str());
  if (!player.is_valid() || !player.has<z13::inventory::Hotbar>()) {
    return std::nullopt;
  }
  return player.get<z13::inventory::Hotbar>().active_cell;
}

TEST_F(InterestStillTest, TheToolKeyOfAPlayerWhoJustEnteredDoesTheSameOnTheOtherClient) {
  const uint32_t walker = LeaveAndReenterThePresenceOfTheOtherClient();
  const std::optional<uint8_t> before = ActiveCellOf(session_.Server(), walker);

  z13::testing::Tap(session_.Client(1), z13::fbs::input::Keycode::KEY_TAB, [this] { session_.Tick(); });
  Run(kSettleTicks);

  const std::optional<uint8_t> after = ActiveCellOf(session_.Server(), walker);
  ASSERT_TRUE(after.has_value());
  EXPECT_NE(after, before);
  EXPECT_EQ(ActiveCellOf(session_.Client(0), walker), after);
}

uint64_t RelayedCommands(Z13TestWorld& server) {
  const auto& by_message = server.World().get<NetSession>().Sent().by_message;
  const auto found = by_message.find(fbs::net::MessageBody::SequencedCommands);
  return found == by_message.end() ? 0 : found->second.packets;
}

TEST_F(InterestEventsTest, CommandsOfPlayersFarApartAreNotRelayedToEachOther) {
  const uint32_t mover = PlayerOf(session_.Client(1));
  const flecs::entity player = session_.Server().World().lookup(z13::gameplay::PlayerEntityName(mover).c_str());
  ASSERT_TRUE(player.is_valid());
  const uint64_t near_before = RelayedCommands(session_.Server());
  Run(kSettleTicks);
  const uint64_t relayed_near = RelayedCommands(session_.Server()) - near_before;

  const int sector_cells = session_.Server().World().get<z13::NetTuning>().sector_size_cells;
  const float far_meters = static_cast<float>(kFarSector * sector_cells) * z13::station::kCellSize;
  player.set(z13::testing::Facing({far_meters, 0.f, kBuilderHeightMeters}, Eigen::Vector3f::UnitX()));
  Run(kSettleTicks);
  Run(kSettleTicks);
  const uint64_t far_before = RelayedCommands(session_.Server());
  Run(kSettleTicks);

  EXPECT_GT(relayed_near, 0u);
  EXPECT_LT(RelayedCommands(session_.Server()) - far_before, relayed_near);
}

bool CatchUpHas(const std::unique_ptr<fbs::net::CatchUpT>& catch_up, const std::string& name) {
  const auto payload = DecodeCatchUp(catch_up);
  EXPECT_TRUE(payload.has_value());
  const ft::EntityRefs entities = ft::AllEntities(payload->snapshot);
  return std::ranges::any_of(entities, [&name](const ft::EntitySnapshot& entity) { return entity.name == name; });
}

TEST_F(InterestEventsTest, WalkingClientsInOneHallStayInSyncWithoutAResync) {
  Run(kSnapshotAgeTicks);

  for (size_t index = 0; index < session_.ClientCount(); ++index) {
    const auto& digests = session_.Client(index).World().get<StateDigests>();
    EXPECT_GT(digests.checked, 0u) << "client " << index;
    EXPECT_EQ(digests.resyncs, 0u) << "client " << index;
  }
}

TEST_F(InterestStillTest, AResyncCarriesOnlyWhatTheClientHas) {
  flecs::world server = session_.Server().World();
  const uint32_t watcher = PlayerOf(session_.Client(0));
  const uint32_t mover = PlayerOf(session_.Client(1));
  const int sector_cells = server.get<z13::NetTuning>().sector_size_cells;
  const float far_meters = static_cast<float>(kFarSector * sector_cells) * z13::station::kCellSize;
  server.lookup(z13::gameplay::PlayerEntityName(mover).c_str())
      .set(z13::testing::Facing({far_meters, 0.f, kBuilderHeightMeters}, Eigen::Vector3f::UnitX()));
  Run(kSnapshotAgeTicks);

  const auto view = ViewOf(server, watcher);
  ASSERT_TRUE(view.has_value());
  const auto whole = MakeCatchUp(server);
  const auto filtered = MakeCatchUp(server, view);
  ASSERT_TRUE(whole.has_value() && filtered.has_value());

  const std::string mover_name = z13::gameplay::PlayerEntityName(mover);
  EXPECT_TRUE(CatchUpHas(*whole, mover_name));
  EXPECT_FALSE(CatchUpHas(*filtered, mover_name));
  EXPECT_TRUE(CatchUpHas(*filtered, z13::gameplay::PlayerEntityName(watcher)));
}

TEST_F(InterestStillTest, AFilteredSnapshotKeepsTheBlocksOfTheViewOnly) {
  flecs::world server = session_.Server().World();
  const std::string near_block = AddBlockInSector(session_.Server(), 0).name().c_str();
  const std::string far_block = AddBlockInSector(session_.Server(), kFarSector, kStrayRow).name().c_str();
  Run(kSettleTicks);
  const auto view = ViewOf(server, PlayerOf(session_.Client(0)));
  ASSERT_TRUE(view.has_value());
  const auto snapshot = ft::CaptureState(server);
  ASSERT_TRUE(snapshot.has_value());

  const ft::WorldSnapshot filtered = FilterSnapshot(server, *snapshot, *view);

  const auto has = [&filtered](const std::string& name) {
    return std::ranges::any_of(
        ft::AllEntities(filtered), [&name](const ft::EntitySnapshot& e) { return e.name == name; });
  };
  EXPECT_TRUE(has(near_block));
  EXPECT_FALSE(has(far_block));
}

TEST_F(InterestStillTest, AFilteredSnapshotKeepsABlockReachingIntoTheViewFromOutside) {
  flecs::world server = session_.Server().World();
  const auto view = ViewOf(server, PlayerOf(session_.Client(0)));
  ASSERT_TRUE(view.has_value());
  const auto westmost =
      std::ranges::min(view->sectors, {}, [](const sectors::SectorKey& sector) { return sector.x(); });
  const flecs::entity reaching = AddFloorIntoNextSector(session_.Server(), westmost - sectors::SectorKey::UnitX());
  const std::string name = reaching.name().c_str();
  Run(kSettleTicks);
  const auto snapshot = ft::CaptureState(server);
  ASSERT_TRUE(snapshot.has_value());

  const ft::WorldSnapshot filtered = FilterSnapshot(server, *snapshot, *ViewOf(server, PlayerOf(session_.Client(0))));

  EXPECT_TRUE(std::ranges::any_of(
      ft::AllEntities(filtered), [&name](const ft::EntitySnapshot& e) { return e.name == name; }));
}

// The station's floors and walls run across sectors: a client holds every block that reaches into its sectors.
TEST(InterestStationTest, WalkersOnAStationStayInSyncWithoutAResync) {
  constexpr size_t kWalkers = 2;
  constexpr uint64_t kWalkTicks = 300;
  z13::testing::LoadSession session(
      kWalkers, {}, [](z13::Settings& settings) { settings.net->interest_events = true; }, z13::testing::kTestScene);
  ASSERT_TRUE(session.Connect(kConnectTicks));
  for (uint64_t tick = 0; tick < kWalkTicks; ++tick) {
    session.Tick();
  }

  for (size_t index = 0; index < session.ClientCount(); ++index) {
    const auto& digests = session.Client(index).World().get<StateDigests>();
    EXPECT_GT(digests.checked, 0u) << "client " << index;
    EXPECT_EQ(digests.resyncs, 0u) << "client " << index;
  }
}

TEST(InterestDeltasTest, MergingAPlayersInputTwiceAddsItOnce) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  const z13::gameplay::PlayerActionRecord held {.tick = 3, .player_id = kEntityPlayer, .action_id = 1, .value = 1.f};
  const z13::gameplay::PlayerActionRecord later {.tick = 90, .player_id = kEntityPlayer, .action_id = 2, .value = 1.f};
  const PlayerInput input {.held_values = {held}, .pending = {later}};
  const auto entries = [&] { return world.World().get<z13::gameplay::PlayerActionLog>().log.Entries().size(); };
  const auto queued = [&] { return world.World().get<z13::gameplay::ScheduledCommands>().records.size(); };
  const size_t logged_before = entries();

  MergePlayerInput(world.World(), input);
  MergePlayerInput(world.World(), input);

  EXPECT_EQ(entries(), logged_before + 1);
  EXPECT_EQ(queued(), 1u);
}

// An entry made in a replay found the commands after its tick in the log, not in the queue, and left them out.
TEST(InterestDeltasTest, APlayersInputBringsTheLoggedCommandsAfterTheSnapshot) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  constexpr uint64_t kSnapshotTick = 8;
  const z13::gameplay::PlayerActionRecord before {.tick = 5, .player_id = kEntityPlayer, .action_id = 1, .value = 1.f};
  const z13::gameplay::PlayerActionRecord after {.tick = 10, .player_id = kEntityPlayer, .action_id = 2, .value = 1.f};
  MergePlayerInput(world.World(), {.held_values = {before, after}});

  const PlayerInput input = InputOf(world.World(), kEntityPlayer, kSnapshotTick);

  ASSERT_EQ(input.pending.size(), 1u);
  EXPECT_EQ(input.pending.front().tick, after.tick);
  EXPECT_TRUE(std::ranges::all_of(input.held_values, [](const auto& record) { return record.tick == kSnapshotTick; }));
}

// Ghosts swap with simulated players all the time when K is small; an entry or exit decided in a replay landed in the
// client's past and its digests disagreed.
TEST(InterestStationTest, PlayersSwappingWithGhostsStayInSyncWithoutAResync) {
  constexpr size_t kWalkers = 6;
  constexpr uint64_t kWalkTicks = 300;
  constexpr double kWholeStationM = 5000.;
  z13::testing::LoadSession session(kWalkers, {}, [](z13::Settings& settings) {
    settings.net->interest_events = true;
    settings.net->ghosts = true;
    settings.net->ghost_radius_m = kWholeStationM;
    settings.net->simulated_radius_m = kWholeStationM;
    settings.net->simulated_players_max = 2;
    settings.net->simulated_players_slack = 0;
  }, z13::testing::kTestScene);
  ASSERT_TRUE(session.Connect(kConnectTicks));
  for (uint64_t tick = 0; tick < kWalkTicks; ++tick) {
    session.Tick();
  }

  for (size_t index = 0; index < session.ClientCount(); ++index) {
    EXPECT_EQ(session.Client(index).World().get<StateDigests>().resyncs, 0u) << "client " << index;
  }
}

}  // namespace
}  // namespace z13::net
