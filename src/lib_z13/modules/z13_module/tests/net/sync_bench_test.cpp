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

// A bench, not a test: how smoothly and faithfully client B replays client A's movement
// and look, per network profile and tuning variant. Disabled; run it with
//   z13_test_runner --gtest_also_run_disabled_tests --gtest_filter='SyncBench.*'
// It prints a table per scenario and writes a summary and per-tick CSVs to
// $Z13_SYNC_BENCH_DIR/<scenario> (default: <temp>/z13_sync_bench).

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/utils/math.h>

#include <net_module/in_memory_transport.h>

#include <z13/components/gameplay.h>
#include <z13/components/net.h>
#include <z13_module/gameplay/camera_look.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/settings.h>

#include "../support/building_test_helpers.h"
#include "../support/motion_metrics.h"
#include "../support/test_network.h"
#include "../support/z13_test_world.h"

namespace z13::net {
namespace {

using z13::fbs::input::Keycode;
using z13::gameplay::LookAngles;
using z13::testing::kConnectArg;
using z13::testing::kMaxNetTestTicks;
using z13::testing::kNetTestDeltaTime;
using z13::testing::kServerArg;
using z13::testing::kTestServerEndpoint;
using z13::testing::KeyDown;
using z13::testing::KeyUp;
using z13::testing::RunNetworkUntil;
using z13::testing::TrackingMetrics;
using z13::testing::Z13TestWorld;

constexpr std::string_view kOutputDirEnv = "Z13_SYNC_BENCH_DIR";
constexpr std::string_view kDefaultOutputDirName = "z13_sync_bench";
constexpr std::string_view kSummaryFileName = "summary.csv";
// Lets the session settle (spawn, clock sync) before A starts moving.
constexpr uint64_t kSettleTicks = 60;
// B gets this long after A stops to converge.
constexpr int kTailTicks = 90;
constexpr int kMaxLagTicks = 60;
constexpr int kYawDelta = 8;
constexpr int kPitchDelta = 6;

// A stretch of A's input: keys held throughout, plus a mouse move every tick.
struct Segment {
  int ticks {};
  std::set<Keycode> keys;
  Eigen::Vector2i look_delta = Eigen::Vector2i::Zero();
};

struct Scenario {
  std::string_view name;
  std::vector<Segment> segments;
};

struct NetProfile {
  std::string_view name;
  FaultConfig fault;
};

struct TuningVariant {
  std::string_view name;
  std::function<void(z13::Settings&)> apply;
};

struct Trajectory {
  std::vector<Eigen::Vector3f> position;
  std::vector<LookAngles> look;
};

struct BenchRun {
  Trajectory truth;     // A as A sees itself
  Trajectory observed;  // A as B sees it
};

std::vector<NetProfile> NetProfiles() {
  return {
      {.name = "lan", .fault = {.min_delay_ticks = 1, .max_delay_ticks = 1}},
      {.name = "wan", .fault = {.min_delay_ticks = 3, .max_delay_ticks = 6}},
  };
}

void PredictNeutral(z13::Settings& settings) {
  settings.net->remote_input_prediction = z13::fbs::net::RemoteInputPrediction::Neutral;
}

void SendEveryTick(z13::Settings& settings) {
  settings.net->send_interval_ticks = 1;
  settings.core->max_rollback_delay_ticks = 0;
}

std::vector<TuningVariant> TuningVariants() {
  return {
      {.name = "default", .apply = [](z13::Settings&) {}},
      {.name = "send1", .apply = [](z13::Settings& settings) { settings.net->send_interval_ticks = 1; }},
      {.name = "rollback0", .apply = [](z13::Settings& settings) { settings.core->max_rollback_delay_ticks = 0; }},
      {.name = "send1+rollback0", .apply = SendEveryTick},
      {.name = "neutral", .apply = PredictNeutral},
      {.name = "neutral+send1+rb0",
       .apply =
           [](z13::Settings& settings) {
             PredictNeutral(settings);
             SendEveryTick(settings);
           }},
  };
}

flecs::entity PlayerIn(Z13TestWorld& world, uint32_t player_id) {
  return world.World().lookup(z13::gameplay::PlayerEntityName(player_id).c_str());
}

void Sample(Z13TestWorld& world, uint32_t player_id, Trajectory& trajectory) {
  const flecs::entity player = PlayerIn(world, player_id);
  trajectory.position.push_back(z13::math::ExtractTranslation<float>(player.get<Eigen::Matrix4f>()));
  trajectory.look.push_back(player.get<LookAngles>());
}

void UpdateHeldKeys(Z13TestWorld& world, const std::set<Keycode>& held, const std::set<Keycode>& wanted) {
  for (const Keycode key : held) {
    if (!wanted.contains(key)) {
      world.EmitInput(KeyUp(key));
    }
  }
  for (const Keycode key : wanted) {
    if (!held.contains(key)) {
      world.EmitInput(KeyDown(key));
    }
  }
}

bool IsConnected(Z13TestWorld& world) {
  return world.World().has<z13::gameplay::Gameplay>() &&
      world.World().get<ConnectionStatus>().state == ConnectionState::kConnected;
}

std::optional<BenchRun> Run(const Scenario& scenario, const NetProfile& profile, const TuningVariant& variant) {
  z13::Settings settings = z13::MakeSettings();
  variant.apply(settings);
  auto network = std::make_shared<InMemoryNetwork>();
  network->SetFaultConfig(profile.fault);
  Z13TestWorld server(/*skip_main_menu=*/false, {std::string(kServerArg)}, network, settings);
  const std::vector<std::string> client_args {std::string(kConnectArg), std::string(kTestServerEndpoint)};
  Z13TestWorld client_a(/*skip_main_menu=*/false, client_args, network, settings);
  Z13TestWorld client_b(/*skip_main_menu=*/false, client_args, network, settings);
  const auto tick = [&](uint64_t ticks) {
    RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, ticks, [] { return false; });
  };

  if (!RunNetworkUntil(*network, {server, client_a, client_b}, kNetTestDeltaTime, kMaxNetTestTicks, [&] {
        return IsConnected(client_a) && IsConnected(client_b);
      })) {
    return std::nullopt;
  }
  tick(kSettleTicks);
  const uint32_t a_id = *client_a.World().get<z13::gameplay::LocalPlayer>().id;
  if (!PlayerIn(client_b, a_id)) {
    return std::nullopt;
  }

  BenchRun run;
  std::set<Keycode> held;
  const auto sample = [&] {
    Sample(client_a, a_id, run.truth);
    Sample(client_b, a_id, run.observed);
  };
  const auto step = [&] {
    tick(1);
    sample();
  };
  sample();
  for (const Segment& segment : scenario.segments) {
    UpdateHeldKeys(client_a, held, segment.keys);
    held = segment.keys;
    for (int t = 0; t < segment.ticks; ++t) {
      if (!segment.look_delta.isZero()) {
        z13::input::MouseMoveEvent look;
        look.delta = {.x = segment.look_delta.x(), .y = segment.look_delta.y()};
        client_a.EmitInput(look);
      }
      step();
    }
  }
  UpdateHeldKeys(client_a, held, {});
  for (int t = 0; t < kTailTicks; ++t) {
    step();
  }
  return run;
}

std::filesystem::path OutputDir() {
  const char* configured = std::getenv(kOutputDirEnv.data());
  return configured ? std::filesystem::path(configured)
                    : std::filesystem::temp_directory_path() / kDefaultOutputDirName;
}

void WriteTrace(const std::filesystem::path& path, const BenchRun& run) {
  std::ofstream out(path);
  out << "tick,truth_x,truth_y,truth_z,truth_yaw,truth_pitch,seen_x,seen_y,seen_z,seen_yaw,seen_pitch\n";
  for (size_t t = 0; t < run.truth.position.size(); ++t) {
    const Eigen::Vector3f& truth = run.truth.position[t];
    const Eigen::Vector3f& seen = run.observed.position[t];
    out << std::format(
        "{},{},{},{},{},{},{},{},{},{},{}\n", t, truth.x(), truth.y(), truth.z(), run.truth.look[t].yaw_deg,
        run.truth.look[t].pitch_deg, seen.x(), seen.y(), seen.z(), run.observed.look[t].yaw_deg,
        run.observed.look[t].pitch_deg);
  }
}

std::string Optional(const std::optional<float>& value, std::string_view format_spec) {
  return value ? std::vformat(format_spec, std::make_format_args(*value)) : std::string("-");
}

std::string MetricsCsv(const TrackingMetrics& m) {
  return std::format(
      "{},{},{},{},{},{},{},{},{},{}", m.lag_ticks, m.rms_error, m.max_error, m.max_off_path,
      Optional(m.path_ratio, "{}"), m.max_step, m.truth_max_step, m.rms_acceleration, m.truth_rms_acceleration,
      Optional(m.stall_fraction, "{}"));
}

std::string MetricsRow(const TrackingMetrics& m) {
  return std::format(
      "{:>4} {:>7.3f} {:>7.3f} {:>7.3f} {:>6} {:>6.3f}/{:<6.3f} {:>6.3f}/{:<6.3f} {:>5}", m.lag_ticks, m.rms_error,
      m.max_error, m.max_off_path, Optional(m.path_ratio, "{:.3f}"), m.max_step, m.truth_max_step,
      m.rms_acceleration, m.truth_rms_acceleration, Optional(m.stall_fraction, "{:.2f}"));
}

constexpr std::string_view kSummaryHeader =
    "profile,tuning,"
    "pos_lag,pos_rms,pos_max,pos_off_path,pos_path_ratio,pos_max_step,pos_truth_max_step,pos_rms_accel,"
    "pos_truth_rms_accel,pos_stall,"
    "look_lag,look_rms,look_max,look_off_path,look_path_ratio,look_max_step,look_truth_max_step,look_rms_accel,"
    "look_truth_rms_accel,look_stall\n";

void RunScenario(const Scenario& scenario) {
  const std::filesystem::path dir = OutputDir() / scenario.name;
  std::filesystem::create_directories(dir);
  std::ofstream summary(dir / kSummaryFileName);
  summary << kSummaryHeader;
  const std::string metrics_header = std::format(
      "{:>4} {:>7} {:>7} {:>7} {:>6} {:>13} {:>13} {:>5}", "lag", "rms", "max", "offpath", "path", "step/truth",
      "accel/truth", "stall");
  std::cout << std::format(
      "\n== {} ==   (B's view of A; position in world units, look in degrees)\n{:<24}|          {}\n",
      scenario.name, "", metrics_header);
  for (const NetProfile& profile : NetProfiles()) {
    for (const TuningVariant& variant : TuningVariants()) {
      const std::optional<BenchRun> run = Run(scenario, profile, variant);
      const std::string label = std::format("{} {}", profile.name, variant.name);
      if (!run) {
        ADD_FAILURE() << label << ": the session never got going";
        continue;
      }
      const TrackingMetrics position =
          z13::testing::MeasureTracking(run->truth.position, run->observed.position, kMaxLagTicks);
      const TrackingMetrics look = z13::testing::MeasureTracking(
          z13::testing::UnwrappedLook(run->truth.look), z13::testing::UnwrappedLook(run->observed.look),
          kMaxLagTicks);
      std::cout << std::format("{:<24}| position {}\n{:<24}| look     {}\n", label, MetricsRow(position), "",
                               MetricsRow(look));
      WriteTrace(dir / std::format("{}_{}.csv", profile.name, variant.name), *run);
      summary << std::format("{},{},{},{}\n", profile.name, variant.name, MetricsCsv(position), MetricsCsv(look));
    }
  }
  std::cout << std::format("traces: {}\n", dir.string()) << std::flush;
}

TEST(SyncBench, DISABLED_RunAndStop) {
  RunScenario({.name = "run_stop",
               .segments = {{.ticks = 60, .keys = {Keycode::KEY_W}},
                            {.ticks = 40},
                            {.ticks = 30, .keys = {Keycode::KEY_W}},
                            {.ticks = 40}}});
}

TEST(SyncBench, DISABLED_Strafe) {
  std::vector<Segment> segments;
  for (int i = 0; i < 6; ++i) {
    segments.push_back({.ticks = 20, .keys = {i % 2 == 0 ? Keycode::KEY_A : Keycode::KEY_D}});
  }
  RunScenario({.name = "strafe", .segments = std::move(segments)});
}

TEST(SyncBench, DISABLED_LookSweep) {
  RunScenario({.name = "look_sweep",
               .segments = {{.ticks = 30, .look_delta = {kYawDelta, 0}},
                            {.ticks = 30, .look_delta = {-kYawDelta, 0}},
                            {.ticks = 20},
                            {.ticks = 30, .look_delta = {0, kPitchDelta}},
                            {.ticks = 30, .look_delta = {0, -kPitchDelta}},
                            {.ticks = 20}}});
}

TEST(SyncBench, DISABLED_Circle) {
  RunScenario({.name = "circle", .segments = {{.ticks = 120, .keys = {Keycode::KEY_W}, .look_delta = {kYawDelta, 0}}}});
}

TEST(SyncBench, DISABLED_Mixed) {
  RunScenario({.name = "mixed",
               .segments = {{.ticks = 40, .keys = {Keycode::KEY_W}},
                            {.ticks = 30, .keys = {Keycode::KEY_W, Keycode::KEY_A}, .look_delta = {kYawDelta, 0}},
                            {.ticks = 20, .look_delta = {-kYawDelta, kPitchDelta}},
                            {.ticks = 30, .keys = {Keycode::KEY_S, Keycode::KEY_D}},
                            {.ticks = 15},
                            {.ticks = 25, .keys = {Keycode::KEY_W}, .look_delta = {-kYawDelta, -kPitchDelta}}}});
}

}  // namespace
}  // namespace z13::net
