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
#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/world_serializer.h>
#include <lib_core/utils/math.h>

#include <sectors/sector_index.h>
#include <z13/components/block.h>
#include <z13/components/block_changes.h>
#include <z13/components/player.h>
#include <z13_settings/net_tuning.h>

#include "../../../building_module/tests/support/station_builders.h"
#include "../../../station_module/tests/support/room_blocks.h"
#include "../../../z13_module/tests/support/z13_test_world.h"

namespace z13::net {
namespace {

using sectors::MemberKind;
using sectors::SectorIndex;
using z13::station::Block;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;

constexpr int kSteps = 60;
constexpr std::array kSeeds {5U, 41U, 2026U};
// Blocks and players land in a cube of this many cells around the origin, across several sectors.
constexpr int kReach = 80;
constexpr uint32_t kMostOpsPerTick = 3;
constexpr uint32_t kFirstExtraPlayer = 20;
constexpr int kExtraPlayers = 3;
constexpr float kBuilderHeightMeters = 1.5f;

int SectorCellsOf(Z13TestWorld& world) {
  return static_cast<int>(world.World().get<z13::NetTuning>().sector_size_cells);
}

Eigen::Vector3i CellOf(const Eigen::Matrix4f& transform) {
  return (z13::math::ExtractTranslation<float>(transform) / z13::station::kCellSize).array().floor().cast<int>();
}

// Where the index differs from placing every player and block afresh; empty when nowhere.
std::string Mismatch(Z13TestWorld& world) {
  const flecs::world w = world.World();
  const SectorIndex& index = w.get<SectorIndex>();
  const int sector_cells = SectorCellsOf(world);
  std::string found;
  if (index.SectorCells() != sector_cells) {
    found += "sector size; ";
  }
  size_t blocks {};
  w.query<const Block>().each([&](flecs::entity e, const Block& block) {
    ++blocks;
    if (index.Locate({MemberKind::kBlock, e.id()}) != sectors::SectorOf(block.cell, sector_cells)) {
      found += std::format("block {}; ", e.name().c_str());
    }
  });
  size_t players {};
  w.query<const z13::gameplay::Player, const Eigen::Matrix4f>().each(
      [&](const z13::gameplay::Player& player, const Eigen::Matrix4f& transform) {
        ++players;
        if (index.Locate({MemberKind::kPlayer, player.id}) != sectors::SectorOf(CellOf(transform), sector_cells)) {
          found += std::format("player {}; ", player.id);
        }
      });
  if (index.Count(MemberKind::kBlock) != blocks || index.Count(MemberKind::kPlayer) != players) {
    found += std::format("holds {} blocks and {} players of {} and {}; ", index.Count(MemberKind::kBlock),
                         index.Count(MemberKind::kPlayer), blocks, players);
  }
  return found;
}

TEST(SectorSystemTest, EveryPlayerIsInTheSectorOfItsPosition) {
  Z13TestWorld world = StationWorld();
  world.Tick();

  EXPECT_EQ(Mismatch(world), "");
  EXPECT_GT(world.World().get<SectorIndex>().Count(MemberKind::kPlayer), 0u);
}

TEST(SectorSystemTest, ABlockIsInTheSectorOfItsAnchorCell) {
  Z13TestWorld world = StationWorld();
  world.Tick();
  const int sector_cells = SectorCellsOf(world);
  const Eigen::Vector3i cell = Eigen::Vector3i::Constant(-sector_cells - 1);

  const flecs::entity block = z13::station::testing::AddBlock(world, {.spec = z13::station::CubeSpec(), .cell = cell});
  world.Tick();

  EXPECT_EQ(world.World().get<SectorIndex>().Locate({MemberKind::kBlock, block.id()}),
            sectors::SectorOf(cell, sector_cells));
  EXPECT_EQ(Mismatch(world), "");
}

TEST(SectorSystemTest, ADestroyedBlockLeavesTheIndex) {
  Z13TestWorld world = StationWorld();
  const flecs::entity block =
      z13::station::testing::AddBlock(world, {.spec = z13::station::CubeSpec(), .cell = {100, 100, 100}});
  world.Tick();
  const size_t with_block = world.World().get<SectorIndex>().Count(MemberKind::kBlock);

  z13::station::DestroyBlock(block, block.get<Block>());
  world.Tick();

  EXPECT_EQ(world.World().get<SectorIndex>().Count(MemberKind::kBlock), with_block - 1);
  EXPECT_EQ(Mismatch(world), "");
}

TEST(SectorSystemTest, APlayerWalkingIntoAnotherSectorMovesIntoIt) {
  Z13TestWorld world = StationWorld();
  const flecs::entity builder = z13::testing::AddBuilder(world, kFirstExtraPlayer, Eigen::Matrix4f::Identity());
  world.Tick();
  const auto before = world.World().get<SectorIndex>().Locate({MemberKind::kPlayer, kFirstExtraPlayer});

  const float far_meters = static_cast<float>(SectorCellsOf(world) * 3) * z13::station::kCellSize;
  builder.set(z13::testing::Facing({far_meters, far_meters, kBuilderHeightMeters}, Eigen::Vector3f::UnitX()));
  world.Tick();

  EXPECT_NE(world.World().get<SectorIndex>().Locate({MemberKind::kPlayer, kFirstExtraPlayer}), before);
  EXPECT_EQ(Mismatch(world), "");
}

TEST(SectorSystemTest, APlayerWhoLeavesLeavesTheIndex) {
  Z13TestWorld world = StationWorld();
  const flecs::entity builder = z13::testing::AddBuilder(world, kFirstExtraPlayer, Eigen::Matrix4f::Identity());
  world.Tick();
  ASSERT_TRUE(world.World().get<SectorIndex>().Locate({MemberKind::kPlayer, kFirstExtraPlayer}).has_value());

  builder.destruct();
  world.Tick();

  EXPECT_FALSE(world.World().get<SectorIndex>().Locate({MemberKind::kPlayer, kFirstExtraPlayer}).has_value());
  EXPECT_EQ(Mismatch(world), "");
}

TEST(SectorSystemTest, AnotherSectorSizeRegroupsEverything) {
  Z13TestWorld world = StationWorld();
  z13::station::testing::AddBlock(world, {.spec = z13::station::CubeSpec(), .cell = {70, -70, 30}});
  world.Tick();
  ASSERT_EQ(Mismatch(world), "");

  auto tuning = world.World().get<z13::NetTuning>();
  tuning.sector_size_cells *= 2;
  world.World().set<z13::NetTuning>(tuning);
  world.Tick();

  EXPECT_EQ(Mismatch(world), "");
}

enum class Op : uint8_t { kBuild, kDestroy, kMove, kCapture, kRestore };
constexpr std::array kOpWeights {5U, 3U, 3U, 1U, 1U};

Op NextOp(std::mt19937& random) {
  uint32_t roll = random() % std::ranges::fold_left(kOpWeights, 0U, std::plus<> {});
  size_t chosen = 0;
  while (roll >= kOpWeights[chosen]) {
    roll -= kOpWeights[chosen];
    ++chosen;
  }
  return static_cast<Op>(chosen);
}

int Between(std::mt19937& random, int low, int high) {
  return low + static_cast<int>(random() % static_cast<uint32_t>(high - low + 1));
}

class SectorSystemEditsTest : public ::testing::TestWithParam<uint32_t> {};

// Builds, destroys, moves players and restores of earlier snapshots, several to a tick: the
// index ends each tick where placing every player and block afresh does.
TEST_P(SectorSystemEditsTest, TheIndexFollowsRandomEditsAndRestores) {
  std::mt19937 random(GetParam());
  Z13TestWorld world = StationWorld();
  std::vector<flecs::entity> builders;
  for (int i = 0; i < kExtraPlayers; ++i) {
    builders.push_back(z13::testing::AddBuilder(world, kFirstExtraPlayer + static_cast<uint32_t>(i),
                                                Eigen::Matrix4f::Identity()));
  }
  world.Tick();
  ASSERT_EQ(Mismatch(world), "");

  std::vector<z13::flecs_tools::WorldSnapshot> snapshots;
  std::vector<flecs::entity> ours;
  int restores {};
  for (int step = 0; step < kSteps; ++step) {
    const uint32_t ops = 1 + random() % kMostOpsPerTick;
    for (uint32_t op = 0; op < ops; ++op) {
      std::erase_if(ours, [](flecs::entity e) { return !e.is_alive(); });
      switch (NextOp(random)) {
        case Op::kBuild: {
          const Eigen::Vector3i cell(
              Between(random, -kReach, kReach), Between(random, -kReach, kReach), Between(random, -kReach, kReach));
          ours.push_back(z13::station::testing::AddBlock(world, {.spec = z13::station::CubeSpec(), .cell = cell}));
          break;
        }
        case Op::kDestroy:
          if (!ours.empty()) {
            const flecs::entity doomed = ours[random() % ours.size()];
            z13::station::DestroyBlock(doomed, doomed.get<Block>());
          }
          break;
        case Op::kMove: {
          const Eigen::Vector3f to(
              static_cast<float>(Between(random, -kReach, kReach)), static_cast<float>(Between(random, -kReach, kReach)),
              kBuilderHeightMeters);
          builders[random() % builders.size()].set(
              z13::testing::Facing(to * z13::station::kCellSize, Eigen::Vector3f::UnitX()));
          break;
        }
        case Op::kCapture: {
          flecs::world w = world.World();
          auto snapshot = z13::flecs_tools::CaptureState(w);
          ASSERT_TRUE(snapshot.has_value());
          snapshots.push_back(std::move(*snapshot));
          break;
        }
        case Op::kRestore:
          if (!snapshots.empty()) {
            flecs::world w = world.World();
            ASSERT_TRUE(z13::flecs_tools::RestoreWorld(w, snapshots[random() % snapshots.size()]));
            ours.clear();
            w.query<const Block>().each([&ours](flecs::entity e, const Block&) { ours.push_back(e); });
            ++restores;
          }
          break;
      }
    }
    world.Tick();
    ASSERT_EQ(Mismatch(world), "") << "step " << step;
  }
  EXPECT_GT(restores, 0);
}

INSTANTIATE_TEST_SUITE_P(Seeds, SectorSystemEditsTest, ::testing::ValuesIn(kSeeds));

}  // namespace
}  // namespace z13::net
