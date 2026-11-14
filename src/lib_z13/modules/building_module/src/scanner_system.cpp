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

#include "scanner_system.h"

#include <algorithm>
#include <format>
#include <limits>
#include <optional>
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>

#include <Eigen/Dense>

#include <lib_core/state/state_markers.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/math.h>
#include <lib_core/world/lifecycle.h>
#include <primitives/palette.h>
#include <z13/components/gameplay/inventory.h>
#include <z13/components/gameplay/player.h>
#include <z13/components/gameplay/scan.h>
#include <z13/components/input/input_phases.h>
#include <z13/components/station/block.h>
#include <z13/components/station/building.h>
#include <z13/components/station/conduit.h>
#include <z13/components/station/door.h>
#include <z13/components/station/spawn_stamp.h>
#include <z13/components/station/station_mode.h>
#include <z13/components/utility/utilities.h>
#include <z13_settings/building_tuning.h>

#include "build_grids.h"

namespace z13::building {

namespace {

using z13::gameplay::Player;
using z13::inventory::ActiveItem;
using z13::inventory::ScanReport;
using z13::inventory::ScannerTool;
using z13::station::Block;
using z13::station::DoorMode;
using z13::station::DoorState;

constexpr std::string_view kItemPrefix = "Item";
constexpr std::string_view kCaption = "Scanner";
// The index of the scanner among the items one player is given in one tick (the build and conduit tools come first).
constexpr uint32_t kScannerIndex = 2;

bool HoldsScanner(flecs::entity player) {
  const auto* active = player.try_get<ActiveItem>();
  if (active == nullptr) {
    return false;
  }
  const flecs::entity item = player.world().entity(active->item);
  return item.is_alive() && item.has<ScannerTool>();
}

void GiveScanner(flecs::entity player, const Player& owner) {
  flecs::world world = player.world();
  const z13::gameplay::SpawnStamp stamp {
      .tick = world.get<z13::flecs_tools::SimulationClock>().tick, .player_id = owner.id};
  world.entity(z13::gameplay::SpawnName(kItemPrefix, stamp, kScannerIndex).c_str())
      .add<z13::flecs_tools::StateEntity>()
      .set(z13::inventory::Item {.kind = z13::inventory::ItemKind::kScanner})
      .set(z13::inventory::CarriedBy {.player_id = owner.id, .cell = kScannerCell})
      .add<ScannerTool>();
  player.add<ScannerGiven>();
}

// What the hotbar shows on the scanner; derived, so a restored one gets it too.
void DeriveCaption(flecs::entity item) {
  if (!item.has<z13::inventory::ItemCaption>()) {
    item.set(z13::inventory::ItemCaption {.text = std::string(kCaption)});
  }
}

std::string_view ModeName(DoorMode mode) {
  switch (mode) {
    case DoorMode::kAuto:
      return "automatic";
    case DoorMode::kManual:
      return "manual";
    case DoorMode::kLocked:
      return "locked";
    case DoorMode::kWelded:
      return "welded";
  }
  return {};
}

std::string_view DoorPhase(const DoorState& state) {
  if (state.position == 0) {
    return "shut";
  }
  return state.position == z13::station::kDoorOpen ? "open" : "moving";
}

std::vector<std::string> Describe(flecs::entity target, const GridView& grid,
                                  const z13::building::primitives::Palette& palette) {
  const Block& block = target.get<Block>();
  const auto primitive = palette.Find(block.spec.type_id);
  std::vector<std::string> lines;
  lines.push_back(primitive ? primitive->get().name : std::string("Block"));
  lines.push_back(std::format("cell ({}, {}, {}), grid {}", block.cell.x(), block.cell.y(), block.cell.z(), grid.id));
  if (const auto* door = target.try_get<DoorState>()) {
    lines.push_back(std::format("door: {}, {}", DoorPhase(*door), ModeName(door->mode)));
  }
  if (const auto* demand = target.try_get<z13::utility::UtilityDemand>()) {
    lines.push_back(std::format("{}: needs {}, {}", demand->network, demand->amount,
                                z13::utility::IsSupplied(target) ? "supplied" : "NOT supplied"));
  }
  return lines;
}

struct ConduitHit {
  flecs::entity run;
  float distance_m {};
};

// A conduit is thinner than a cell and aiming at it is hard, so its aim box is this share of a cell across.
constexpr float kMinConduitAimCells = 0.4f;

// The ray in one grid's cells, worked out once per grid, not per run.
struct CellRay {
  Eigen::Vector3f from = Eigen::Vector3f::Zero();
  Eigen::Vector3f direction = Eigen::Vector3f::Zero();
  float length_m {};
};

// Slab test of the ray, in the grid's cells, against the run's box; the distance in meters, along the segment.
std::optional<float> RayDistanceToRun(const z13::conduit::Conduit& run, float aim_cells, const CellRay& ray) {
  const Eigen::Vector3f& from = ray.from;
  const Eigen::Vector3f& direction = ray.direction;
  const float half = aim_cells / 2.f;
  const Eigen::Vector3f centre = run.cell.cast<float>() + Eigen::Vector3f::Constant(0.5f);
  Eigen::Vector3f low = centre - Eigen::Vector3f::Constant(half);
  Eigen::Vector3f high = centre + Eigen::Vector3f::Constant(half);
  const int axis = static_cast<int>(run.axis);
  low[axis] = static_cast<float>(run.cell[axis]);
  high[axis] = static_cast<float>(run.cell[axis] + run.length);
  float enter = 0.f;
  float leave = 1.f;
  for (int i = 0; i < 3; ++i) {
    if (std::abs(direction[i]) < z13::math::kEpsilon) {
      if (from[i] < low[i] || from[i] > high[i]) {
        return std::nullopt;
      }
      continue;
    }
    float t0 = (low[i] - from[i]) / direction[i];
    float t1 = (high[i] - from[i]) / direction[i];
    if (t0 > t1) {
      std::swap(t0, t1);
    }
    enter = std::max(enter, t0);
    leave = std::min(leave, t1);
    if (enter > leave) {
      return std::nullopt;
    }
  }
  return enter * ray.length_m;
}

using ConduitQuery = flecs::query<const z13::conduit::Conduit>;

std::optional<ConduitHit> NearestConduit(const ConduitQuery& runs, const BuildGrids& grids,
                                         const Eigen::Vector3f& from_m,
                                         const Eigen::Vector3f& to_m) {
  std::optional<ConduitHit> nearest;
  std::unordered_map<z13::station::GridId, CellRay> rays;
  runs.each([&](flecs::entity run, const z13::conduit::Conduit& conduit) {
    auto ray = rays.find(conduit.grid);
    if (ray == rays.end()) {
      const auto grid = grids.Find(conduit.grid);
      CellRay made;
      if (grid) {
        made = {.from = grid->get().ToCells(from_m),
                .direction = grid->get().ToCells(to_m) - grid->get().ToCells(from_m),
                .length_m = (to_m - from_m).norm()};
      }
      ray = rays.emplace(conduit.grid, made).first;
    }
    const auto distance = RayDistanceToRun(conduit, kMinConduitAimCells, ray->second);
    if (distance && (!nearest || *distance < nearest->distance_m)) {
      nearest = ConduitHit {.run = run, .distance_m = *distance};
    }
  });
  return nearest;
}

std::vector<std::string> DescribeConduit(const ConduitHit& hit, const z13::conduit::ConduitKinds& kinds) {
  const auto& conduit = hit.run.get<z13::conduit::Conduit>();
  const auto kind = kinds.Find(conduit.kind);
  return {kind ? kind->name : std::string("Conduit"),
          std::format("{} network, {} cells from ({}, {}, {}), grid {}", kind ? kind->network : std::string("?"),
                      conduit.length, conduit.cell.x(), conduit.cell.y(), conduit.cell.z(), conduit.grid)};
}

void Scan(flecs::entity player, const Eigen::Matrix4f& transform, const BuildGrids& grids,
          const z13::building::primitives::Palette& palette, const z13::conduit::ConduitKinds& kinds,
          const ConduitQuery& runs, float reach_m) {
  if (!HoldsScanner(player)) {
    player.remove<ScanReport>();
    return;
  }
  const Eigen::Vector3f eye = z13::math::ExtractTranslation<float>(transform);
  const Eigen::Vector3f forward = transform.block<3, 3>(0, 0).col(0);
  ScanReport report;
  const Eigen::Vector3f end = eye + forward * reach_m;
  const auto hit = grids.Raycast(eye, end);
  const float block_distance =
      hit ? (hit->grid.get().ToWorldMeters(hit->hit.point) - eye).norm() : std::numeric_limits<float>::max();
  const auto conduit = NearestConduit(runs, grids, eye, end);
  if (conduit && conduit->distance_m < block_distance) {
    report.lines = DescribeConduit(*conduit, kinds);
  } else if (hit) {
    const flecs::entity target = player.world().entity(hit->hit.block);
    if (target.is_alive() && target.has<Block>()) {
      report.lines = Describe(target, hit->grid, palette);
    }
  }
  player.set(std::move(report));
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<ScannerTool, ScannerGiven>(world);
}

void RegisterSystems(flecs::world world) {
  world.system<const Player>("ScannerSystem::GiveScanner")
      .kind<z13::inventory::InventoryPhase>()
      .with<z13::station::StationMode>()
      .without<ScannerGiven>()
      .order_by<Player>(z13::gameplay::CompareByPlayerId)
      .write<z13::inventory::Item>()
      .write<z13::inventory::CarriedBy>()
      .write<ScannerTool>()
      .write<ScannerGiven>()
      .each(GiveScanner);

  world.system("ScannerSystem::DeriveCaption")
      .with<ScannerTool>()
      .kind<z13::input::ClearActionFramePhase>()
      .write<z13::inventory::ItemCaption>()
      .each(DeriveCaption);

  const ConduitQuery runs = world.query<const z13::conduit::Conduit>();
  const auto players = world.query_builder<const Eigen::Matrix4f>("ScannerSystem::Players").with<Player>().build();
  world.system<const z13::BuildingTuning, const z13::building::primitives::BlockPalette, const z13::conduit::ConduitKinds>(
          "ScannerSystem::Scan")
      .kind<z13::building::UpdateBuildingToolPhase>()
      .with<z13::station::StationMode>()
      .read<z13::building::grid::BlockIndex>()
      .read<ActiveItem>()
      .write<ScanReport>()
      .each([players, runs](flecs::iter& it, size_t, const z13::BuildingTuning& tuning,
                      const z13::building::primitives::BlockPalette& palette,
                      const z13::conduit::ConduitKinds& kinds) {
        const BuildGrids grids = BuildGrids::Of(it.world());
        if (grids.Empty()) {
          return;
        }
        players.each([&](flecs::entity player, const Eigen::Matrix4f& transform) {
          Scan(player, transform, grids, palette.palette, kinds, runs, tuning.destroy_reach_distance);
        });
      });
}

}  // namespace

void ScannerSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::building
