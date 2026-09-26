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

#include <cmath>
#include <cstdint>
#include <format>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/math.h>
#include <lib_core/simulation_clock.h>
#include <lib_core/world_json_store.h>
#include <lib_core/world_snapshot_history.h>
#include <lib_core/world_state.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/net.h>
#include <z13/components/player_action.h>
#include <z13_module/gameplay/gameplay_entities.h>

#include <net_module/protocol.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/world_json_test_helpers.h"
#include "../support/z13_test_world.h"

// Stage 3: handshake, id assignment, full snapshot on join, join/leave applied
// immediately. All on InMemoryTransport, deterministic, no sleep.
namespace z13::net {
namespace {

using z13::gameplay::Gameplay;
using z13::gameplay::IdCounters;
using z13::gameplay::LocalPlayer;
using z13::gameplay::Pause;
using z13::gameplay::Player;
using z13::gameplay::PlayerEntityName;
using z13::testing::RunNetworkUntil;
using z13::testing::Z13TestWorld;

constexpr float kTestDeltaTime = 1.f / 60.f;
constexpr uint64_t kMaxTicks = 200;
constexpr std::string_view kServerEndpoint = "127.0.0.1:26213";

// Server first: a client's connect attempt only ever resolves against whatever's
// already listening at the moment it's created.
Z13TestWorld MakeServer(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld(/*skip_main_menu=*/false, {"--server"}, network);
}

Z13TestWorld MakeClient(const std::shared_ptr<InMemoryNetwork>& network) {
  return Z13TestWorld(/*skip_main_menu=*/false, {"--connect", std::string(kServerEndpoint)}, network);
}

// Matches building_system.cpp's ProcessBuildBlockRequest: a block only round-trips
// through a snapshot if it's tagged StateEntity like a really-placed one is.
void SpawnBlock(flecs::world world, const std::string& name, const Eigen::Matrix4f& transform) {
  world.entity(name.c_str())
      .add<z13::flecs_tools::StateEntity>()
      .set(transform)
      .add<z13::building::BasicBlock>();
}

bool IsConnected(Z13TestWorld& world) {
  return world.World().has<Gameplay>() &&
      world.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
}

size_t BlockCount(flecs::world world) {
  size_t count = 0;
  world.query_builder<const z13::building::BasicBlock>().build().each(
      [&](const z13::building::BasicBlock&) { ++count; });
  return count;
}

std::set<uint32_t> PlayerIds(flecs::world world) {
  std::set<uint32_t> ids;
  world.query_builder<const Player>().build().each([&](const Player& player) { ids.insert(player.id); });
  return ids;
}

std::span<const uint8_t> AsUint8(std::span<const std::byte> data) {
  return {reinterpret_cast<const uint8_t*>(data.data()), data.size()};
}

// A raw transport talking directly to `server`'s NetSession, bypassing Z13TestWorld's
// own handshake -- for exercising rejection/malformed-input paths a real client never triggers.
std::unique_ptr<Transport> ConnectRawClient(InMemoryNetwork& network, Z13TestWorld& server, ConnectionId& connection) {
  auto raw = CreateInMemoryClientTransport(network, z13::kDefaultServerPort);
  for (uint64_t tick = 0; tick < kMaxTicks; ++tick) {
    server.World().progress(kTestDeltaTime);
    network.Tick();
    for (const TransportEvent& event : raw->Service()) {
      if (event.kind == TransportEventKind::kConnected) {
        connection = event.connection;
        return raw;
      }
    }
  }
  return raw;
}

template <typename T>
void SendRaw(Transport& transport, ConnectionId connection, T message) {
  Envelope envelope;
  envelope.body.Set(message);
  const std::vector<uint8_t> bytes = EncodeMessage(envelope);
  transport.Send(connection, Channel::kReliable, std::as_bytes(std::span(bytes)));
}

void SendGarbage(Transport& transport, ConnectionId connection) {
  const std::vector<std::byte> garbage(16, std::byte {0xAB});
  transport.Send(connection, Channel::kReliable, garbage);
}

TEST(NetSessionTest, ClientReceivesSnapshotAndMatchesServerCounters) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  SpawnBlock(server.World(), "Block_1", Eigen::Matrix4f::Identity());

  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(client); }));

  EXPECT_EQ(BlockCount(client.World()), 1u);
  EXPECT_EQ(client.World().get<IdCounters>().last_player_id, server.World().get<IdCounters>().last_player_id);
  // Not exact equality: the snapshot was captured a tick or two before the client
  // applied it, and the server's own tick kept advancing in between.
  const uint64_t client_tick = client.World().get<z13::flecs_tools::SimulationClock>().tick;
  const uint64_t server_tick = server.World().get<z13::flecs_tools::SimulationClock>().tick;
  EXPECT_GT(client_tick, 0u);
  EXPECT_LE(client_tick, server_tick);
}

// Welcome carries a snapshot cached before the joiner was spawned, so its own player can
// only arrive as a PlayerJoined delta. Joining inside the first interval hides this: the
// server still has no cache and falls back to a fresh capture that already has the player.
TEST(NetSessionTest, JoinerGetsItsOwnPlayerFromACachedSnapshot) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);

  const uint64_t interval_ticks = static_cast<uint64_t>(
      std::llround(server.Config().GetSnapshotIntervalSeconds() * server.Config().GetFPS()));
  for (uint64_t i = 0; i <= interval_ticks; ++i) {
    server.World().progress(kTestDeltaTime);
  }
  ASSERT_FALSE(server.World().get<z13::flecs_tools::WorldSnapshotHistory>().history.Empty());

  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(client); }));

  const std::optional<uint32_t> local_id = client.World().get<z13::gameplay::LocalPlayer>().id;
  ASSERT_TRUE(local_id.has_value());
  EXPECT_TRUE(client.World().lookup(PlayerEntityName(*local_id).c_str()));
  EXPECT_EQ(PlayerIds(client.World()), PlayerIds(server.World()));
  EXPECT_TRUE(client.Player());
}

// The cached snapshot SendWelcome reuses can be stale by up to one snapshot interval,
// so the host's own recent movement must reach the joiner through the replay instead.
TEST(NetSessionTest, JoinReplaysHostMovementSinceTheCachedSnapshot) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);

  // Let WorldSnapshotHistory capture at least one entry before the host moves, so
  // Welcome's snapshot is guaranteed to predate the movement below.
  const uint64_t interval_ticks = static_cast<uint64_t>(
      std::llround(server.Config().GetSnapshotIntervalSeconds() * server.Config().GetFPS()));
  for (uint64_t i = 0; i < interval_ticks; ++i) {
    server.World().progress(kTestDeltaTime);
  }

  // Move the host, then let a client join immediately -- well before the next periodic
  // capture, so Welcome must reuse the older cached snapshot and replay this movement.
  server.EmitInput(z13::testing::KeyDown(z13::fbs::input::Keycode::KEY_W));
  for (uint64_t i = 0; i < 10; ++i) {
    server.World().progress(kTestDeltaTime);
  }
  server.EmitInput(z13::testing::KeyUp(z13::fbs::input::Keycode::KEY_W));
  server.World().progress(kTestDeltaTime);

  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(client); }));

  const flecs::entity host_on_server = server.World().lookup(PlayerEntityName(0).c_str());
  const flecs::entity host_on_client = client.World().lookup(PlayerEntityName(0).c_str());
  ASSERT_TRUE(host_on_server);
  ASSERT_TRUE(host_on_client);

  const Eigen::Vector3f host_position = z13::math::ExtractTranslation<float>(host_on_server.get<Eigen::Matrix4f>());
  ASSERT_GT(host_position.norm(), 0.01f);  // actually moved, or this test proves nothing
  const Eigen::Vector3f replayed_position =
      z13::math::ExtractTranslation<float>(host_on_client.get<Eigen::Matrix4f>());
  EXPECT_LT((replayed_position - host_position).norm(), z13::testing::kTestEpsilon);
}

TEST(NetSessionTest, ThreeClientsGetSequentialIdsServerIsZero) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);
  Z13TestWorld client_b = MakeClient(network);
  Z13TestWorld client_c = MakeClient(network);

  const std::set<uint32_t> expected {0, 1, 2, 3};
  // Also wait for client_a's own copy of b/c's PlayerJoined broadcasts: kConnected only
  // means its own Welcome resolved, not that other clients' joins have arrived yet.
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client_a, client_b, client_c}, kTestDeltaTime, kMaxTicks, [&] {
        return IsConnected(client_a) && IsConnected(client_b) && IsConnected(client_c) &&
            PlayerIds(client_a.World()) == expected;
      }));

  EXPECT_EQ(client_a.World().get<LocalPlayer>().id, 1u);
  EXPECT_EQ(client_b.World().get<LocalPlayer>().id, 2u);
  EXPECT_EQ(client_c.World().get<LocalPlayer>().id, 3u);
  EXPECT_EQ(PlayerIds(server.World()), expected);
}

TEST(NetSessionTest, ClientHasExactlyOneLocalPlayerOthersHaveNoInputListener) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(client); }));

  size_t local_count = 0;
  client.World().query_builder().with<z13::input::CurrentActionListenerTag>().build().each(
      [&](flecs::entity) { ++local_count; });
  EXPECT_EQ(local_count, 1u);

  ASSERT_TRUE(client.Player());
  EXPECT_EQ(client.Player().get<Player>().id, client.World().get<LocalPlayer>().id);

  const flecs::entity remote = client.World().lookup(PlayerEntityName(0).c_str());
  ASSERT_TRUE(remote);
  EXPECT_FALSE(remote.has<z13::input::InputListener>());
  EXPECT_FALSE(remote.has<z13::input::CurrentActionListenerTag>());
}

TEST(NetSessionTest, PlayerJoinedReachesAlreadyConnectedClients) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client_a}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(client_a); }));

  Z13TestWorld client_b = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client_a, client_b}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(client_b); }));

  EXPECT_TRUE(client_a.World().lookup(PlayerEntityName(2).c_str()));
}

TEST(NetSessionTest, PlayerLeftRemovesEntityOnServerAndOtherClients) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client_a}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(client_a); }));

  // A raw client stands in for the leaving peer so the test can call Disconnect()
  // directly; InMemoryTransport only notifies the peer on an explicit Disconnect(), not
  // a timed-out connection.
  ConnectionId leaver_connection = kInvalidConnectionId;
  const auto leaver = ConnectRawClient(*network, server, leaver_connection);
  ASSERT_NE(leaver_connection, kInvalidConnectionId);
  SendRaw(*leaver, leaver_connection, fbs::net::ClientHelloT {.version = kProtocolVersion});

  // Wait for client_a's copy specifically: the server spawns Player_2 synchronously,
  // but the PlayerJoined broadcast still needs a network tick to reach client_a.
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a}, kTestDeltaTime, kMaxTicks, [&] {
    return static_cast<bool>(client_a.World().lookup(PlayerEntityName(2).c_str()));
  }));

  leaver->Disconnect(leaver_connection);

  // Same reasoning as above: wait for client_a's copy to go away. The server schedules
  // its own removal exactly like it does for everyone else (LeaveApplyTick), so by the
  // time client_a's copy has caught up, the server's is long since gone too.
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client_a}, kTestDeltaTime, kMaxTicks,
      [&] { return !client_a.World().lookup(PlayerEntityName(2).c_str()); }));
  EXPECT_FALSE(server.World().lookup(PlayerEntityName(2).c_str()));
}

// A disconnect doesn't remove the entity outright: LeaveApplyTick holds it until
// whatever that player already had scheduled has had a chance to apply everywhere.
TEST(NetSessionTest, PlayerLeftWaitsForAlreadyScheduledCommandsToApply) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);

  // The only connection so far, so it gets id 1 (server is 0) -- not the id 2 a second
  // client would get, as in the test above.
  ConnectionId leaver_connection = kInvalidConnectionId;
  const auto leaver = ConnectRawClient(*network, server, leaver_connection);
  ASSERT_NE(leaver_connection, kInvalidConnectionId);
  SendRaw(*leaver, leaver_connection, fbs::net::ClientHelloT {.version = kProtocolVersion});
  ASSERT_TRUE(RunNetworkUntil(*network, {server}, kTestDeltaTime, kMaxTicks, [&] {
    return static_cast<bool>(server.World().lookup(PlayerEntityName(1).c_str()));
  }));

  // Stands in for a command this player sent moments before dropping -- already
  // scheduled, not yet due.
  const uint64_t future_tick = server.World().get<z13::flecs_tools::SimulationClock>().tick + 5;
  server.World().get_mut<z13::gameplay::ScheduledCommands>().records.push_back(
      {.tick = future_tick, .player_id = 1, .action_id = 0, .value = 1.f});

  leaver->Disconnect(leaver_connection);
  RunNetworkUntil(*network, {server}, kTestDeltaTime, 2, [] { return false; });  // let the disconnect land

  const uint64_t now = server.World().get<z13::flecs_tools::SimulationClock>().tick;
  ASSERT_LT(now, future_tick);
  EXPECT_TRUE(server.World().lookup(PlayerEntityName(1).c_str()))
      << "removed before its already-scheduled command could apply";

  RunNetworkUntil(*network, {server}, kTestDeltaTime, future_tick - now + 1, [] { return false; });
  EXPECT_FALSE(server.World().lookup(PlayerEntityName(1).c_str()));
}

TEST(NetSessionTest, ConnectViaBootstrapStaysAtMenuUntilConnectedThenClearsPause) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client = MakeClient(network);

  EXPECT_TRUE(client.World().has<Pause>());
  EXPECT_FALSE(client.World().has<Gameplay>());

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(client); }));

  EXPECT_FALSE(client.World().has<Pause>());
  EXPECT_TRUE(client.World().has<Gameplay>());
}

TEST(NetSessionTest, RejectedOnProtocolVersionMismatch) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);

  ConnectionId connection = kInvalidConnectionId;
  const auto raw = ConnectRawClient(*network, server, connection);
  ASSERT_NE(connection, kInvalidConnectionId);

  fbs::net::ClientHelloT hello;
  hello.version = kProtocolVersion + 1;
  SendRaw(*raw, connection, hello);

  std::optional<std::string> reason;
  for (uint64_t tick = 0; tick < kMaxTicks && !reason; ++tick) {
    server.World().progress(kTestDeltaTime);
    network->Tick();
    for (const TransportEvent& event : raw->Service()) {
      if (event.kind != TransportEventKind::kReceived) {
        continue;
      }
      const auto decoded = DecodeMessage(AsUint8(event.data));
      ASSERT_TRUE(decoded.has_value());
      if (const auto* rejected = AsBody<fbs::net::RejectedT>(decoded->body)) {
        reason = rejected->reason;
      }
    }
  }

  ASSERT_TRUE(reason.has_value());
  EXPECT_FALSE(reason->empty());
  EXPECT_EQ(PlayerIds(server.World()), std::set<uint32_t> {0});  // no player spawned for the rejected connection
}

TEST(NetSessionTest, CorruptSnapshotFailsTheClientWithoutCreatingAScene) {
  auto network = std::make_shared<InMemoryNetwork>();

  // A bare listener stands in for a server sending a well-formed but corrupt Welcome,
  // purely to exercise the client's decode-failure path.
  auto listener = CreateInMemoryServerTransport(*network, z13::kDefaultServerPort);
  ASSERT_TRUE(listener.has_value());

  Z13TestWorld client = MakeClient(network);

  std::optional<ConnectionId> connection;
  for (uint64_t tick = 0; tick < kMaxTicks && !connection; ++tick) {
    client.World().progress(kTestDeltaTime);
    network->Tick();
    for (const TransportEvent& event : (*listener)->Service()) {
      if (event.kind == TransportEventKind::kConnected) {
        connection = event.connection;
      }
    }
  }
  ASSERT_TRUE(connection.has_value());

  fbs::net::WelcomeT welcome;
  welcome.player_id = 1;
  welcome.snapshot = {1, 2, 3, 4};  // not valid msgpack for WorldSnapshot
  SendRaw(**listener, *connection, welcome);

  ASSERT_TRUE(RunNetworkUntil(*network, {client}, kTestDeltaTime, kMaxTicks, [&] {
    return client.World().get<ConnectionStatus>().state == ConnectionState::kFailed;
  }));

  EXPECT_FALSE(client.World().has<Gameplay>());
  EXPECT_TRUE(client.World().has<Pause>());
}

TEST(NetSessionTest, GarbagePacketFromClientDropsThatPeerButServerStaysAlive) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);

  ConnectionId garbage_connection = kInvalidConnectionId;
  const auto raw = ConnectRawClient(*network, server, garbage_connection);
  ASSERT_NE(garbage_connection, kInvalidConnectionId);
  SendGarbage(*raw, garbage_connection);

  Z13TestWorld good_client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, good_client}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(good_client); }));

  EXPECT_EQ(good_client.World().get<LocalPlayer>().id, 1u);  // garbage sender never got an id
}

// Any id an ActionMap has never uses -- there are nowhere near 256 distinct actions.
constexpr uint8_t kUnknownActionId = 255;

uint8_t AnyKnownActionId(flecs::world world) {
  const auto& by_id = world.get<z13::input::ActionMap>().action_map.get<z13::input::ActionMap::IdTag>();
  return static_cast<uint8_t>(by_id.begin()->id);
}

TEST(NetSessionTest, UnknownActionIdIsDroppedButOtherCommandsInTheBatchAreKept) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);

  ConnectionId connection = kInvalidConnectionId;
  const auto raw = ConnectRawClient(*network, server, connection);
  ASSERT_NE(connection, kInvalidConnectionId);
  SendRaw(*raw, connection, fbs::net::ClientHelloT {.version = kProtocolVersion});
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server}, kTestDeltaTime, kMaxTicks, [&] { return static_cast<bool>(server.World().lookup(PlayerEntityName(1).c_str())); }));

  const uint8_t valid_action_id = AnyKnownActionId(server.World());
  fbs::net::CommandBatchT batch;
  batch.base_tick = server.World().get<z13::flecs_tools::SimulationClock>().tick + 1;
  batch.commands.emplace_back(0, kUnknownActionId, 100);
  batch.commands.emplace_back(0, valid_action_id, 100);
  SendRaw(*raw, connection, batch);

  ASSERT_TRUE(RunNetworkUntil(*network, {server}, kTestDeltaTime, kMaxTicks, [&] {
    return !server.World().get<z13::gameplay::ScheduledCommands>().records.empty();
  }));
  const auto& records = server.World().get<z13::gameplay::ScheduledCommands>().records;
  ASSERT_EQ(records.size(), 1u);
  EXPECT_EQ(records.front().action_id, valid_action_id);
}

TEST(NetSessionTest, CommandsPastTheRateLimitAreDroppedForThatConnection) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);

  ConnectionId connection = kInvalidConnectionId;
  const auto raw = ConnectRawClient(*network, server, connection);
  ASSERT_NE(connection, kInvalidConnectionId);
  SendRaw(*raw, connection, fbs::net::ClientHelloT {.version = kProtocolVersion});
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server}, kTestDeltaTime, kMaxTicks, [&] { return static_cast<bool>(server.World().lookup(PlayerEntityName(1).c_str())); }));

  const uint8_t valid_action_id = AnyKnownActionId(server.World());
  fbs::net::CommandBatchT batch;
  batch.base_tick = server.World().get<z13::flecs_tools::SimulationClock>().tick + 1;
  for (uint32_t i = 0; i < z13::net::kMaxCommandsPerRateLimitWindow + 20; ++i) {
    batch.commands.emplace_back(0, valid_action_id, 100);
  }
  SendRaw(*raw, connection, batch);

  ASSERT_TRUE(RunNetworkUntil(*network, {server}, kTestDeltaTime, kMaxTicks, [&] {
    return !server.World().get<z13::gameplay::ScheduledCommands>().records.empty();
  }));
  EXPECT_EQ(
      server.World().get<z13::gameplay::ScheduledCommands>().records.size(), z13::net::kMaxCommandsPerRateLimitWindow);
}

TEST(NetSessionTest, LargeSnapshotArrivesWhole) {
  constexpr int kBlockCount = 5000;
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  for (int i = 0; i < kBlockCount; ++i) {
    Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
    transform(0, 3) = static_cast<float>(i);
    SpawnBlock(server.World(), std::format("Block_{}", i), transform);
  }

  Z13TestWorld client = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(client); }));

  EXPECT_EQ(BlockCount(client.World()), static_cast<size_t>(kBlockCount));
}

// IdCounters.last_player_id is server-only bookkeeping (docs/client-server-plan.md,
// "Клиент не трогает IdCounters при join"): a client never learns of some other
// connection's join/leave bumping it beyond its own, so it can legitimately differ from
// the server's value even once everything else matches.
std::string WithoutLastPlayerId(std::string json) {
  constexpr std::string_view kKey = "\"last_player_id\": ";
  const auto key_pos = json.find(kKey);
  if (key_pos == std::string::npos) {
    return json;
  }
  const auto digits_start = key_pos + kKey.size();
  const auto digits_end = json.find(',', digits_start);
  json.replace(digits_start, digits_end - digits_start, "0");
  return json;
}

std::string Checkpoint(Z13TestWorld& test_world) {
  const auto json = z13::flecs_tools::WorldJsonStore::Save(test_world.World());
  EXPECT_TRUE(json.has_value()) << (json ? "" : json.error());
  return WithoutLastPlayerId(z13::testing::WithNormalizedSimulationTick(json.value_or("")));
}

// Every participant applies the same commands on the same ticks in the same order
// (docs/client-server-plan.md, "Модель синхронизации"), so a whole scripted session --
// movement, a look, building, destroying, a late join, and an unrelated third player
// joining and leaving mid-session -- has to converge to bit-identical state on every
// participant still around to check it.
TEST(NetSessionTest, ScriptedSessionConvergesToIdenticalStateEverywhere) {
  auto network = std::make_shared<InMemoryNetwork>();
  Z13TestWorld server = MakeServer(network);
  Z13TestWorld client_a = MakeClient(network);

  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client_a}, kTestDeltaTime, kMaxTicks, [&] { return IsConnected(client_a); }));

  // Long enough for a command to clear kInputDelayTicks, NetActionSender's own batching
  // interval and a network round trip, so each stage below has visibly landed everywhere
  // before the next begins.
  constexpr uint64_t kSettleTicks = 30;
  auto tick_all = [&](std::vector<std::reference_wrapper<Z13TestWorld>> worlds, uint64_t count) {
    RunNetworkUntil(*network, worlds, kTestDeltaTime, count, [] { return false; });
  };

  using z13::testing::KeyDown;
  using z13::testing::KeyUp;
  using z13::testing::MouseDown;
  using z13::testing::MouseUp;
  using Keycode = z13::fbs::input::Keycode;

  // client_a: move forward, look, build a block, destroy it again.
  client_a.EmitInput(KeyDown(Keycode::KEY_W));
  tick_all({server, client_a}, 10);
  client_a.EmitInput(KeyUp(Keycode::KEY_W));
  tick_all({server, client_a}, kSettleTicks);

  z13::input::MouseMoveEvent look;
  look.delta = {.x = 50, .y = 0};
  client_a.EmitInput(look);
  tick_all({server, client_a}, kSettleTicks);

  client_a.EmitInput(KeyDown(Keycode::KEY_TAB));
  tick_all({server, client_a}, 1);
  client_a.EmitInput(KeyUp(Keycode::KEY_TAB));
  tick_all({server, client_a}, kSettleTicks);
  ASSERT_TRUE(client_a.Player().has<z13::building::BuildingTool>())
      << "TAB never toggled BuildingTool on for client_a's own local copy";

  client_a.EmitInput(MouseDown(Keycode::MOUSE_BUTTON_LEFT));
  tick_all({server, client_a}, 1);
  client_a.EmitInput(MouseUp(Keycode::MOUSE_BUTTON_LEFT));
  tick_all({server, client_a}, kSettleTicks);
  ASSERT_EQ(BlockCount(server.World()), 1u) << "client_a's block never landed on the server";

  client_a.EmitInput(MouseDown(Keycode::MOUSE_BUTTON_RIGHT));
  tick_all({server, client_a}, 1);
  client_a.EmitInput(MouseUp(Keycode::MOUSE_BUTTON_RIGHT));
  tick_all({server, client_a}, kSettleTicks);
  ASSERT_EQ(BlockCount(server.World()), 0u) << "client_a's own block survived its destroy";

  EXPECT_EQ(Checkpoint(server), Checkpoint(client_a)) << "after move/look/build/destroy";

  // client_b joins mid-session, well after client_a's own actions above.
  Z13TestWorld client_b = MakeClient(network);
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kTestDeltaTime, kMaxTicks, [&] {
    return IsConnected(client_b);
  }));
  tick_all({server, client_a, client_b}, kSettleTicks);

  EXPECT_EQ(Checkpoint(server), Checkpoint(client_a)) << "right after client_b's join";
  EXPECT_EQ(Checkpoint(server), Checkpoint(client_b)) << "right after client_b's join";

  // An unrelated third player joins and leaves mid-session -- its id churn and the
  // resulting PlayerJoined/PlayerLeft scheduling must not disturb the other two.
  ConnectionId third_connection = kInvalidConnectionId;
  const auto third = ConnectRawClient(*network, server, third_connection);
  ASSERT_NE(third_connection, kInvalidConnectionId);
  SendRaw(*third, third_connection, fbs::net::ClientHelloT {.version = kProtocolVersion});
  ASSERT_TRUE(RunNetworkUntil(*network, {server, client_a, client_b}, kTestDeltaTime, kMaxTicks, [&] {
    return static_cast<bool>(server.World().lookup(PlayerEntityName(3).c_str()));
  }));
  third->Disconnect(third_connection);
  tick_all({server, client_a, client_b}, kSettleTicks);
  ASSERT_FALSE(server.World().lookup(PlayerEntityName(3).c_str()));

  // client_b builds its own block.
  client_b.EmitInput(KeyDown(Keycode::KEY_TAB));
  tick_all({server, client_a, client_b}, 1);
  client_b.EmitInput(KeyUp(Keycode::KEY_TAB));
  tick_all({server, client_a, client_b}, kSettleTicks);

  client_b.EmitInput(MouseDown(Keycode::MOUSE_BUTTON_LEFT));
  tick_all({server, client_a, client_b}, 1);
  client_b.EmitInput(MouseUp(Keycode::MOUSE_BUTTON_LEFT));
  tick_all({server, client_a, client_b}, kSettleTicks);
  ASSERT_EQ(BlockCount(server.World()), 1u) << "client_b's block never landed on the server";

  EXPECT_EQ(Checkpoint(server), Checkpoint(client_a)) << "after client_b's own build";
  EXPECT_EQ(Checkpoint(server), Checkpoint(client_b)) << "after client_b's own build";
}

}  // namespace
}  // namespace z13::net
