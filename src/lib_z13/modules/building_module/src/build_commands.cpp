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


#include "build_commands.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Dense>
#include <rfl/enums.hpp>

#include <lib_core/commands/command_registry.h>
#include <lib_core/state/world_state.h>
#include <lib_core/utils/flecs_utils.h>
#include <z13/components/gameplay/player_parts.h>
#include <z13/components/station/block.h>
#include <z13/components/station/block_changes.h>
#include <z13/components/station/grid.h>
#include <z13/components/station/spawn_point.h>
#include <z13/components/station/station_mode.h>
#include <z13/components/station/wall.h>
#include <grid/block_index.h>
#include <primitives/occupancy.h>
#include <primitives/palette.h>
#include <z13_settings/building_tuning.h>

#include "block_entities.h"
#include "build_validation.h"

namespace z13::building {

namespace {

using z13::building::primitives::BlockPalette;
using z13::building::primitives::CellBox;
using z13::station::Block;
using z13::station::Orientation;

constexpr int kDefaultBlocksLimit = 200;

// The blocks of commands are named apart from the players': no tick or player stamps them.
struct McpBlockCounter {
  using Singleton = void;
  uint32_t made {};
};

struct CellInfo {
  int x {};
  int y {};
  int z {};
};

struct PrimitiveInfo {
  uint32_t id {};
  std::string name;
  std::string shape;
  CellInfo min_size;
  CellInfo max_size;
};

struct BlockInfo {
  std::string name;
  std::string primitive;
  CellInfo cell;
  CellInfo size;
  std::string orientation;
};

struct PaletteCommand {
  using Command = z13::Command<std::vector<PrimitiveInfo>,
                               "Lists the block types: id, name, shape and the size limits in cells (a cell is 0.25 m).",
                               Tags<kDebugTag>>;
  int unused {};
};

struct BlocksCommand {
  using Command = z13::Command<std::vector<BlockInfo>,
                               "Lists the blocks of `grid` (the main grid without it; ids from `grids`) that touch the "
                               "box from cell (x, y, z) of size_x * size_y * size_z cells of that grid, at most `limit`. "
                               "X and Y run along the floor, Z up.",
                               Tags<kDebugTag>>;
  std::optional<uint32_t> grid;
  int x {};
  int y {};
  int z {};
  int size_x = 1;
  int size_y = 1;
  int size_z = 1;
  int limit = kDefaultBlocksLimit;
};

struct GridInfo {
  uint32_t id {};
  CellInfo origin;
  double yaw_degrees {};
  double pitch_degrees {};
  size_t blocks {};
};

struct GridsCommand {
  using Command = z13::Command<std::vector<GridInfo>,
                               "Lists the grids: id, where the cell (0, 0, 0) stands in the world's cells, the turn "
                               "about Z and the tilt in degrees (a grid's cells are in its own axes), its block count.",
                               Tags<kDebugTag>>;
  int unused {};
};

struct BuildBlockCommand {
  using Command = z13::Command<
      std::string,
      "Builds one block of the type `primitive` (a name from `palette`) with its lowest corner at cell (x, y, z). "
      "size_x/y/z are in cells, 0 takes the type's smallest. `orientation` is an enumerator of "
      "z13::station::Orientation (empty is the default, unturned). Station mode only; refused where it would break "
      "the world (occupied cells, a player inside, a size outside the limits). Answers the new block's name.",
      Tags<kDebugTag, kBuildTag>>;
  std::string primitive;
  int x {};
  int y {};
  int z {};
  int size_x {};
  int size_y {};
  int size_z {};
  std::string orientation;
};

struct DestroyBlockCommand {
  using Command = z13::Command<std::string,
                               "Destroys the block occupying cell (x, y, z) of the main grid and answers its name. Not "
                               "the last spawn point, nor a wall's panel. Station mode only.",
                               Tags<kDebugTag, kBuildTag>>;
  int x {};
  int y {};
  int z {};
};

CellInfo CellInfoOf(const Eigen::Vector3i& value) {
  return {.x = value.x(), .y = value.y(), .z = value.z()};
}

std::expected<flecs::entity, std::string> EditableGrid(flecs::world world) {
  if (!world.has<z13::station::StationMode>()) {
    return std::unexpected(std::string {"not in station mode: start a station scene first"});
  }
  const flecs::entity grid = z13::station::MainGridEntity(world);
  if (!grid.is_valid() || !grid.has<grid::BlockIndex>()) {
    return std::unexpected(std::string {"the station has no main grid yet"});
  }
  return grid;
}

BlockInfo InfoOf(flecs::entity entity, const Block& block, const primitives::Palette& palette) {
  const auto primitive = palette.Find(block.spec.type_id);
  return {.name = std::string(entity.name().c_str()),
          .primitive = primitive ? primitive->get().name : std::to_string(block.spec.type_id),
          .cell = CellInfoOf(block.cell),
          .size = CellInfoOf(block.spec.size),
          .orientation = std::string(rfl::enum_to_string(block.spec.orientation))};
}

std::vector<PlayerSphere> PlayerSpheresOf(flecs::world world) {
  std::vector<PlayerSphere> spheres;
  world.query<const z13::gameplay::PlayerCollider, const Eigen::Matrix4f>().each(
      [&spheres](const z13::gameplay::PlayerCollider& collider, const Eigen::Matrix4f& transform) {
        spheres.push_back({.center = transform.block<3, 1>(0, 3), .radius = collider.radius});
      });
  return spheres;
}

std::vector<CellBox> ClearancesOf(flecs::world world, const BuildingTuning& tuning) {
  std::vector<CellBox> clearances;
  world.query_builder<const Block>().with<z13::station::SpawnPoint>().build().each([&](const Block& marker) {
    if (marker.grid == z13::station::kMainGrid) {
      clearances.push_back(SpawnClearance(marker, tuning));
    }
  });
  return clearances;
}

void ListPalette(flecs::world world, ReplyOf<PaletteCommand>& reply) {
  std::vector<PrimitiveInfo> listed;
  for (const auto& primitive : world.get<BlockPalette>().palette.primitives) {
    listed.push_back({.id = primitive.id,
                      .name = primitive.name,
                      .shape = std::string(rfl::enum_to_string(primitive.shape.kind)),
                      .min_size = CellInfoOf(primitive.min_size),
                      .max_size = CellInfoOf(primitive.max_size)});
  }
  reply.Succeed(std::move(listed));
}

std::optional<flecs::entity> GridById(flecs::world world, uint32_t id) {
  std::optional<flecs::entity> found;
  world.query<const z13::station::Grid>().each([&](flecs::entity entity, const z13::station::Grid& grid) {
    if (grid.id == id) {
      found = entity;
    }
  });
  return found;
}

void ListGrids(flecs::world world, ReplyOf<GridsCommand>& reply) {
  std::vector<GridInfo> listed;
  world.query<const z13::station::Grid>().each([&](flecs::entity entity, const z13::station::Grid& grid) {
    listed.push_back({.id = grid.id,
                      .origin = CellInfoOf(grid.origin),
                      .yaw_degrees = grid.yaw_degrees,
                      .pitch_degrees = grid.pitch_degrees,
                      .blocks = entity.has<grid::BlockIndex>() ? entity.get<grid::BlockIndex>().Size() : 0});
  });
  std::ranges::sort(listed, {}, &GridInfo::id);
  reply.Succeed(std::move(listed));
}

void ListBlocks(flecs::world world, const BlocksCommand& request, ReplyOf<BlocksCommand>& reply) {
  auto grid = EditableGrid(world);
  if (grid && request.grid) {
    const auto chosen = GridById(world, *request.grid);
    if (!chosen || !chosen->has<grid::BlockIndex>()) {
      reply.Fail(std::format("no grid {}", *request.grid));
      return;
    }
    grid = *chosen;
  }
  if (!grid) {
    reply.Fail(grid.error());
    return;
  }
  const CellBox box {.min = {request.x, request.y, request.z}, .extent = {request.size_x, request.size_y, request.size_z}};
  const auto& palette = world.get<BlockPalette>().palette;
  std::vector<BlockInfo> listed;
  for (const grid::BlockId id : grid->get<grid::BlockIndex>().Overlapping(box)) {
    const flecs::entity entity = world.entity(id);
    listed.push_back(InfoOf(entity, entity.get<Block>(), palette));
  }
  std::ranges::sort(listed, {}, &BlockInfo::name);
  if (static_cast<int>(listed.size()) > request.limit) {
    listed.resize(static_cast<size_t>(std::max(request.limit, 0)));
  }
  reply.Succeed(std::move(listed));
}

void BuildBlock(flecs::world world, const BuildBlockCommand& request, ReplyOf<BuildBlockCommand>& reply) {
  const auto grid = EditableGrid(world);
  if (!grid) {
    reply.Fail(grid.error());
    return;
  }
  const auto& palette = world.get<BlockPalette>().palette;
  const auto primitive = palette.Find(request.primitive);
  if (!primitive) {
    reply.Fail(std::format("unknown primitive '{}': see `palette`", request.primitive));
    return;
  }
  Orientation orientation {};
  if (!request.orientation.empty()) {
    const auto parsed = rfl::string_to_enum<Orientation>(request.orientation);
    if (!parsed) {
      reply.Fail(std::format("unknown orientation '{}'", request.orientation));
      return;
    }
    orientation = *parsed;
  }
  const Eigen::Vector3i asked {request.size_x, request.size_y, request.size_z};
  const Eigen::Vector3i size = (asked.array() == 0).select(primitive->get().min_size, asked);
  const Block block {.spec = {.type_id = primitive->get().id, .size = size, .orientation = orientation},
                     .cell = {request.x, request.y, request.z}};

  const auto& tuning = world.get<BuildingTuning>();
  const std::vector<PlayerSphere> players = PlayerSpheresOf(world);
  const std::vector<CellBox> clearances = ClearancesOf(world, tuning);
  const BuildView view {.palette = palette,
                        .index = grid->get<grid::BlockIndex>(),
                        .tuning = tuning,
                        .players = players,
                        .spawn_clearances = clearances};
  if (const Status valid = ValidateBuild(block, view); !valid) {
    reply.Fail(valid.error());
    return;
  }

  const z13::ImmediateScope immediate(world);
  const std::string name = std::format("McpBlock_{}", world.get_mut<McpBlockCounter>().made++);
  const flecs::entity created = CreateBlock(world, name, block, palette, tuning);
  grid->get_mut<grid::BlockIndex>().Insert(created.id(), primitives::ShapeOfBlock(block, palette));
  reply.Succeed(name);
}

void DestroyBlockAt(flecs::world world, const DestroyBlockCommand& request, ReplyOf<DestroyBlockCommand>& reply) {
  const auto grid = EditableGrid(world);
  if (!grid) {
    reply.Fail(grid.error());
    return;
  }
  const auto found = grid->get<grid::BlockIndex>().At({request.x, request.y, request.z});
  if (!found) {
    reply.Fail("no block in that cell");
    return;
  }
  const flecs::entity target = world.entity(*found);
  const Block block = target.get<Block>();
  if (target.has<z13::station::WallPanelOf>()) {
    reply.Fail("that block is a wall's panel: remove the wall instead");
    return;
  }
  if (const Status valid =
          ValidateDestroy(block, world.get<BlockPalette>().palette, world.count<z13::station::SpawnPoint>());
      !valid) {
    reply.Fail(valid.error());
    return;
  }
  const z13::ImmediateScope immediate(world);
  const std::string name = std::string(target.name().c_str());
  grid->get_mut<grid::BlockIndex>().Erase(*found);
  z13::station::DestroyBlock(target, block);
  reply.Succeed(name);
}

}  // namespace

void RegisterBuildCommandComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<McpBlockCounter, PaletteCommand, BlocksCommand, GridsCommand,
                                       BuildBlockCommand, DestroyBlockCommand>(world);
}

void RegisterBuildCommandSystems(flecs::world world) {
  world.set<McpBlockCounter>({});
  const auto edits = [](auto& builder) {
    builder.immediate();  // the world is read-only inside an ordinary system, and an entity is made here
    builder.template write<Block>().template write<z13::station::BlockChange>().template write<
        z13::station::SpawnPoint>();
  };
  CommandSystem<PaletteCommand>(world, "BuildCommands::Palette",
                                [](flecs::iter& it, size_t, const PaletteCommand&, ReplyOf<PaletteCommand>& reply) {
                                  ListPalette(it.world(), reply);
                                });
  CommandSystem<BlocksCommand>(world, "BuildCommands::Blocks",
                               [](flecs::iter& it, size_t, const BlocksCommand& request,
                                  ReplyOf<BlocksCommand>& reply) { ListBlocks(it.world(), request, reply); });
  CommandSystem<GridsCommand>(world, "BuildCommands::Grids",
                              [](flecs::iter& it, size_t, const GridsCommand&, ReplyOf<GridsCommand>& reply) {
                                ListGrids(it.world(), reply);
                              });
  CommandSystem<BuildBlockCommand>(world, "BuildCommands::Build", edits,
                                   [](flecs::iter& it, size_t, const BuildBlockCommand& request,
                                      ReplyOf<BuildBlockCommand>& reply) { BuildBlock(it.world(), request, reply); });
  CommandSystem<DestroyBlockCommand>(
      world, "BuildCommands::Destroy", edits,
      [](flecs::iter& it, size_t, const DestroyBlockCommand& request, ReplyOf<DestroyBlockCommand>& reply) {
        DestroyBlockAt(it.world(), request, reply);
      });
}

}  // namespace z13::building
