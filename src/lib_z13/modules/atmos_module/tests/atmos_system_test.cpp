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
#include <numeric>
#include <ranges>
#include <string>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/state_markers.h>
#include <lib_core/utils/math.h>

#include <atmos/air.h>
#include <atmos/port_names.h>
#include <primitives/palette.h>
#include <rooms/room_cache.h>
#include <z13/components/utility/atmosphere.h>
#include <z13/components/station/block.h>
#include <z13/components/station/block_changes.h>
#include <z13/components/station/conduit.h>
#include <z13/components/station/door.h>
#include <z13/components/station/grid.h>
#include <z13/components/gameplay/gameplay.h>
#include <z13/components/station/overlay_layers.h>
#include <z13/components/utility/utilities.h>
#include <player/camera_look.h>
#include <z13_settings/atmos_tuning.h>
#include <z13_settings/door_tuning.h>
#include <z13_settings/physics_tuning.h>

#include "../../building_module/tests/support/station_builders.h"
#include "../../z13_module/tests/support/building_test_helpers.h"
#include "../../station_module/tests/support/room_blocks.h"
#include "../../z13_module/tests/support/z13_test_world.h"
#include "../src/room_air_carry.h"
#include "../src/room_air_store.h"

// The air of rooms built of shipped primitives.
namespace z13::atmos_module {
namespace {

using z13::station::Block;
using z13::station::DoorMode;
using z13::station::DoorState;
using z13::station::kCellSize;
using z13::station::kDoorOpen;
using z13::station::MainGridData;
using z13::station::rooms::RoomCache;
using z13::testing::StationWorld;
using z13::testing::Z13TestWorld;
using z13::utility::AirlockDoors;
using z13::station::DoorInterlock;
using z13::station::InterlockReason;
using z13::utility::RoomAirEntry;
using z13::utility::atmos::Air;
namespace atmos = z13::utility::atmos;
namespace testing = z13::station::testing;

// The cable kind of assets/station/conduits.json.
constexpr uint32_t kCableKind = 1;

// Rounding in the pressure of an air built from integers: the divisions on the way.
constexpr int64_t kPascalsTolerance = 2;
constexpr int64_t kKelvinTolerance = 1;
// The share of oxygen in thousandths, rounded down.
constexpr int64_t kPermilleTolerance = 1;
// Two rooms joined by a door level their pressures only as far as the shares round and the flow's cap allows.
constexpr int64_t kLevelledPascals = atmos::kStandardPascals / 50;
// An air so thin it is as good as none: a hundredth of the standard.
constexpr int64_t kThinPascals = atmos::kStandardPascals / 100;
// Enough ticks for the rooms to be built, seeded and one step of the air to run.
constexpr int kSettleTicks = 14;
// A step of the air is `step_ticks` ticks; this many of them let a door or a unit do its work.
constexpr int kLongRunTicks = 600;
constexpr int kPercentOfAir = 50;
constexpr int kPercent = 100;

// A cell in each half of the sealed box split by the door partition, and one in the whole box.
const Eigen::Vector3i kLeftCell = testing::kRoomCorner + Eigen::Vector3i(3, 6, 3);
const Eigen::Vector3i kRightCell = testing::kRoomCorner + Eigen::Vector3i(8, 6, 3);
const Eigen::Vector3i kBoxCell = testing::kRoomCorner + Eigen::Vector3i(6, 6, 3);

// The partition door and the outer door of the box, the airlock controller in the chamber between them, and the
// places two cells before each door.
const Eigen::Vector3i kInnerDoorCell = testing::kRoomCorner + Eigen::Vector3i(5, 1, 1);
const Eigen::Vector3i kOuterDoorCell = testing::kRoomCorner + Eigen::Vector3i(testing::kSide + 1, 1, 1);
const Eigen::Vector3i kControllerCell = testing::kRoomCorner + Eigen::Vector3i(10, 7, 2);
// A reactor in the chamber's far corner and a cable from it to the controller.
const Eigen::Vector3i kReactorCell = testing::kRoomCorner + Eigen::Vector3i(6, 6, 2);
const Eigen::Vector3i kCableCell = testing::kRoomCorner + Eigen::Vector3i(10, 6, 2);
const Eigen::Vector2f kBeforeInnerDoor {3.f, 3.5f};
const Eigen::Vector2f kBeforeOuterDoor {9.f, 3.5f};

// The sealed box with its last wall holding a door to the outside.
std::vector<Block> BoxWithAnOuterDoor() {
  constexpr int outer = testing::kSide + 2;
  std::vector<Block> blocks = testing::SealedBox();
  blocks.pop_back();
  blocks.push_back(testing::At(testing::kDoor, {testing::kDoorWidth, 1, testing::kDoorHeight}, testing::kAlongY,
                               {outer - 1, 1, 1}));
  blocks.push_back(testing::At(testing::kWall, {testing::kSide - testing::kDoorWidth, 1, testing::kHeight},
                               testing::kAlongY, {outer - 1, 1 + testing::kDoorWidth, 1}));
  blocks.push_back(testing::At(testing::kWall, {testing::kDoorWidth, 1, testing::kHeight - testing::kDoorHeight},
                               testing::kAlongY, {outer - 1, 1, 1 + testing::kDoorHeight}));
  return blocks;
}

flecs::entity FindBlock(Z13TestWorld& world, std::string_view primitive) {
  flecs::entity found;
  world.World().query_builder<const Block>().build().each([&](flecs::entity block, const Block& placed) {
    if (placed.spec.type_id == testing::IdOf(primitive)) {
      found = block;
    }
  });
  return found;
}

Eigen::Matrix4f TransformAt(const Eigen::Vector3f& position) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  z13::math::SetTranslation(position, transform);
  return transform;
}

void Advance(Z13TestWorld& world, int ticks = kSettleTicks) {
  z13::testing::Ticks(world, ticks);
}

void OpenDoor(flecs::entity door) {
  door.set(DoorState {.mode = DoorMode::kManual, .position = kDoorOpen, .open_wanted = true, .leaf_blocks = false});
}

std::vector<RoomAirEntry> Rooms(Z13TestWorld& world) {
  return LoadRoomAir(world.World().query<const RoomAirEntry>());
}

size_t RoomIndexAt(Z13TestWorld& world, const Eigen::Vector3i& cell) {
  const auto graph = MainGridData<RoomCache>(world.World())->get().Current()->get();
  const auto ids = graph.ByAnchor();
  return static_cast<size_t>(std::ranges::find(ids, *graph.RoomAt(cell)) - ids.begin());
}

int64_t VolumeAt(Z13TestWorld& world, const Eigen::Vector3i& cell) {
  const auto graph = MainGridData<RoomCache>(world.World())->get().Current()->get();
  return graph.rooms.at(*graph.RoomAt(cell)).volume_cells;
}

Air AirAt(Z13TestWorld& world, const Eigen::Vector3i& cell) {
  return AirOf(Rooms(world).at(RoomIndexAt(world, cell)));
}

int64_t PressureAt(Z13TestWorld& world, const Eigen::Vector3i& cell) {
  return atmos::PressurePascals(AirAt(world, cell), VolumeAt(world, cell));
}

void SetAirAt(Z13TestWorld& world, const Eigen::Vector3i& cell, const Air& air) {
  const Eigen::Vector3i anchor = Rooms(world).at(RoomIndexAt(world, cell)).anchor;
  world.World().query<const RoomAirEntry>().each([&](flecs::entity room, const RoomAirEntry& entry) {
    if (entry.anchor == anchor) {
      room.set(EntryOf(anchor, air));
    }
  });
}

Air Everything(Z13TestWorld& world) {
  Air total;
  for (const auto& entry : Rooms(world)) {
    total += AirOf(entry);
  }
  return total;
}

class AtmosSystemTest : public ::testing::Test {
 protected:
  void SetUp() override { world_.Tick(); }

  void Build(const std::vector<Block>& blocks) {
    testing::AddBlocks(world_, blocks);
    Advance(world_);
  }

  Z13TestWorld world_ = StationWorld();
};

TEST_F(AtmosSystemTest, ASealedRoomStartsWithStandardAir) {
  Build(testing::SealedBox());

  const Air air = AirAt(world_, kBoxCell);
  EXPECT_NEAR(PressureAt(world_, kBoxCell), atmos::kStandardPascals, kPascalsTolerance);
  EXPECT_NEAR(atmos::Kelvin(air), atmos::kStandardKelvin, kKelvinTolerance);
  EXPECT_NEAR(air.millimoles[static_cast<size_t>(atmos::Gas::kOxygen)] * atmos::kPermille / air.TotalMillimoles(),
              atmos::kOxygenPermille, kPermilleTolerance);
}

TEST_F(AtmosSystemTest, ABoxSplitByAPartitionKeepsEveryMillimoleAndSharesThePressure) {
  Build(testing::SealedBox());
  const Air before = Everything(world_);

  testing::AddBlocks(world_, testing::DoorPartition());
  Advance(world_);

  EXPECT_EQ(Everything(world_), before);
  EXPECT_NEAR(PressureAt(world_, kLeftCell), PressureAt(world_, kRightCell), kPascalsTolerance);
  EXPECT_GT(PressureAt(world_, kLeftCell), atmos::kStandardPascals) << "the partition took room, the air stays";
}

TEST_F(AtmosSystemTest, AnOpenDoorLevelsTwoRoomsAndLosesNothing) {
  Build(testing::SealedBox());
  testing::AddBlocks(world_, testing::DoorPartition());
  Advance(world_);
  const Air thin = atmos::StandardAir(VolumeAt(world_, kRightCell));
  SetAirAt(world_, kRightCell, atmos::Air {.millimoles = {thin.millimoles[0] / 2, thin.millimoles[1] / 2, 0},
                                           .energy = thin.energy / 2});
  const Air before = Everything(world_);
  Advance(world_);
  ASSERT_LT(PressureAt(world_, kRightCell), PressureAt(world_, kLeftCell)) << "a shut door holds the difference";

  OpenDoor(FindBlock(world_, testing::kDoor));
  Advance(world_, kLongRunTicks);

  EXPECT_NEAR(PressureAt(world_, kLeftCell), PressureAt(world_, kRightCell), kLevelledPascals);
  EXPECT_EQ(Everything(world_), before);
}

TEST_F(AtmosSystemTest, ARoomWithAnOpenDoorToTheOutsideEmptiesAndWhatLeavesIsGone) {
  Build(BoxWithAnOuterDoor());
  ASSERT_NEAR(PressureAt(world_, kBoxCell), atmos::kStandardPascals, kPascalsTolerance);
  const Air before = Everything(world_);

  OpenDoor(FindBlock(world_, testing::kDoor));
  Advance(world_, kLongRunTicks);

  EXPECT_LT(PressureAt(world_, kBoxCell), kThinPascals);
  EXPECT_LT(Everything(world_).TotalMillimoles(), before.TotalMillimoles() / kPercent);
}

TEST_F(AtmosSystemTest, ADoorBetweenAGreatDifferenceInPressureIsInterlockedUntilItLevels) {
  Build(testing::SealedBox());
  testing::AddBlocks(world_, testing::DoorPartition());
  Advance(world_);
  const flecs::entity door = FindBlock(world_, testing::kDoor);
  ASSERT_FALSE(door.has<DoorInterlock>()) << "equal pressures";

  SetAirAt(world_, kRightCell, Air {});
  Advance(world_);
  EXPECT_TRUE(door.has<DoorInterlock>());

  OpenDoor(door);
  Advance(world_, kLongRunTicks);
  EXPECT_FALSE(door.has<DoorInterlock>());
}

TEST_F(AtmosSystemTest, ABodyBreathesOxygenIntoCarbonDioxideAndKeepsTheTotal) {
  Build(testing::SealedBox());
  const Air before = AirAt(world_, kBoxCell);
  const Eigen::Vector3f at = (kBoxCell.cast<float>() + Eigen::Vector3f::Constant(0.5f)) * kCellSize;
  world_.Player().set(TransformAt(at));

  Advance(world_, kLongRunTicks);

  const Air after = AirAt(world_, kBoxCell);
  const auto oxygen = static_cast<size_t>(atmos::Gas::kOxygen);
  const auto carbon_dioxide = static_cast<size_t>(atmos::Gas::kCarbonDioxide);
  EXPECT_LT(after.millimoles[oxygen], before.millimoles[oxygen]);
  EXPECT_EQ(after.millimoles[carbon_dioxide], before.millimoles[oxygen] - after.millimoles[oxygen]);
  EXPECT_EQ(after.TotalMillimoles(), before.TotalMillimoles());
  EXPECT_GT(after.energy, before.energy) << "the body's heat";
}

// A unit and a vent in the sealed box, joined by a pipe of the gas network.
class AtmosUnitTest : public AtmosSystemTest {
 protected:
  static constexpr uint32_t kPipeKind = 3;
  static constexpr int64_t kBigStep = 1'000'000;

  void SetUp() override {
    AtmosSystemTest::SetUp();
    SetUnitValues();
    Build(testing::SealedBox());
    const Eigen::Vector3i unit_cell = testing::kRoomCorner + Eigen::Vector3i(3, 3, 2);
    unit_ = testing::AddBlock(world_, {.spec = {.type_id = testing::IdOf("AtmosUnit"), .size = {2, 2, 2}},
                                       .cell = unit_cell});
    testing::AddBlock(world_, {.spec = {.type_id = testing::IdOf("Vent"), .size = {1, 1, 1}},
                               .cell = testing::kRoomCorner + Eigen::Vector3i(8, 3, 2)});
    world_.World().entity().add<z13::flecs_tools::StateEntity>().set(z13::conduit::Conduit {
        .kind = kPipeKind,
        .cell = testing::kRoomCorner + Eigen::Vector3i(5, 3, 2),
        .axis = z13::conduit::ConduitAxis::kX,
        .length = 3});
    Advance(world_);
  }

  // The shipped unit fills slowly; this one fills a room in a step.
  void SetUnitValues() {
    auto& palette = world_.World().get_mut<z13::building::primitives::BlockPalette>().palette;
    const auto unit = std::ranges::find(palette.primitives, "AtmosUnit", &z13::building::primitives::Primitive::name);
    for (auto& port : unit->ports) {
      for (auto& value : port.values) {
        if (value.name == atmos::kFillValue || value.name == atmos::kHeatValue) {
          value.value = kBigStep * kBigStep;
        }
      }
    }
  }

  void Power() {
    testing::AddBlock(world_, {.spec = {.type_id = testing::IdOf("Reactor"), .size = {4, 4, 4}},
                               .cell = testing::kRoomCorner + Eigen::Vector3i(3, 6, 2)});
    world_.World().entity().add<z13::flecs_tools::StateEntity>().set(z13::conduit::Conduit {
        .kind = kCableKind,
        .cell = testing::kRoomCorner + Eigen::Vector3i(3, 5, 2),
        .axis = z13::conduit::ConduitAxis::kY,
        .length = 1});
    Advance(world_);
  }

  void Thin() { SetAirAt(world_, kBoxCell, atmos::StandardAir(VolumeAt(world_, kBoxCell) * kPercentOfAir / kPercent)); }

  flecs::entity unit_;
};

TEST_F(AtmosUnitTest, APoweredUnitHoldsTheRoomOfItsVentAtItsTarget) {
  Power();
  Thin();
  ASSERT_LT(PressureAt(world_, kBoxCell), atmos::kStandardPascals * kPercentOfAir / kPercent + kPascalsTolerance);

  Advance(world_);

  EXPECT_NEAR(PressureAt(world_, kBoxCell), atmos::kStandardPascals, atmos::kStandardPascals / kPercent);
}

TEST_F(AtmosUnitTest, AUnitWithoutPowerLeavesTheRoomAlone) {
  Thin();
  const int64_t before = PressureAt(world_, kBoxCell);

  Advance(world_);

  EXPECT_EQ(PressureAt(world_, kBoxCell), before);
}

// A chamber between the station's left half and the vacuum outside: the right half of the box holds the controller
// and has the partition door on one side and the outer door on the other.
class AirlockTest : public AtmosSystemTest {
 protected:
  using Keycode = z13::fbs::input::Keycode;
  static constexpr Keycode kUseKey = Keycode::KEY_E;
  // Long enough to fall to the box's floor.
  static constexpr int kFallTicks = 120;
  static constexpr uint32_t kOpenTicks = 10;
  // Enough to turn to a door the player already stands at the height of.
  static constexpr int kLookTicks = 2;

  void SetUp() override {
    AtmosSystemTest::SetUp();
    world_.World().get_mut<z13::DoorTuning>().open_ticks = kOpenTicks;
    Build(BoxWithAnOuterDoor());
    testing::AddBlocks(world_, testing::DoorPartition());
    AddController();
    reactor_ = testing::AddBlock(world_, {.spec = {.type_id = testing::IdOf("Reactor"), .size = {4, 4, 4}},
                                          .cell = kReactorCell});
    world_.World().entity().add<z13::flecs_tools::StateEntity>().set(
        z13::conduit::Conduit {.kind = kCableKind, .cell = kCableCell, .axis = z13::conduit::ConduitAxis::kY, .length = 1});
    Advance(world_, kSettleTicks * 2);
    inner_ = DoorAtCell(kInnerDoorCell);
    outer_ = DoorAtCell(kOuterDoorCell);
    controller_ = FindBlock(world_, "AirlockController");
  }

  void AddController() {
    testing::AddBlock(world_, {.spec = {.type_id = testing::IdOf("AirlockController"), .size = {1, 1, 1}},
                               .cell = kControllerCell});
  }

  flecs::entity DoorAtCell(const Eigen::Vector3i& cell) {
    flecs::entity found;
    world_.World().query_builder<const Block>().build().each([&](flecs::entity block, const Block& placed) {
      if (placed.spec.type_id == testing::IdOf(testing::kDoor) && placed.cell == cell) {
        found = block;
      }
    });
    return found;
  }

  // Two cells before a door, looking at it.
  void StandBefore(const Eigen::Vector2f& before, int settle_ticks = kFallTicks) {
    const Eigen::Vector2f xy = (testing::kRoomCorner.head<2>().cast<float>() + before) * kCellSize;
    const float floor_top = static_cast<float>(testing::kRoomCorner.z() + 1) * kCellSize;
    world_.Player()
        .set(TransformAt({xy.x(), xy.y(), floor_top + z13::PhysicsTuning {}.eye_height}))
        .set(z13::gameplay::LookAngles {});
    Advance(world_, settle_ticks);
  }

  void Use() { z13::testing::Tap(world_, kUseKey, z13::testing::TickOnly(world_)); }

  static bool IsShut(flecs::entity door) {
    return door.get<DoorState>().position == 0 && !door.get<DoorState>().open_wanted;
  }

  // Ticks until `door` is wanted open; the `other` must stay shut all the while.
  bool RunUntilOpening(flecs::entity door, flecs::entity other) {
    for (int tick = 0; tick < kLongRunTicks; ++tick) {
      world_.Tick();
      EXPECT_TRUE(IsShut(other)) << "both doors of an airlock open at tick " << tick;
      if (door.get<DoorState>().open_wanted) {
        return true;
      }
    }
    return false;
  }

  // Whether an airlock holds the door shut, as opposed to the pressure interlock every door can have.
  static bool HeldByAirlock(flecs::entity door) {
    return door.has<DoorInterlock>() && door.get<DoorInterlock>().reason == InterlockReason::kAirlock;
  }

  flecs::entity inner_;
  flecs::entity outer_;
  flecs::entity controller_;
  flecs::entity reactor_;
};

TEST_F(AirlockTest, TheDoorsOfAPoweredAirlockAreHeldShutAndIgnoreABodyInTheirSensor) {
  ASSERT_TRUE(controller_.has<AirlockDoors>());
  EXPECT_TRUE(HeldByAirlock(inner_));
  EXPECT_TRUE(HeldByAirlock(outer_));

  StandBefore(kBeforeInnerDoor);
  Advance(world_, kOpenTicks * 2);

  EXPECT_TRUE(IsShut(inner_));
}

TEST_F(AirlockTest, CyclingOutPumpsTheChamberDownAndKeepsTheStationsAirAndTheInnerDoorShut) {
  StandBefore(kBeforeOuterDoor);
  const Air station = AirAt(world_, kLeftCell);
  const Air everything = Everything(world_);
  const Air chamber = AirAt(world_, kRightCell);

  Use();

  ASSERT_TRUE(RunUntilOpening(outer_, inner_));
  EXPECT_LE(PressureAt(world_, kRightCell), world_.World().get<z13::AtmosTuning>().interlock_pascals + kPascalsTolerance);
  EXPECT_EQ(AirAt(world_, kLeftCell), station);
  EXPECT_GE(Everything(world_).TotalMillimoles(), everything.TotalMillimoles() - chamber.TotalMillimoles());
}

TEST_F(AirlockTest, CyclingInFillsTheChamberFromTheStationAndLosesNothing) {
  SetAirAt(world_, kRightCell, Air {});
  StandBefore(kBeforeInnerDoor);
  const int64_t before = Everything(world_).TotalMillimoles();

  Use();

  ASSERT_TRUE(RunUntilOpening(inner_, outer_));
  const int64_t interlock = world_.World().get<z13::AtmosTuning>().interlock_pascals;
  EXPECT_NEAR(PressureAt(world_, kRightCell), PressureAt(world_, kLeftCell), interlock + kPascalsTolerance);
  EXPECT_EQ(Everything(world_).TotalMillimoles(), before);
}

TEST_F(AirlockTest, AChamberAlreadyLevelOpensTheDoorWithinAStepOrTwo) {
  StandBefore(kBeforeInnerDoor);
  Use();

  constexpr int kStepsToOpen = 3;
  for (uint32_t tick = 0; tick < kStepsToOpen * world_.World().get<z13::AtmosTuning>().step_ticks; ++tick) {
    world_.Tick();
  }

  EXPECT_TRUE(inner_.get<DoorState>().open_wanted);
  EXPECT_TRUE(IsShut(outer_));
}

TEST_F(AirlockTest, UsingTheOtherDoorMidCycleRedirectsTheCycle) {
  StandBefore(kBeforeOuterDoor);
  Use();
  Advance(world_, kSettleTicks);
  ASSERT_TRUE(IsShut(outer_));
  ASSERT_LT(PressureAt(world_, kRightCell), atmos::kStandardPascals) << "the chamber is being pumped down";

  StandBefore(kBeforeInnerDoor, kLookTicks);
  Use();

  EXPECT_TRUE(RunUntilOpening(inner_, outer_));
}

TEST_F(AirlockTest, WithoutPowerTheControllerHoldsNothingAndTheDoorsGoByHand) {
  z13::station::DestroyBlock(reactor_, reactor_.get<Block>());
  Advance(world_, kSettleTicks * 2);
  EXPECT_FALSE(HeldByAirlock(inner_));
  EXPECT_FALSE(HeldByAirlock(outer_));

  StandBefore(kBeforeInnerDoor);
  Use();
  Advance(world_, kOpenTicks);

  EXPECT_TRUE(inner_.get<DoorState>().open_wanted);
}

TEST_F(AtmosSystemTest, AControllerWithOnlyOneDoorOnItsChamberHoldsNothing) {
  Build(testing::SealedBox());
  testing::AddBlocks(world_, testing::DoorPartition());
  testing::AddBlock(world_, {.spec = {.type_id = testing::IdOf("AirlockController"), .size = {1, 1, 1}},
                             .cell = kControllerCell});
  Advance(world_);

  EXPECT_FALSE(FindBlock(world_, "AirlockController").has<AirlockDoors>());
  EXPECT_FALSE(FindBlock(world_, testing::kDoor).has<DoorInterlock>()) << "equal pressures, and no airlock";
}

TEST_F(AtmosSystemTest, ThePressureLayerFillsOnlyWhileItIsShown) {
  Build(testing::SealedBox());
  auto& layers = world_.World().get_mut<z13::station::OverlayLayers>();
  ASSERT_NE(layers.Find("pressure"), nullptr);
  Advance(world_);
  EXPECT_TRUE(layers.Find("pressure")->boxes.empty());

  layers.shown = "pressure";
  Advance(world_);
  const z13::station::OverlayLayer& layer = *layers.Find("pressure");
  EXPECT_EQ(layer.boxes.size(), 1u);
  EXPECT_NE(layer.label.find("kPa"), std::string::npos) << layer.label;

  layers.shown.reset();
  Advance(world_);
  EXPECT_TRUE(layers.Find("pressure")->boxes.empty());
}

TEST_F(AtmosSystemTest, TheWorldCarriesTheAtmosModuleAndItsBlocks) {
  const auto modules = world_.World().get<z13::utility::UtilityModules>().Sorted();

  EXPECT_TRUE(std::ranges::contains(modules, "atmos", &z13::utility::UtilityModule::name));
  EXPECT_TRUE(world_.World().get<z13::building::primitives::BlockPalette>().palette.Find("AtmosUnit").has_value());
  EXPECT_TRUE(world_.World().get<z13::building::primitives::BlockPalette>().palette.Find("Vent").has_value());
}

}  // namespace
}  // namespace z13::atmos_module
