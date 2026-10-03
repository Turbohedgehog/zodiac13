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
#include <vector>

#include <flecs.h>

#include <lib_core/rollback.h>
#include <lib_core/simulation_clock.h>
#include <lib_core/world_serializer.h>
#include <lib_core/world_snapshot_history.h>
#include <lib_core/world_state.h>

namespace z13 {
namespace {

namespace ft = z13::flecs_tools;
using ft::FrameKind;

class FrameStartTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ft::RegisterStateMeta(world_);
    ft::OnFrameStart(world_, [this](flecs::world&, FrameKind kind) { frames_.push_back(kind); });
  }

  uint64_t Tick() const { return world_.get<ft::SimulationClock>().tick; }

  flecs::world world_;
  std::vector<FrameKind> frames_;
};

TEST_F(FrameStartTest, ReportsTheCallersFrameKind) {
  ft::TickWorld(world_, 1.f);
  ft::TickWorld(world_, 1.f, FrameKind::kCatchUp);

  EXPECT_EQ(frames_, (std::vector {FrameKind::kLive, FrameKind::kCatchUp}));
  EXPECT_EQ(Tick(), 2u);
}

TEST_F(FrameStartTest, ExtraClockTicksAreCatchUpFrames) {
  ft::RequestClockAdjust(world_, 2);

  ft::TickWorld(world_, 1.f);

  EXPECT_EQ(frames_, (std::vector {FrameKind::kCatchUp, FrameKind::kCatchUp, FrameKind::kLive}));
}

TEST_F(FrameStartTest, RolledBackTicksAreReplayFrames) {
  constexpr uint64_t kSnapshotTick {3};
  constexpr uint64_t kPresentTick {6};
  while (Tick() < kSnapshotTick) {
    ft::TickWorld(world_, 1.f);
  }
  world_.get_mut<ft::WorldSnapshotHistory>().history.Push({.tick = Tick(), .snapshot = ft::CaptureState(world_).value()});
  while (Tick() < kPresentTick) {
    ft::TickWorld(world_, 1.f);
  }
  frames_.clear();

  ft::RequestRollback(world_, kSnapshotTick, kPresentTick + 1);
  ft::TickWorld(world_, 1.f);

  ASSERT_EQ(world_.get<ft::RollbackMetrics>().rollbacks, 1u);
  EXPECT_EQ(Tick(), kPresentTick + 1);
  const std::vector<FrameKind> expected {
      FrameKind::kLive, FrameKind::kReplay, FrameKind::kReplay, FrameKind::kReplay, FrameKind::kLive};
  EXPECT_EQ(frames_, expected);
}

TEST_F(FrameStartTest, SystemFollowsATickSourceSetEarlierInTheSameFrame) {
  struct Gate {};
  struct LatePhase {};
  world_.component<LatePhase>().add(flecs::Phase).depends_on(flecs::OnStore);
  world_.entity<Gate>().set<flecs::TickSource>({.tick = true, .time_elapsed = 0.f});
  bool open = false;
  int runs = 0;
  world_.system("OpenGate").kind(flecs::PreUpdate).immediate().run([&](flecs::iter&) {
    world_.entity<Gate>().get_mut<flecs::TickSource>().tick = open;
  });
  world_.system("Gated").kind<LatePhase>().tick_source<Gate>().run([&](flecs::iter&) { ++runs; });

  open = false;
  ft::TickWorld(world_, 1.f);
  open = true;
  ft::TickWorld(world_, 1.f);

  EXPECT_EQ(runs, 1);
}

}  // namespace
}  // namespace z13
