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

#include "ghost_players.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <lib_core/state/rollback.h>
#include <lib_core/state/world_state.h>
#include <lib_core/time/simulation_clock.h>
#include <lib_core/utils/math.h>
#include <lib_core/world/lifecycle.h>

#include <z13/components/ghost.h>
#include <z13/components/player.h>
#include <z13_module/gameplay/gameplay_entities.h>
#include <z13_settings/net_tuning.h>

#include "pose_packet.h"

namespace z13::net {

namespace {

namespace ft = z13::flecs_tools;

// Pose intervals a ghost outlives its last pose by, so a few lost packets don't make it blink.
constexpr uint64_t kGhostLifeIntervals = 3;

Eigen::Matrix4f TransformOf(const GhostPose& pose) {
  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  transform.block<3, 3>(0, 0) =
      (Eigen::AngleAxisf(z13::math::ToRadians(pose.look.yaw_deg), Eigen::Vector3f::UnitZ()) *
       Eigen::AngleAxisf(z13::math::ToRadians(pose.look.pitch_deg), Eigen::Vector3f::UnitY()))
          .toRotationMatrix();
  transform.block<3, 1>(0, 3) = pose.position;
  return transform;
}

std::string GhostName(uint32_t player_id) {
  return std::format("ghost_{}", player_id);
}

bool IsSimulated(flecs::world world, uint32_t player_id) {
  return world.lookup(z13::gameplay::PlayerEntityName(player_id).c_str());
}

void SyncGhosts(
    flecs::iter& it, size_t, GhostPlayers& ghosts, const ft::SimulationClock& clock, const z13::NetTuning& tuning) {
  flecs::world world = it.world();
  const uint64_t life = tuning.ghost_pose_interval_ticks * tuning.ghost_far_every * kGhostLifeIntervals;
  std::erase_if(ghosts.by_player, [&](const auto& entry) {
    return entry.second.tick + life < clock.tick || IsSimulated(world, entry.first);
  });
  for (const auto& [player_id, pose] : ghosts.by_player) {
    const std::string name = GhostName(player_id);
    world.entity(name.c_str()).set(z13::gameplay::GhostPlayer {.id = player_id}).set(TransformOf(pose));
  }
  std::vector<flecs::entity> gone;
  world.query_builder<const z13::gameplay::GhostPlayer>().build().each(
      [&](flecs::entity ghost, const z13::gameplay::GhostPlayer& marker) {
        if (!ghosts.by_player.contains(marker.id)) {
          gone.push_back(ghost);
        }
      });
  for (flecs::entity ghost : gone) {
    ghost.destruct();
  }
}

void RegisterComponents(flecs::world world) {
  z13::flecs_tools::RegisterComponents<GhostPlayers>(world);
}

void RegisterSystems(flecs::world world) {
  world.set(GhostPlayers {});
  world.system<GhostPlayers, const ft::SimulationClock, const z13::NetTuning>("GhostPlayerSystem::Sync")
      .kind(flecs::PostUpdate)
      .without<ft::ReplayInProgress>()
      .write<z13::gameplay::GhostPlayer>()
      .write<Eigen::Matrix4f>()
      .each(SyncGhosts);
}

}  // namespace

void StoreGhostPoses(flecs::world world, std::span<const uint8_t> bytes) {
  const auto packet = DecodePoses(bytes, world.get<ft::SimulationClock>().tick);
  if (!packet) {
    return;
  }
  const std::optional<uint32_t> own = world.get<z13::gameplay::LocalPlayer>().id;
  auto& ghosts = world.get_mut<GhostPlayers>().by_player;
  for (const PoseRecord& record : packet->poses) {
    if (record.player_id == own || IsSimulated(world, record.player_id)) {
      continue;
    }
    GhostPose& stored = ghosts[record.player_id];
    if (packet->tick > stored.tick) {
      stored = {.position = record.position, .look = record.look, .tick = packet->tick};
    }
  }
}

void GhostPlayerSystem::Register(flecs::world& world) {
  z13::OnRegisterComponents(world, RegisterComponents);
  z13::OnInitSystems(world, RegisterSystems);
}

}  // namespace z13::net
