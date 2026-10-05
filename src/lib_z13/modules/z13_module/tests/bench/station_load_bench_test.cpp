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

// Load of the test station (docs/station-layout) on everything but rendering, with today's
// BasicBlock standing in for every block. Built by `make.py --bench`, run with
// `z13.py --bench --filter 'StationLoadBench.*'`.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <Eigen/Dense>
#include <flatbuffers/flatbuffers.h>
#include <world_snapshot_generated.h>

#include <lib_core/state/rollback.h>
#include <lib_core/state/world_serializer.h>
#include <lib_core/state/world_snapshot_history.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/math.h>

#include <net_module/in_memory_transport.h>
#include <net_module/state_digest.h>

#include <z13/components/building.h>
#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13_settings/net_tuning.h>

#include "../support/building_test_helpers.h"
#include "../support/test_network.h"
#include "../support/z13_test_world.h"

namespace z13::testing {
namespace {

namespace ft = z13::flecs_tools;
using Keycode = z13::fbs::input::Keycode;

const std::filesystem::path kStationLayoutDir {Z13_STATION_LAYOUT_DIR};
const std::filesystem::path kRunsFile = kStationLayoutDir / "station-blocks-runs.txt";
const std::filesystem::path kPanelsFile = kStationLayoutDir / "station-blocks-panels.txt";

constexpr float kCellSize = 0.25f;
// Where the station sits relative to the spawned player: its first deck's spine corridor.
const Eigen::Vector3f kSpineCentre {48.5f, 24.5f, 0.f};
constexpr float kFloorBelowPlayer = 0.5f;
// Copies for the x2/x4 scales stand side by side with a gap.
constexpr float kCopyPitch = 110.f;
constexpr int kScales[] = {1, 2, 4};
constexpr int kTimedRuns = 5;
constexpr int kWalkTicks = 60;
constexpr uint64_t kRollbackDepths[] = {10, 60, 240};
constexpr uint64_t kWarmUpTicks = 250;  // more history than the deepest rollback
constexpr int kMaxCatchUpCalls = 1000;
constexpr double kBytesPerMb = 1024.0 * 1024.0;
constexpr size_t kTopSystems = 8;
constexpr double kMsPerSecond = 1000.0;

struct Box {
  Eigen::Vector3i min = Eigen::Vector3i::Zero();
  Eigen::Vector3i size = Eigen::Vector3i::Zero();
};

std::expected<std::vector<Box>, std::string> ReadBlocks(const std::filesystem::path& path) {
  std::ifstream file(path);
  if (!file) {
    return std::unexpected(std::format("can't open {} (run docs/station-layout/generate_layout.py)", path.string()));
  }
  std::vector<Box> boxes;
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line.front() == '#') {
      continue;
    }
    std::istringstream fields(line);
    std::string kind;
    Box box;
    if (!(fields >> kind >> box.min.x() >> box.min.y() >> box.min.z() >> box.size.x() >> box.size.y() >> box.size.z())) {
      return std::unexpected(std::format("malformed line in {}: {}", path.string(), line));
    }
    boxes.push_back(box);
  }
  return boxes;
}

template <typename F>
double Ms(F&& work) {
  const auto start = std::chrono::steady_clock::now();
  work();
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

double Median(std::vector<double> values) {
  std::ranges::sort(values);
  return values.empty() ? 0.0 : values[values.size() / 2];
}

Eigen::Vector3f PlayerPosition(Z13TestWorld& world) {
  return z13::math::ExtractTranslation<float>(world.Player().get<Eigen::Matrix4f>());
}

// The station placed around the player, `copies` times along +x, as BasicBlocks named like
// SpawnBlock's so IdCounters stays in step.
void PlaceStation(Z13TestWorld& world, const std::vector<Box>& boxes, int copies) {
  flecs::world w = world.World();
  const Eigen::Vector3f origin = PlayerPosition(world) - kSpineCentre - Eigen::Vector3f(0.f, 0.f, kFloorBelowPlayer);
  auto& counters = w.get_mut<z13::gameplay::IdCounters>();
  for (int copy = 0; copy < copies; ++copy) {
    const Eigen::Vector3f copy_origin = origin + Eigen::Vector3f(kCopyPitch * static_cast<float>(copy), 0.f, 0.f);
    for (const Box& box : boxes) {
      const Eigen::Vector3f centre =
          (box.min.cast<float>() + box.size.cast<float>() * 0.5f) * kCellSize + copy_origin;
      Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
      z13::math::SetTranslation(centre, transform);
      w.entity(std::format("Block_{}", ++counters.last_block_id).c_str())
          .add<ft::StateEntity>()
          .set(transform)
          .add<z13::building::BasicBlock>();
    }
  }
}

size_t SnapshotBytes(const ft::WorldSnapshot& snapshot) {
  const z13::fbs::state::WorldSnapshotT flat = ft::ToFlatbuffer(snapshot);
  flatbuffers::FlatBufferBuilder builder;
  builder.Finish(z13::fbs::state::WorldSnapshot::Pack(builder, &flat));
  return builder.GetSize();
}

struct Row {
  std::string label;
  size_t blocks {};
  double place_ms {};
  size_t snapshot_bytes {};
  double capture_ms {};
  double restore_ms {};
  double tick_median_ms {};
  double tick_max_ms {};
  double history_mb {};
  double digest_ms {};
  std::vector<double> rollback_ms;
  double build_tick_ms {};
  std::vector<std::pair<std::string, double>> top_systems_ms;  // per tick
};

std::vector<std::pair<std::string, double>> SystemTimes(flecs::world world) {
  std::vector<std::pair<std::string, double>> times;
  world.query_builder().with(flecs::System).build().each([&](flecs::entity system) {
    if (const ecs_system_t* data = ecs_system_get(world, system)) {
      times.emplace_back(std::string(system.path()), static_cast<double>(data->time_spent) * kMsPerSecond);
    }
  });
  return times;
}

// Where the walk's ticks went, system by system, heaviest first.
std::vector<std::pair<std::string, double>> TopSystems(
    const std::vector<std::pair<std::string, double>>& before, const std::vector<std::pair<std::string, double>>& after,
    int ticks) {
  std::vector<std::pair<std::string, double>> spent;
  for (const auto& [name, ms] : after) {
    const auto found = std::ranges::find(before, name, &std::pair<std::string, double>::first);
    spent.emplace_back(name, (ms - (found != before.end() ? found->second : 0.0)) / ticks);
  }
  std::ranges::sort(spent, std::greater<>(), &std::pair<std::string, double>::second);
  spent.resize(std::min(spent.size(), kTopSystems));
  return spent;
}

Row Measure(const std::string& label, const std::vector<Box>& boxes, int copies) {
  Row row {.label = label};
  Z13TestWorld world;
  world.Tick();  // bootstrap starts the game and spawns the player
  flecs::world w = world.World();
  // The network roles' capture rate: what a session pays, not single player's.
  w.set<ft::SnapshotCaptureRate>({.per_interval = z13::NetTuning {}.rollback_snapshots_per_interval});

  row.place_ms = Ms([&] {
    PlaceStation(world, boxes, copies);
    world.Tick();  // bodies are created on the first frame that sees the blocks
  });
  row.blocks = static_cast<size_t>(w.count<z13::building::BasicBlock>());

  std::vector<double> capture;
  std::vector<double> restore;
  for (int run = 0; run < kTimedRuns; ++run) {
    std::expected<ft::WorldSnapshot, std::string> snapshot;
    capture.push_back(Ms([&] { snapshot = ft::CaptureState(w); }));
    EXPECT_TRUE(snapshot.has_value()) << (snapshot ? "" : snapshot.error());
    if (!snapshot) {
      return row;
    }
    row.snapshot_bytes = SnapshotBytes(*snapshot);
    restore.push_back(Ms([&] { EXPECT_TRUE(ft::RestoreWorld(w, *snapshot).has_value()); }));
  }
  row.capture_ms = Median(capture);
  row.restore_ms = Median(restore);

  for (uint64_t tick = 0; tick < kWarmUpTicks; ++tick) {
    world.Tick();
  }
  ecs_measure_system_time(w, true);
  const auto systems_before = SystemTimes(w);
  world.EmitInput(KeyDown(Keycode::KEY_W));
  std::vector<double> ticks;
  for (int tick = 0; tick < kWalkTicks; ++tick) {
    ticks.push_back(Ms([&] { world.Tick(); }));
  }
  world.EmitInput(KeyUp(Keycode::KEY_W));
  row.top_systems_ms = TopSystems(systems_before, SystemTimes(w), kWalkTicks);
  ecs_measure_system_time(w, false);
  row.tick_median_ms = Median(ticks);
  row.tick_max_ms = std::ranges::max(ticks);
  row.history_mb = static_cast<double>(w.get<ft::WorldSnapshotHistory>().history.Entries().size() * row.snapshot_bytes) /
                   kBytesPerMb;

  std::vector<double> digest;
  for (int run = 0; run < kTimedRuns; ++run) {
    digest.push_back(Ms([&] { z13::net::ComputeStateDigest(w, w.get<ft::SimulationClock>().tick); }));
  }
  row.digest_ms = Median(digest);

  for (const uint64_t depth : kRollbackDepths) {
    const uint64_t now = w.get<ft::SimulationClock>().tick;
    ft::RequestRollback(w, now - depth, now);
    row.rollback_ms.push_back(Ms([&] {
      int calls = 0;
      do {
        world.Tick();
      } while (ft::IsCatchingUp(w) && ++calls < kMaxCatchUpCalls);
    }));
    EXPECT_FALSE(w.has<ft::RollbackFailed>()) << label << ": no snapshot " << depth << " ticks old";
    w.remove<ft::RollbackFailed>();
  }

  PlaceStation(world, {Box {.size = Eigen::Vector3i::Ones()}}, /*copies=*/1);
  row.build_tick_ms = Ms([&] { world.Tick(); });
  return row;
}

void Print(const std::string& title, const std::vector<Row>& rows) {
  std::cout << std::format("[ bench ] {}\n", title);
  std::cout << "[ bench ] scale  blocks  place ms  snapshot KB  capture ms  restore ms  tick med/max ms  "
               "history MB  digest ms  rollback 10/60/240 ms  +1 block tick ms\n";
  for (const Row& row : rows) {
    std::string rollback;
    for (const double ms : row.rollback_ms) {
      rollback += std::format("{}{:.0f}", rollback.empty() ? "" : "/", ms);
    }
    std::cout << std::format(
        "[ bench ] {:<5}  {:>6}  {:>8.0f}  {:>11.0f}  {:>10.1f}  {:>10.1f}  {:>7.1f}/{:<7.1f}  {:>10.1f}  {:>9.1f}  "
        "{:>21}  {:>16.1f}\n",
        row.label, row.blocks, row.place_ms, static_cast<double>(row.snapshot_bytes) / 1024.0, row.capture_ms,
        row.restore_ms, row.tick_median_ms, row.tick_max_ms, row.history_mb, row.digest_ms, rollback,
        row.build_tick_ms);
  }
  for (const Row& row : rows) {
    std::cout << std::format("[ bench ] {} heaviest systems, ms per tick:", row.label);
    for (const auto& [name, ms] : row.top_systems_ms) {
      std::cout << std::format(" {} {:.2f};", name, ms);
    }
    std::cout << "\n";
  }
  std::cout << std::flush;
}

void RunScales(const std::filesystem::path& file, const std::string& title) {
  const auto boxes = ReadBlocks(file);
  ASSERT_TRUE(boxes.has_value()) << boxes.error();
  std::vector<Row> rows;
  for (const int scale : kScales) {
    rows.push_back(Measure(std::format("x{}", scale), *boxes, scale));
  }
  Print(title, rows);
}

TEST(StationLoadBench, DISABLED_Runs) {
  RunScales(kRunsFile, "station, one block per straight run");
}

TEST(StationLoadBench, DISABLED_Panels) {
  RunScales(kPanelsFile, "station, panels up to 4 m");
}

// The server already holds the station when a client connects; Welcome carries all of it.
TEST(StationLoadBench, DISABLED_Join) {
  for (const auto& [file, title] : {std::pair {kRunsFile, "runs"}, std::pair {kPanelsFile, "panels"}}) {
    const auto boxes = ReadBlocks(file);
    ASSERT_TRUE(boxes.has_value()) << boxes.error();
    auto network = std::make_shared<z13::net::InMemoryNetwork>();
    Z13TestWorld server(/*skip_main_menu=*/false, {std::string(kServerArg)}, network);
    server.Tick();
    PlaceStation(server, *boxes, 1);
    server.Tick();
    const auto snapshot = ft::CaptureState(server.World());
    ASSERT_TRUE(snapshot.has_value()) << snapshot.error();

    Z13TestWorld client(/*skip_main_menu=*/false, {std::string(kConnectArg), std::string(kTestServerEndpoint)}, network);
    uint64_t ticks = 0;
    const auto connected = [&] {
      return client.World().has<z13::gameplay::Gameplay>() &&
             client.World().get<z13::net::ConnectionStatus>().state == z13::net::ConnectionState::kConnected;
    };
    const double join_ms = Ms([&] {
      while (!connected() && ticks < kMaxNetTestTicks) {
        RunNetworkUntil(*network, {server, client}, kNetTestDeltaTime, 1, [] { return false; });
        ++ticks;
      }
    });
    EXPECT_TRUE(connected()) << title;
    std::cout << std::format(
        "[ bench ] join ({}): {} blocks, Welcome snapshot ~{:.0f} KB, joined in {} ticks, {:.0f} ms of server+client "
        "frames\n",
        title, server.World().count<z13::building::BasicBlock>(),
        static_cast<double>(SnapshotBytes(*snapshot)) / 1024.0, ticks, join_ms)
              << std::flush;
  }
}

}  // namespace
}  // namespace z13::testing
