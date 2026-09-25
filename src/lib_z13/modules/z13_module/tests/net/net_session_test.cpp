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
#include <lib_core/world_snapshot_history.h>
#include <lib_core/world_state.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/input.h>
#include <z13/components/net.h>
#include <z13_module/gameplay/gameplay_entities.h>

#include <net_module/protocol.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
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

  // Same reasoning as above: wait for client_a's copy to go away, not the server's
  // (which updates synchronously, before the PlayerLeft broadcast has even been sent).
  ASSERT_TRUE(RunNetworkUntil(
      *network, {server, client_a}, kTestDeltaTime, kMaxTicks,
      [&] { return !client_a.World().lookup(PlayerEntityName(2).c_str()); }));
  EXPECT_FALSE(server.World().lookup(PlayerEntityName(2).c_str()));
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

}  // namespace
}  // namespace z13::net
