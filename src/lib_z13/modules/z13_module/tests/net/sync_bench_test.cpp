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

#include <lib_core/settings/config.h>
#include <lib_core/state/rollback.h>
#include <lib_core/utils/math.h>
#include <lib_core/utils/pose_smoothing.h>

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

namespace ft = z13::flecs_tools;
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
constexpr std::string_view kCostFileName = "cost.csv";
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

// What B draws: the simulated pose as is, or chased by the render's smoothing.
struct SmoothingVariant {
  std::string_view name;
  float smooth_time_seconds {};
};

using Trajectory = std::vector<Eigen::Matrix4f>;

// What a variant costs, per second of the scenario.
struct BenchCost {
  double uplink_bytes {};    // everything the clients sent the server
  double rollbacks {};       // B's
  double replayed_ticks {};  // B's
};

struct BenchRun {
  Trajectory truth;     // A as A sees itself
  Trajectory observed;  // A as B simulates it
  BenchCost cost;
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
      {.name = "send1+rb0", .apply = SendEveryTick},
      {.name = "neutral", .apply = PredictNeutral},
      {.name = "neutral+send1+rb0",
       .apply =
           [](z13::Settings& settings) {
             PredictNeutral(settings);
             SendEveryTick(settings);
           }},
  };
}

std::vector<SmoothingVariant> SmoothingVariants() {
  return {
      {.name = "raw"},
      {.name = "s50", .smooth_time_seconds = 0.05f},
      {.name = "s75", .smooth_time_seconds = 0.075f},
      {.name = "s100", .smooth_time_seconds = 0.1f},
      {.name = "s150", .smooth_time_seconds = 0.15f},
  };
}

flecs::entity PlayerIn(Z13TestWorld& world, uint32_t player_id) {
  return world.World().lookup(z13::gameplay::PlayerEntityName(player_id).c_str());
}

void Sample(Z13TestWorld& world, uint32_t player_id, Trajectory& trajectory) {
  trajectory.push_back(PlayerIn(world, player_id).get<Eigen::Matrix4f>());
}

Trajectory Drawn(const Trajectory& simulated, const SmoothingVariant& smoothing) {
  const z13::VisualSmoothing defaults(*z13::MakeSettings().visual_smoothing);
  const z13::math::PoseSmoothingParams params {
      .smooth_time_seconds = smoothing.smooth_time_seconds,
      .snap_distance = defaults.snap_distance,
      .snap_angle_rad = z13::math::ToRadians(defaults.snap_angle_deg),
  };
  Trajectory drawn;
  z13::math::SmoothedPose pose = z13::math::PoseAt(simulated.front());
  for (const Eigen::Matrix4f& target : simulated) {
    z13::math::ChasePose(pose, target, kNetTestDeltaTime, params);
    drawn.push_back(z13::math::DrawnTransform(pose, target));
  }
  return drawn;
}

std::vector<Eigen::Vector3f> Positions(const Trajectory& trajectory) {
  std::vector<Eigen::Vector3f> positions;
  for (const Eigen::Matrix4f& transform : trajectory) {
    positions.push_back(z13::math::ExtractTranslation(transform));
  }
  return positions;
}

std::vector<Eigen::Vector3f> Looks(const Trajectory& trajectory) {
  std::vector<LookAngles> looks;
  for (const Eigen::Matrix4f& transform : trajectory) {
    looks.push_back(z13::gameplay::LookAnglesFromTransform(transform));
  }
  return z13::testing::UnwrappedLook(looks);
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
  const uint64_t uplink_before = network->TrafficTo(z13::kDefaultServerPort).bytes;
  const ft::RollbackMetrics rollbacks_before = client_b.World().get<ft::RollbackMetrics>();
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
  const double seconds = static_cast<double>(run.truth.size()) * kNetTestDeltaTime;
  const ft::RollbackMetrics& rollbacks = client_b.World().get<ft::RollbackMetrics>();
  run.cost = {
      .uplink_bytes = static_cast<double>(network->TrafficTo(z13::kDefaultServerPort).bytes - uplink_before) / seconds,
      .rollbacks = static_cast<double>(rollbacks.rollbacks - rollbacks_before.rollbacks) / seconds,
      .replayed_ticks = static_cast<double>(rollbacks.replayed_ticks - rollbacks_before.replayed_ticks) / seconds,
  };
  return run;
}

std::filesystem::path OutputDir() {
  const char* configured = std::getenv(kOutputDirEnv.data());
  return configured ? std::filesystem::path(configured)
                    : std::filesystem::temp_directory_path() / kDefaultOutputDirName;
}

void WriteTrace(const std::filesystem::path& path, const Trajectory& truth, const Trajectory& drawn) {
  std::ofstream out(path);
  out << "tick,truth_x,truth_y,truth_z,truth_yaw,truth_pitch,seen_x,seen_y,seen_z,seen_yaw,seen_pitch\n";
  const auto truth_position = Positions(truth);
  const auto truth_look = Looks(truth);
  const auto seen_position = Positions(drawn);
  const auto seen_look = Looks(drawn);
  for (size_t t = 0; t < truth.size(); ++t) {
    out << std::format(
        "{},{},{},{},{},{},{},{},{},{},{}\n", t, truth_position[t].x(), truth_position[t].y(), truth_position[t].z(),
        truth_look[t].x(), truth_look[t].y(), seen_position[t].x(), seen_position[t].y(), seen_position[t].z(),
        seen_look[t].x(), seen_look[t].y());
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

std::string MetricsColumns(const TrackingMetrics& m) {
  return std::format(
      "{:>4} {:>6.2f} {:>6.2f} {:>5} {:>6.3f} {:>5}", m.lag_ticks, m.rms_error, m.max_off_path,
      Optional(m.path_ratio, "{:.2f}"), m.rms_acceleration, Optional(m.stall_fraction, "{:.2f}"));
}

constexpr std::string_view kSummaryHeader =
    "profile,tuning,smoothing,"
    "pos_lag,pos_rms,pos_max,pos_off_path,pos_path_ratio,pos_max_step,pos_truth_max_step,pos_rms_accel,"
    "pos_truth_rms_accel,pos_stall,"
    "look_lag,look_rms,look_max,look_off_path,look_path_ratio,look_max_step,look_truth_max_step,look_rms_accel,"
    "look_truth_rms_accel,look_stall\n";

void RunScenario(const Scenario& scenario) {
  const std::filesystem::path dir = OutputDir() / scenario.name;
  std::filesystem::create_directories(dir);
  std::ofstream summary(dir / kSummaryFileName);
  summary << kSummaryHeader;
  std::ofstream costs(dir / kCostFileName);
  costs << "profile,tuning,uplink_bytes_per_s,rollbacks_per_s,replayed_ticks_per_s\n";
  const std::string columns =
      std::format("{:>4} {:>6} {:>6} {:>5} {:>6} {:>5}", "lag", "rms", "offpth", "path", "accel", "stall");
  std::cout << std::format(
      "\n== {} ==   (B's view of A; position in world units, look in degrees)\n{:<26}| position {} | look {}\n",
      scenario.name, "", columns, columns);
  for (const NetProfile& profile : NetProfiles()) {
    for (const TuningVariant& variant : TuningVariants()) {
      const std::optional<BenchRun> run = Run(scenario, profile, variant);
      if (!run) {
        ADD_FAILURE() << profile.name << " " << variant.name << ": the session never got going";
        continue;
      }
      const auto truth_position = Positions(run->truth);
      const auto truth_look = Looks(run->truth);
      for (const SmoothingVariant& smoothing : SmoothingVariants()) {
        const Trajectory drawn = Drawn(run->observed, smoothing);
        const TrackingMetrics position = z13::testing::MeasureTracking(truth_position, Positions(drawn), kMaxLagTicks);
        const TrackingMetrics look = z13::testing::MeasureTracking(truth_look, Looks(drawn), kMaxLagTicks);
        const std::string label = std::format("{} {} {}", profile.name, variant.name, smoothing.name);
        std::cout << std::format("{:<26}|          {} |      {}\n", label, MetricsColumns(position), MetricsColumns(look));
        WriteTrace(dir / std::format("{}_{}_{}.csv", profile.name, variant.name, smoothing.name), run->truth, drawn);
        summary << std::format(
            "{},{},{},{},{}\n", profile.name, variant.name, smoothing.name, MetricsCsv(position), MetricsCsv(look));
      }
      const TrackingMetrics reference = z13::testing::MeasureTracking(truth_position, truth_position, 0);
      const TrackingMetrics look_reference = z13::testing::MeasureTracking(truth_look, truth_look, 0);
      std::cout << std::format(
          "{:<26}| A's accel {:.3f} / {:.3f}; uplink {:.0f} B/s, B rollbacks {:.1f}/s replaying {:.0f} ticks/s\n", "",
          reference.rms_acceleration, look_reference.rms_acceleration, run->cost.uplink_bytes, run->cost.rollbacks,
          run->cost.replayed_ticks);
      costs << std::format(
          "{},{},{},{},{}\n", profile.name, variant.name, run->cost.uplink_bytes, run->cost.rollbacks,
          run->cost.replayed_ticks);
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
