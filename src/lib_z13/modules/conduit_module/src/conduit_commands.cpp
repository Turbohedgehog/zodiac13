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


#include "conduit_commands.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <rfl/enums.hpp>

#include <lib_core/commands/command_registry.h>
#include <lib_core/state/state_markers.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/flecs_utils.h>
#include <z13/components/station/conduit.h>
#include <z13/components/station/state_changes.h>
#include <z13/components/station/station_mode.h>
#include <primitives/palette.h>

#include "conduit_index.h"
#include "conduit_run.h"

namespace z13::conduit {

namespace {

constexpr int kDefaultConduitsLimit = 200;

// Names the commands' runs apart from the players'.
struct McpConduitCounter {
  using Singleton = void;
  uint32_t made {};
};

struct CellInfo {
  int x {};
  int y {};
  int z {};
};

struct KindInfo {
  uint32_t id {};
  std::string name;
  std::string network;
};

struct ConduitInfo {
  std::string name;
  std::string kind;
  std::string network;
  CellInfo cell;
  std::string axis;
  int length {};
};

struct ConduitKindsCommand {
  using Command = z13::Command<std::vector<KindInfo>,
                               "Lists the conduit kinds: id, name and the network each carries (a cell holds one "
                               "conduit of each network).",
                               Tags<kDebugTag>>;
  int unused {};
};

struct ConduitsCommand {
  using Command = z13::Command<std::vector<ConduitInfo>,
                               "Lists the main grid's conduit runs that touch the box from cell (x, y, z) of size_x * "
                               "size_y * size_z cells, at most `limit`.",
                               Tags<kDebugTag>>;
  int x {};
  int y {};
  int z {};
  int size_x = 1;
  int size_y = 1;
  int size_z = 1;
  int limit = kDefaultConduitsLimit;
};

struct LayConduitCommand {
  using Command = z13::Command<
      std::string,
      "Lays a run of the conduit `kind` (a name from `conduit_kinds`) from cell (x, y, z) towards (to_x, to_y, to_z) "
      "along the axis they are furthest apart on, both ends included. Refused where the run is too long or a conduit "
      "of that network is in one of its cells already. Station mode only. Answers the run's name.",
      Tags<kDebugTag, kBuildTag>>;
  std::string kind;
  int x {};
  int y {};
  int z {};
  int to_x {};
  int to_y {};
  int to_z {};
};

struct RemoveConduitCommand {
  using Command = z13::Command<std::string,
                               "Removes the whole run of the conduit `kind` that passes through cell (x, y, z) and "
                               "answers its name. Station mode only.",
                               Tags<kDebugTag, kBuildTag>>;
  std::string kind;
  int x {};
  int y {};
  int z {};
};

CellInfo CellInfoOf(const Eigen::Vector3i& value) {
  return {.x = value.x(), .y = value.y(), .z = value.z()};
}

std::expected<void, std::string> CheckStationMode(flecs::world world) {
  if (!world.has<z13::station::StationMode>()) {
    return std::unexpected(std::string {"not in station mode: start a station scene first"});
  }
  return {};
}

std::expected<ConduitKind, std::string> KindNamed(flecs::world world, const std::string& name) {
  const auto& kinds = world.get<ConduitKinds>().kinds;
  const auto found = std::ranges::find(kinds, name, &ConduitKind::name);
  if (found == kinds.end()) {
    return std::unexpected(std::format("unknown conduit kind '{}': see `conduit_kinds`", name));
  }
  return *found;
}

std::string NetworkOf(const ConduitKinds& kinds, uint32_t kind) {
  const auto found = kinds.Find(kind);
  return found ? found->network : std::string {};
}

ConduitIndex IndexOfMainGrid(flecs::world world) {
  ConduitIndex index;
  const auto& kinds = world.get<ConduitKinds>();
  world.each([&](flecs::entity entity, const Conduit& conduit) {
    if (conduit.grid == z13::station::kMainGrid) {
      index.Insert(entity.id(), NetworkOf(kinds, conduit.kind), conduit);
    }
  });
  return index;
}

void ListKinds(flecs::world world, ReplyOf<ConduitKindsCommand>& reply) {
  std::vector<KindInfo> listed;
  for (const ConduitKind& kind : world.get<ConduitKinds>().kinds) {
    listed.push_back({.id = kind.id, .name = kind.name, .network = kind.network});
  }
  reply.Succeed(std::move(listed));
}

void ListConduits(flecs::world world, const ConduitsCommand& request, ReplyOf<ConduitsCommand>& reply) {
  const auto& kinds = world.get<ConduitKinds>();
  const Eigen::Vector3i min {request.x, request.y, request.z};
  const Eigen::Vector3i end = min + Eigen::Vector3i {request.size_x, request.size_y, request.size_z};
  std::vector<ConduitInfo> listed;
  world.each([&](flecs::entity entity, const Conduit& conduit) {
    if (conduit.grid != z13::station::kMainGrid) {
      return;
    }
    const auto cells = CellsOf(conduit);
    const bool touches = std::ranges::any_of(cells, [&](const Eigen::Vector3i& cell) {
      return (cell.array() >= min.array()).all() && (cell.array() < end.array()).all();
    });
    if (!touches) {
      return;
    }
    const auto kind = kinds.Find(conduit.kind);
    listed.push_back({.name = std::string(entity.name().c_str()),
                      .kind = kind ? kind->name : std::to_string(conduit.kind),
                      .network = kind ? kind->network : std::string {},
                      .cell = CellInfoOf(conduit.cell),
                      .axis = std::string(rfl::enum_to_string(conduit.axis)),
                      .length = conduit.length});
  });
  std::ranges::sort(listed, {}, &ConduitInfo::name);
  listed.resize(std::min(listed.size(), static_cast<size_t>(std::max(request.limit, 0))));
  reply.Succeed(std::move(listed));
}

void Lay(flecs::world world, const LayConduitCommand& request, ReplyOf<LayConduitCommand>& reply) {
  if (const auto mode = CheckStationMode(world); !mode) {
    reply.Fail(mode.error());
    return;
  }
  const auto kind = KindNamed(world, request.kind);
  if (!kind) {
    reply.Fail(kind.error());
    return;
  }
  const Conduit run = RunBetween(kind->id, {request.x, request.y, request.z}, {request.to_x, request.to_y, request.to_z});
  const int max_length = world.get<z13::building::primitives::BlockPalette>().palette.max_size_cells;
  if (run.length > max_length) {
    reply.Fail(std::format("a run is at most {} cells, this one {}", max_length, run.length));
    return;
  }
  if (IndexOfMainGrid(world).Overlaps(kind->network, run)) {
    reply.Fail(std::format("a {} conduit is in one of those cells already", kind->network));
    return;
  }
  const z13::ImmediateScope immediate(world);
  const std::string name = std::format("McpConduit_{}", world.get_mut<McpConduitCounter>().made++);
  world.entity(name.c_str()).add<z13::flecs_tools::StateEntity>().set(run);
  reply.Succeed(name);
}

void Remove(flecs::world world, const RemoveConduitCommand& request, ReplyOf<RemoveConduitCommand>& reply) {
  if (const auto mode = CheckStationMode(world); !mode) {
    reply.Fail(mode.error());
    return;
  }
  const auto kind = KindNamed(world, request.kind);
  if (!kind) {
    reply.Fail(kind.error());
    return;
  }
  const ConduitIndex index = IndexOfMainGrid(world);
  const Eigen::Vector3i cell {request.x, request.y, request.z};
  const auto found = std::ranges::find(index.At(cell), kind->network, &ConduitInCell::network);
  if (found == index.At(cell).end()) {
    reply.Fail(std::format("no {} conduit in that cell", kind->network));
    return;
  }
  const flecs::entity target = world.entity(found->id);
  const std::string name = std::string(target.name().c_str());
  const z13::ImmediateScope immediate(world);
  target.destruct();
  reply.Succeed(name);
}

}  // namespace

void RegisterConduitCommandComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<McpConduitCounter, ConduitKindsCommand, ConduitsCommand, LayConduitCommand,
                                       RemoveConduitCommand>(world);
}

void RegisterConduitCommandSystems(flecs::world world) {
  world.set<McpConduitCounter>({});
  const auto edits = [](auto& builder) {
    builder.immediate();  // an entity is made or destroyed here
    builder.template write<Conduit>();
  };
  CommandSystem<ConduitKindsCommand>(
      world, "ConduitCommands::Kinds",
      [](flecs::iter& it, size_t, const ConduitKindsCommand&, ReplyOf<ConduitKindsCommand>& reply) {
        ListKinds(it.world(), reply);
      });
  CommandSystem<ConduitsCommand>(world, "ConduitCommands::List",
                                 [](flecs::iter& it, size_t, const ConduitsCommand& request,
                                    ReplyOf<ConduitsCommand>& reply) { ListConduits(it.world(), request, reply); });
  CommandSystem<LayConduitCommand>(world, "ConduitCommands::Lay", edits,
                                   [](flecs::iter& it, size_t, const LayConduitCommand& request,
                                      ReplyOf<LayConduitCommand>& reply) { Lay(it.world(), request, reply); });
  CommandSystem<RemoveConduitCommand>(
      world, "ConduitCommands::Remove", edits,
      [](flecs::iter& it, size_t, const RemoveConduitCommand& request, ReplyOf<RemoveConduitCommand>& reply) {
        Remove(it.world(), request, reply);
      });
}

}  // namespace z13::conduit
